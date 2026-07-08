// VRM4U Copyright (c) 2021-2026 Haruyoshi Yamamoto. This software is released under the MIT License.

#include "VrmVMCRetargetAnimInstance.h"

#if	UE_VERSION_OLDER_THAN(5,2,0)
#else

#include "Components/SkeletalMeshComponent.h"
#include "Retargeter/IKRetargeter.h"
#include "AnimNodes/AnimNode_RetargetPoseFromMesh.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Rig/IKRigDefinition.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectIterator.h"

/////  Proxy  /////

void FVrmVMCRetargetAnimInstanceProxy::EnsureNode()
{
	if (Node_Retarget.Get() == nullptr)
	{
		Node_Retarget = MakeShareable(new FAnimNode_RetargetPoseFromMesh());
		FAnimationInitializeContext InitContext(this);
		Node_Retarget->Initialize_AnyThread(InitContext);
	}
}

void FVrmVMCRetargetAnimInstanceProxy::Initialize(UAnimInstance* InAnimInstance)
{
	Super::Initialize(InAnimInstance);
	EnsureNode();
}

void FVrmVMCRetargetAnimInstanceProxy::UpdateAnimationNode(const FAnimationUpdateContext& InContext)
{
	EnsureNode();
	if (Node_Retarget.Get())
	{
		// Drives DeltaTime/RelevancyWeight into the node. Without it CacheBones/Evaluate run on stale state.
		Node_Retarget->Update_AnyThread(InContext);
	}
}

void FVrmVMCRetargetAnimInstanceProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	Super::PreUpdate(InAnimInstance, DeltaSeconds);

	EnsureNode();
	if (Node_Retarget.Get())
	{
		FAnimNode_RetargetPoseFromMesh* Node = Node_Retarget.Get();

		Node->IKRetargeterAsset = Retargeter.Get();
		Node->RetargetFrom = ERetargetSourceMode::CustomSkeletalMeshComponent;
		Node->SourceMeshComponent = SourceMeshComponent;

		FAnimationInitializeContext InitContext(this);
		if (Node->GetRetargetProcessor() == nullptr)
		{
			Node->Initialize_AnyThread(InitContext);
		}

		Node->PreUpdate(InAnimInstance);
	}
}

bool FVrmVMCRetargetAnimInstanceProxy::Evaluate(FPoseContext& Output)
{
	if (Node_Retarget.Get() && Retargeter.IsValid() && SourceMeshComponent.IsValid())
	{
		FAnimNode_RetargetPoseFromMesh* Node = Node_Retarget.Get();

		FAnimationCacheBonesContext CacheContext(this);
		Node->CacheBones_AnyThread(CacheContext);

		Node->Evaluate_AnyThread(Output);
		return true;
	}

	// Nothing wired yet: hold the reference pose rather than leaving it uninitialized.
	Output.ResetToRefPose();
	return true;
}

/////  AnimInstance  /////

UVrmVMCRetargetAnimInstance::UVrmVMCRetargetAnimInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// We read the pose from another component and poke the retarget node from the game thread.
	bUsingCopyPoseFromMesh = true;
	bUseMultiThreadedAnimationUpdate = false;
}

FAnimInstanceProxy* UVrmVMCRetargetAnimInstance::CreateAnimInstanceProxy()
{
	MyProxy = new FVrmVMCRetargetAnimInstanceProxy(this);
	return MyProxy;
}

void UVrmVMCRetargetAnimInstance::SetRetargetSource(USkeletalMeshComponent* InSource, UIKRetargeter* InRetargeter)
{
	SourceMeshComponent = InSource;
	Retargeter = InRetargeter;
	bTickPrereqApplied = false; // re-evaluate the prerequisite for the new source
}

void UVrmVMCRetargetAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	AutoResolveSourceAndRetargeter();

	if (MyProxy)
	{
		MyProxy->SourceMeshComponent = SourceMeshComponent;
		MyProxy->Retargeter = Retargeter;
	}

	if (bAddSourceTickPrerequisite && !bTickPrereqApplied && SourceMeshComponent)
	{
		if (USkeletalMeshComponent* TargetMesh = GetSkelMeshComponent())
		{
			if (USkeletalMeshComponent* OldSource = PrevSourcePrereq.Get())
			{
				if (OldSource != SourceMeshComponent)
				{
					TargetMesh->RemoveTickPrerequisiteComponent(OldSource);
				}
			}
			TargetMesh->AddTickPrerequisiteComponent(SourceMeshComponent);
			PrevSourcePrereq = SourceMeshComponent;
			bTickPrereqApplied = true;
		}
	}

	CapturePosesForDebug();
}

void UVrmVMCRetargetAnimInstance::NativeUninitializeAnimation()
{
	if (USkeletalMeshComponent* OldSource = PrevSourcePrereq.Get())
	{
		if (USkeletalMeshComponent* TargetMesh = GetSkelMeshComponent())
		{
			TargetMesh->RemoveTickPrerequisiteComponent(OldSource);
		}
		PrevSourcePrereq = nullptr;
	}
	Super::NativeUninitializeAnimation();
}

void UVrmVMCRetargetAnimInstance::AutoResolveSourceAndRetargeter()
{
	if (!bAutoResolveSourceAndRetargeter || (SourceMeshComponent != nullptr && Retargeter != nullptr))
	{
		return;
	}
	UWorld* World = GetWorld();
	USkeletalMeshComponent* Target = GetSkelMeshComponent();
	if (World == nullptr || !World->IsGameWorld() ||
		Target == nullptr || Target->GetSkeletalMeshAsset() == nullptr)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	if (Now - LastAutoResolveTimeSeconds < AutoResolveRetryIntervalSeconds)
	{
		return;
	}
	LastAutoResolveTimeSeconds = Now;
	// Back off on repeated failure so an unresolved instance doesn't rescan the whole
	// asset registry + world every second forever. Reset to the fast interval once resolved.
	AutoResolveRetryIntervalSeconds = FMath::Min(AutoResolveRetryIntervalSeconds * 2.0, 15.0);

	const FReferenceSkeleton& TargetRef = Target->GetSkeletalMeshAsset()->GetRefSkeleton();

	if (Retargeter == nullptr)
	{
		const FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FAssetData> Assets;
		AssetRegistry.Get().GetAssetsByClass(UIKRetargeter::StaticClass()->GetClassPathName(), Assets, true);
		UIKRetargeter* Best = nullptr;
		int32 BestScore = 0;
		for (const FAssetData& AssetData : Assets)
		{
			UIKRetargeter* Candidate = Cast<UIKRetargeter>(AssetData.GetAsset());
			const UIKRigDefinition* TgtRig = Candidate ? Candidate->GetIKRig(ERetargetSourceOrTarget::Target) : nullptr;
			const UIKRigDefinition* SrcRig = Candidate ? Candidate->GetIKRig(ERetargetSourceOrTarget::Source) : nullptr;
			if (TgtRig == nullptr || SrcRig == nullptr)
			{
				continue;
			}
			int32 Score = 0;
			if (TgtRig->GetPreviewMesh() == Target->GetSkeletalMeshAsset())
			{
				Score += 4;
			}
			const FName TgtPelvis = TgtRig->GetPelvis();
			const FName SrcPelvis = SrcRig->GetPelvis();
			if (TgtPelvis != NAME_None && TargetRef.FindBoneIndex(TgtPelvis) != INDEX_NONE)
			{
				Score += 2;
			}
			// Prefer a genuinely cross-rig retargeter over an identity one.
			if (SrcPelvis != NAME_None && SrcPelvis != TgtPelvis && TargetRef.FindBoneIndex(SrcPelvis) == INDEX_NONE)
			{
				Score += 1;
			}
			if (Score >= 2 && Score > BestScore)
			{
				BestScore = Score;
				Best = Candidate;
			}
		}
		if (Best)
		{
			Retargeter = Best;
			UE_LOG(LogTemp, Display, TEXT("[VRM4U] VMC retarget auto-resolved Retargeter=%s for %s (score %d)"),
				*Best->GetName(), *Target->GetSkeletalMeshAsset()->GetName(), BestScore);
		}
	}

	if (SourceMeshComponent == nullptr && Retargeter != nullptr)
	{
		const UIKRigDefinition* SrcRig = Retargeter->GetIKRig(ERetargetSourceOrTarget::Source);
		if (SrcRig == nullptr)
		{
			return;
		}
		USkeletalMesh* SrcPreviewMesh = SrcRig->GetPreviewMesh();
		const FName SrcPelvis = SrcRig->GetPelvis();
		USkeletalMeshComponent* Best = nullptr;
		int32 BestScore = 0;
		for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
		{
			USkeletalMeshComponent* Candidate = *It;
			if (!IsValid(Candidate) || Candidate == Target || Candidate->GetWorld() != World ||
				Candidate->GetSkeletalMeshAsset() == nullptr ||
				Candidate->GetSkeletalMeshAsset() == Target->GetSkeletalMeshAsset())
			{
				continue;
			}
			int32 Score = 0;
			if (SrcPreviewMesh != nullptr && Candidate->GetSkeletalMeshAsset() == SrcPreviewMesh)
			{
				Score += 4;
			}
			if (SrcPelvis != NAME_None &&
				Candidate->GetSkeletalMeshAsset()->GetRefSkeleton().FindBoneIndex(SrcPelvis) != INDEX_NONE)
			{
				Score += 2;
			}
			if (Score >= 2 && Score > BestScore)
			{
				BestScore = Score;
				Best = Candidate;
			}
		}
		if (Best)
		{
			SourceMeshComponent = Best;
			bTickPrereqApplied = false;
			UE_LOG(LogTemp, Display, TEXT("[VRM4U] VMC retarget auto-resolved SourceMeshComponent=%s (%s, score %d)"),
				*Best->GetName(), *Best->GetSkeletalMeshAsset()->GetName(), BestScore);
		}
	}

	if (Retargeter != nullptr && SourceMeshComponent != nullptr)
	{
		AutoResolveRetryIntervalSeconds = 1.0;
	}
	// Still unresolved: say so where the user is looking. Otherwise the mesh just T-poses
	// silently and the cause (missing RTG asset vs missing source rig in the level) is
	// invisible outside the log.
	else
	{
		const FString Missing = (Retargeter == nullptr && SourceMeshComponent == nullptr)
			? TEXT("no IK Retargeter asset matches this mesh, and no source rig found")
			: (Retargeter == nullptr
				? TEXT("no IK Retargeter asset matches this mesh (create one, or run VRM4U: Auto-Setup VMC Retarget)")
				: TEXT("no live source rig matching the retargeter's source IK rig found in this level"));
		// The resolve retries on a backoff, so a persistent failure only needs an occasional
		// log line (the keyed on-screen message below already refreshes continuously).
		if (Now - LastResolveFailLogTimeSeconds > 10.0)
		{
			LastResolveFailLogTimeSeconds = Now;
			UE_LOG(LogTemp, Warning, TEXT("[VRM4U] VMC retarget auto-resolve FAILED on '%s': %s."),
				*Target->GetSkeletalMeshAsset()->GetName(), *Missing);
		}
#if !UE_BUILD_SHIPPING
		if (GEngine)
		{
			// Stable per-instance key: updates in place instead of stacking a new line
			// every retry.
			GEngine->AddOnScreenDebugMessage((uint64)(PTRINT)this, 1.5f, FColor::Orange,
				FString::Printf(TEXT("VRM4U VMC retarget '%s': %s"),
					*Target->GetSkeletalMeshAsset()->GetName(), *Missing));
		}
#endif
	}
}

// Format (space-separated, one line per rig):
//   VRMCAP HDR <rig> <meshName> <boneCount> <boneName>*N
//   VRMCAP POSE <timeSeconds> <rig> <boneCount> (<px py pz qx qy qz qw>)*N
// Transforms are the component-space transforms in the mesh's reference-skeleton bone order,
// the same space/order FIKRetargetProcessor consumes/produces, so a capture replays directly.
void UVrmVMCRetargetAnimInstance::CapturePosesForDebug()
{
#if WITH_EDITOR
	if (!bDebugCapturePoses)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (World == nullptr || !World->IsGameWorld())
	{
		return;
	}
	USkeletalMeshComponent* Target = GetSkelMeshComponent();
	USkeletalMeshComponent* Source = SourceMeshComponent;
	if (Target == nullptr || Source == nullptr ||
		Target->GetSkeletalMeshAsset() == nullptr || Source->GetSkeletalMeshAsset() == nullptr)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	if (Now - LastDebugCaptureTimeSeconds < DebugCaptureIntervalSeconds)
	{
		return;
	}

	// Both components must have evaluated full-skeleton transforms this session.
	const auto ValidCount = [](USkeletalMeshComponent* Comp)
	{
		return Comp->GetComponentSpaceTransforms().Num() ==
			Comp->GetSkeletalMeshAsset()->GetRefSkeleton().GetNum();
	};
	if (!ValidCount(Source) || !ValidCount(Target))
	{
		return;
	}
	LastDebugCaptureTimeSeconds = Now;

	FString Chunk;
	if (DebugCaptureFilePath.IsEmpty())
	{
		const FString CaptureDir = FPaths::ProjectSavedDir() / TEXT("VRM4U");
		IFileManager::Get().MakeDirectory(*CaptureDir, /*Tree =*/ true); // SaveStringToFile won't create it
		DebugCaptureFilePath = CaptureDir /
			FString::Printf(TEXT("PoseCapture_%s.txt"), *FDateTime::Now().ToString());
		for (const auto& RigComp : { TPair<const TCHAR*, USkeletalMeshComponent*>(TEXT("source"), Source),
									 TPair<const TCHAR*, USkeletalMeshComponent*>(TEXT("target"), Target) })
		{
			const FReferenceSkeleton& Ref = RigComp.Value->GetSkeletalMeshAsset()->GetRefSkeleton();
			Chunk += FString::Printf(TEXT("VRMCAP HDR %s %s %d"),
				RigComp.Key, *RigComp.Value->GetSkeletalMeshAsset()->GetName(), Ref.GetNum());
			for (int32 i = 0; i < Ref.GetNum(); ++i)
			{
				Chunk += TEXT(" ") + Ref.GetBoneName(i).ToString();
			}
			Chunk += LINE_TERMINATOR;
		}
		UE_LOG(LogTemp, Display, TEXT("[VRM4U] VMC retarget pose capture -> %s"), *DebugCaptureFilePath);
	}

	for (const auto& RigComp : { TPair<const TCHAR*, USkeletalMeshComponent*>(TEXT("source"), Source),
								 TPair<const TCHAR*, USkeletalMeshComponent*>(TEXT("target"), Target) })
	{
		const TArray<FTransform>& Xf = RigComp.Value->GetComponentSpaceTransforms();
		Chunk += FString::Printf(TEXT("VRMCAP POSE %.3f %s %d"), Now, RigComp.Key, Xf.Num());
		for (const FTransform& T : Xf)
		{
			const FVector P = T.GetLocation();
			const FQuat Q = T.GetRotation();
			Chunk += FString::Printf(TEXT(" %.3f %.3f %.3f %.6f %.6f %.6f %.6f"),
				P.X, P.Y, P.Z, Q.X, Q.Y, Q.Z, Q.W);
		}
		Chunk += LINE_TERMINATOR;
	}
	FFileHelper::SaveStringToFile(Chunk, *DebugCaptureFilePath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
#endif // WITH_EDITOR
}

#endif // 5.2
