// VRM4U Copyright (c) 2021-2026 Haruyoshi Yamamoto. This software is released under the MIT License.


#include "AnimNode_VrmVMC.h"
#include "AnimationRuntime.h"
#include "Animation/AnimInstanceProxy.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/Engine.h"
#include "DrawDebugHelpers.h"

#include "VRM4U_VMCSubsystem.h"
#include "VrmAssetListObject.h"
#include "VrmMetaObject.h"
#include "VrmUtil.h"

#include "OWLVrmVMCNodeRegistry.h"

#include "Engine/SkeletalMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

#include <algorithm>

FAnimNode_VrmVMC::FAnimNode_VrmVMC()
		: BoneStateLock(MakeShared<FCriticalSection>())
{
}


FAnimNode_VrmVMC::~FAnimNode_VrmVMC()
{
	FOWLVrmVMCNodeRegistry::Unregister(this);
}


void FAnimNode_VrmVMC::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	Super::Initialize_AnyThread(Context);

	// Defensive: handle copy-from-template paths that bypass our constructor.
	if (!BoneStateLock.IsValid())
	{
		BoneStateLock = MakeShared<FCriticalSection>();
	}

	VrmMetaObject_Internal = VrmMetaObject;
	if (VrmMetaObject_Internal == nullptr && EnableAutoSearchMetaData)
	{
		VrmAssetListObject_Internal = VRMUtil::GetAssetListObject(
			VRMGetSkinnedAsset(Context.AnimInstanceProxy->GetSkelMeshComponent()));
		if (VrmAssetListObject_Internal)
		{
			VrmMetaObject_Internal = VrmAssetListObject_Internal->VrmMetaObject;
		}
	}

	UVRM4U_VMCSubsystem* subsystem = GEngine->GetEngineSubsystem<UVRM4U_VMCSubsystem>();
	if (subsystem == nullptr) return;
	{
		auto* s = subsystem->FindOrAddServer(ServerAddress, Port);
		if (s)
		{
			s->bForceUpdate = bForceUpdate;

			switch (PerformanceMode)
			{
			case EVMCPerformanceMode::Performance:
				s->UpdateThrottleTime = 1.0f / 30.0f; // 30 FPS
				s->bAdaptiveThrottling = false;
				break;

			case EVMCPerformanceMode::Balanced:
				s->UpdateThrottleTime = 1.0f / 60.0f; // 60 FPS
				s->bAdaptiveThrottling = false;
				break;

			case EVMCPerformanceMode::Streaming:
				s->UpdateThrottleTime = 1.0f / 90.0f; // 90 FPS
				s->bAdaptiveThrottling = false;
				break;

			case EVMCPerformanceMode::Adaptive:
				s->UpdateThrottleTime = 1.0f / 60.0f; // Base rate
				s->bAdaptiveThrottling = true; // Enable adaptive
				break;

			case EVMCPerformanceMode::Custom:
				int ClampedRate = FMath::Clamp(CustomUpdateRate, 30, 120);
				s->UpdateThrottleTime = 1.0f / ClampedRate;
				s->bAdaptiveThrottling = false;
				break;
			}
		}
	}
	bCreateServer = true;

	// init global reftransform
	{
		const auto Skeleton = Context.AnimInstanceProxy->GetSkeleton();
		if (Skeleton != nullptr)
		{
			const auto& RefSkeleton = Skeleton->GetReferenceSkeleton();
			const auto& RefSkeletonTransform = RefSkeleton.GetRefBonePose();

			FScopeLock Lock(BoneStateLock.Get());
			RefSkeletonTransform_global = RefSkeletonTransform;
			for (int i = 0; i < RefSkeletonTransform.Num(); ++i)
			{
				int parent = RefSkeleton.GetParentIndex(i);
				if (parent < 0) continue;
				RefSkeletonTransform_global[i] = RefSkeletonTransform_global[i] * RefSkeletonTransform_global[parent];
			}
		}
	}

	{
		const auto SkelForFlag = Context.AnimInstanceProxy->GetSkeleton();

		// Identify the exact component this node runs on, so a duplicate / stray
		// VMC node (e.g. a second MetaHuman mesh component or a post-process ABP)
		// can be located and removed. Format: "<OwnerActor> / <Component>".
		const USkeletalMeshComponent* InitComp = Context.AnimInstanceProxy->GetSkelMeshComponent();
		const AActor* InitOwner = (InitComp != nullptr) ? InitComp->GetOwner() : nullptr;
		const FString CompId = FString::Printf(TEXT("%s / %s"),
		                                       InitOwner != nullptr ? *InitOwner->GetName() : TEXT("<no owner>"),
		                                       InitComp != nullptr ? *InitComp->GetName() : TEXT("<no comp>"));

		UE_LOG(LogTemp, Log,
		       TEXT("[VMC Init] [%s] Skeleton=%s bIgnoreLocalRotation=%s HasMeta=%s"),
		       *CompId,
		       SkelForFlag ? *SkelForFlag->GetName() : TEXT("<null>"),
		       bIgnoreLocalRotation ? TEXT("true") : TEXT("false"),
		       (VrmMetaObject_Internal != nullptr) ? TEXT("true") : TEXT("false"));

		// Fires once per init (not per frame). If no humanoid table resolved, the
		// node drives nothing — name the exact component so the user can find it.
		if (VrmMetaObject_Internal == nullptr)
		{
			UE_LOG(LogTemp, Warning,
			       TEXT("[VMC] No Vrm Meta Object on component [%s] (Skeleton '%s') -- this VRM VMC node drives no "
			            "bones. Assign a Vrm Meta Object to this node, or DELETE this duplicate/stray VMC node. "
			            "(Auto-search resolves VRM-imported meshes only.)"),
			       *CompId,
			       SkelForFlag ? *SkelForFlag->GetName() : TEXT("<null>"));
		}
	}

	if (USkeletalMeshComponent* Component = Context.AnimInstanceProxy->GetSkelMeshComponent())
	{
		UAnimInstance* AnimInstance = Cast<UAnimInstance>(Context.AnimInstanceProxy->GetAnimInstanceObject());
		FOWLVrmVMCNodeRegistry::Register(Component, AnimInstance, this);
	}
}

void FAnimNode_VrmVMC::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	Super::CacheBones_AnyThread(Context);
}


void FAnimNode_VrmVMC::UpdateInternal(const FAnimationUpdateContext& Context)
{
	Super::UpdateInternal(Context);
}


void FAnimNode_VrmVMC::GatherDebugData(FNodeDebugData& DebugData)
{
	FString DebugLine = DebugData.GetNodeName(this);

	DebugLine += "(";
	AddDebugNodeData(DebugLine);
	DebugData.AddDebugItem(DebugLine);

	ComponentPose.GatherDebugData(DebugData);
}

void FAnimNode_VrmVMC::EvaluateComponentPose_AnyThread(FComponentSpacePoseContext& Output)
{
	Super::EvaluateComponentPose_AnyThread(Output);
}

void FAnimNode_VrmVMC::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,
                                                         TArray<FBoneTransform>& OutBoneTransforms)
{
	check(OutBoneTransforms.Num() == 0);

	if (VrmMetaObject_Internal == nullptr)
	{
		return;
	}

	UVRM4U_VMCSubsystem* subsystem = GEngine->GetEngineSubsystem<UVRM4U_VMCSubsystem>();
	if (subsystem == nullptr) return;

	const auto Skeleton = Output.AnimInstanceProxy->GetSkeleton();
	if (Skeleton == nullptr)
	{
		return; // skeleton can be null mid mesh/skeleton swap on the worker thread
	}
	const auto RefSkeleton = Skeleton->GetReferenceSkeleton();
	const FTransform ComponentTransform = Output.AnimInstanceProxy->GetComponentTransform();
	const auto& RefSkeletonTransform = RefSkeleton.GetRefBonePose();
	const FBoneContainer& RequiredBones = Output.Pose.GetPose().GetBoneContainer();

	if (RefSkeletonTransform_global.Num() != RefSkeletonTransform.Num())
	{
		return;
	}

	TArray<int> boneIndexTable;
	TArray<FBoneTransform> tmpOutTransform;

	FVMCData VMCData;
	if (subsystem->CopyVMCData(VMCData, ServerAddress, Port) == false)
	{
		return;
	}

	if (VMCData.BoneData.Num() == 0 && VMCData.CurveData.Num() == 0)
	{
		return;
	}
	if (bApplyPerfectSync)
	{
		for (auto& c : VMCData.CurveData)
		{
			if (c.Key.Contains(TEXT("BlendShape.")) == false) continue;

			c.Key.RightChopInline(11); // [blendshape.]
		}
	}

TMap<FString, FTransform>& BoneTrans = VMCData.BoneData;

	// Refresh the mask/override snapshot only when a Blueprint mutator changed
	// it, rather than copying both state maps every eval. The cached copies are
	// read on this worker thread only; the maps and dirty flag live under lock.
	{
		FScopeLock Lock(BoneStateLock.Get());
		if (bStateSnapshotDirty)
		{
			BoneStatesSnapshotCached = BoneStates;
			CurveStatesSnapshotCached = CurveStates;
			bStateSnapshotDirty = false;
		}
	}
	const TMap<FString, FOWLVMCPerCurveState>& CurveStatesSnapshot = CurveStatesSnapshotCached;

	struct FPendingCurveLastApplied
	{
		FString Key;
		float Value;
	};
	TArray<FPendingCurveLastApplied> PendingCurveLastApplied;
	PendingCurveLastApplied.Reserve(VMCData.CurveData.Num());

	// Guard the GetSkelMeshComponent()->GetSkinnedAsset()->GetMorphTargets()
	// chain: GetSkinnedAsset() can be null during a mesh swap / streaming /
	// level transition, and this runs every frame curve data is present.
	USkeletalMeshComponent* CurveSkelComp = Output.AnimInstanceProxy->GetSkelMeshComponent();
	USkinnedAsset* CurveSkinnedAsset = CurveSkelComp ? CurveSkelComp->GetSkinnedAsset() : nullptr;
	static const TArray<TObjectPtr<UMorphTarget>> EmptyMorphList;
	const TArray<TObjectPtr<UMorphTarget>>& MorphList =
		CurveSkinnedAsset ? CurveSkinnedAsset->GetMorphTargets() : EmptyMorphList;
	for (auto& c : VMCData.CurveData)
	{
		const FOWLVMCPerCurveState* SnapshotState = CurveStatesSnapshot.Find(c.Key);
		const bool bHasSnapshot = (SnapshotState != nullptr);

		// Masked curves skip the apply entirely, leaving the upstream curve
		// value intact.
		if (bHasSnapshot && SnapshotState->bMasked)
		{
			continue;
		}

		// Override value substitutes for the incoming VMC curve value before
		// the morph target lookup.
		float FinalValue = c.Value;
		if (bHasSnapshot && SnapshotState->bHasOverride)
		{
			FinalValue = SnapshotState->OverrideValue;
		}

#if	UE_VERSION_OLDER_THAN(5, 3, 0)
		{
			SmartName::UID_Type NewUID;
			FName NewName = *c.Key;

			NewUID = Skeleton->GetUIDByName(USkeleton::AnimCurveMappingName, NewName);

			Output.Curve.Set(NewUID, FinalValue);
		}
#else
		auto m = MorphList.FindByPredicate([&c](const TObjectPtr<UMorphTarget>& m)
		{
			FString s = c.Key;

			if (m->GetName().Compare(s, ESearchCase::IgnoreCase))
			{
				return false;
			}
			return true;
		});
		if (m)
		{
			Output.Curve.Set(*m->GetName(), FinalValue);
		}
		else
		{
			Output.Curve.Set(*c.Key, FinalValue);
		}
#endif

		FPendingCurveLastApplied& Pending = PendingCurveLastApplied.AddDefaulted_GetRef();
		Pending.Key = c.Key;
		Pending.Value = FinalValue;
	}

	// Flush accumulated curve last-applied writes under a single lock. BP
	// writes that landed mid-loop are not reflected in this frame's eval and
	// will surface next frame.
	{
		FScopeLock Lock(BoneStateLock.Get());
		for (const FPendingCurveLastApplied& Pending : PendingCurveLastApplied)
		{
			FOWLVMCPerCurveState& Cache = CurveStates.FindOrAdd(Pending.Key);
			Cache.LastAppliedValue = Pending.Value;
			Cache.bHasLastApplied = true;
		}
	}

	{
		{
			// Resolve the VMC "root" stream entry once (/VMC/Ext/Root/Pos). Absent on some senders.
			FVector VmcRootTranslation = FVector::ZeroVector;
			FTransform VmcRootTransform = FTransform::Identity;
			bool bHasVmcRoot = false;
			for (const auto& a : BoneTrans)
			{
				if (a.Key.Compare(TEXT("root"), ESearchCase::IgnoreCase) == 0)
				{
					VmcRootTransform = a.Value;
					VmcRootTranslation = a.Value.GetLocation();
					bHasVmcRoot = true;
					break;
				}
			}
			
			const TMap<FString, FOWLVMCPerBoneState>& BoneStatesSnapshot = BoneStatesSnapshotCached;

			struct FPendingLastApplied
			{
				FString Key;
				FQuat Rotation;
				FTransform Transform;
			};
			TArray<FPendingLastApplied> PendingLastApplied;
			PendingLastApplied.Reserve(VrmMetaObject_Internal->humanoidBoneTable.Num());

			for (const auto& t : VrmMetaObject_Internal->humanoidBoneTable)
			{
#if   UE_VERSION_OLDER_THAN(4, 27, 0)
				auto* tmpVal = BoneTrans.Find(t.Key.ToLower());
				if (tmpVal == nullptr) continue;

				auto modelBone = *tmpVal;
#else
				auto filterList = BoneTrans.FilterByPredicate([&t](TPair<FString, FTransform> a)
					{
						return a.Key.Compare(t.Key, ESearchCase::IgnoreCase) == 0;
					}
				);
				if (filterList.Num() != 1) continue;
				auto modelBone = filterList.begin()->Value;
#endif

				int index = RefSkeleton.FindBoneIndex(*t.Value);
				if (index < 0) continue;

				FCompactPoseBoneIndex CompactIndex = RequiredBones.GetCompactPoseIndexFromSkeletonIndex(index);
				if (CompactIndex == FCompactPoseBoneIndex(INDEX_NONE)) continue;

				FBoneTransform f(CompactIndex, modelBone);

				const bool bIsHumanoidHips = (t.Key.Compare(TEXT("hips"), ESearchCase::IgnoreCase) == 0);

				if (bIsHumanoidHips)
				{
					if (bUseRemoteCenterPos)
					{
						FVector v = f.Transform.GetLocation() * ModelRelativeScale;
						// Fold root motion into hips only when hips is the skeleton root;
						// otherwise it drives the separate root bone (index 0) below.
						f.Transform.SetTranslation(index == 0 ? v + VmcRootTranslation : v);
					}
					else
					{
						FVector v = RefSkeletonTransform[index].GetLocation();
						f.Transform.SetTranslation(v);
					}
				}
				else
				{
					FVector v = RefSkeletonTransform[index].GetLocation();
					f.Transform.SetTranslation(v);
				}

				// Snapshot per-bone state under lock. Mask, overrides, and the
				// last-applied write below all consult this snapshot.
				FOWLVMCPerBoneState SnapshotState;
				bool bHasSnapshot = false;
				if (const FOWLVMCPerBoneState* Found = BoneStatesSnapshot.Find(t.Key))
				{
					SnapshotState = *Found;
					bHasSnapshot = true;
				}

				// Masked bones skip the rebase entirely and do not contribute to
				// the output transform set, leaving the upstream pose intact.
				if (bHasSnapshot && SnapshotState.bMasked)
				{
					continue;
				}

				// Pre-rebase override substitutes for the incoming VMC rotation
				// before the rebase formula runs.
				if (bHasSnapshot && SnapshotState.bHasPreRebase)
				{
					f.Transform.SetRotation(SnapshotState.PreRebaseRotation);
				}

				if (bIgnoreLocalRotation)
				{
					if (bHasSnapshot && SnapshotState.bHasPostRebase)
					{
						// Post-rebase override bypasses the rebase formula.
						f.Transform.SetRotation(SnapshotState.PostRebaseRotation);
					}
					else
					{
						auto r_refg = RefSkeletonTransform_global[index].GetRotation();
						auto r_ref = RefSkeletonTransform[index].GetRotation();
						auto r_vmc = f.Transform.GetRotation();

						FQuat r_dif = r_refg.Inverse() * r_vmc * r_refg;

						f.Transform.SetRotation(r_ref * r_dif);
					}
				}
				else if (bHasSnapshot && SnapshotState.bHasPostRebase)
				{
					// Post-rebase override also applies when bIgnoreLocalRotation
					// is false. Pre-rebase override is meaningless here and is
					// silently ignored if combined with post-rebase.
					f.Transform.SetRotation(SnapshotState.PostRebaseRotation);
				}

				FPendingLastApplied& Pending = PendingLastApplied.AddDefaulted_GetRef();
				Pending.Key = t.Key;
				Pending.Rotation = f.Transform.GetRotation();
				Pending.Transform = f.Transform;

				tmpOutTransform.Add(f);
				boneIndexTable.Add(index);

				// Rigs whose hips is not the skeleton root (Mixamo/Mannequin-style)
				// carry locomotion on a separate root bone; drive it from the stream.
				if (bIsHumanoidHips && index != 0 && bHasVmcRoot)
				{
					const FCompactPoseBoneIndex RootCompactIndex =
						RequiredBones.GetCompactPoseIndexFromSkeletonIndex(0);
					if (RootCompactIndex != FCompactPoseBoneIndex(INDEX_NONE))
					{
						tmpOutTransform.Add(FBoneTransform(RootCompactIndex, VmcRootTransform));
						boneIndexTable.Add(0);
					}
				}
			}

			// Flush accumulated last-applied writes under a single lock. BP
			// writes that landed mid-loop are not reflected in this frame's
			// eval and will surface next frame.
			{
				FScopeLock Lock(BoneStateLock.Get());
				for (const FPendingLastApplied& Pending : PendingLastApplied)
				{
					FOWLVMCPerBoneState& Cache = BoneStates.FindOrAdd(Pending.Key);
					Cache.LastAppliedRotation = Pending.Rotation;
					Cache.LastAppliedTransform = Pending.Transform;
					Cache.bHasLastApplied = true;
				}
			}

			// bone hierarchy - Convert parent bone indices to compact indices
			for (int i = 1; i < tmpOutTransform.Num(); ++i)
			{
				int parentBoneIndex = RefSkeleton.GetParentIndex(boneIndexTable[i]);
				int parentInTable = boneIndexTable.Find(parentBoneIndex);

				for (int j = 0; j < 1000; ++j)
				{
					if (parentInTable >= 0)
					{
						break;
					}
					if (parentBoneIndex < 0) break;

					// Convert parent bone index to compact index before creating FBoneTransform
					FCompactPoseBoneIndex ParentCompactIndex = RequiredBones.GetCompactPoseIndexFromSkeletonIndex(
						parentBoneIndex);
					if (ParentCompactIndex != FCompactPoseBoneIndex(INDEX_NONE))
					{
						FBoneTransform f(ParentCompactIndex, RefSkeletonTransform[parentBoneIndex]);
						tmpOutTransform.Add(f);
						boneIndexTable.Add(parentBoneIndex);
					}

					parentBoneIndex = RefSkeleton.GetParentIndex(parentBoneIndex);
					parentInTable = boneIndexTable.Find(parentBoneIndex);
				}
			}

			tmpOutTransform.Sort(FCompareBoneTransformIndex());
			boneIndexTable.Sort();
		}
	}

	for (int i = 0; i < tmpOutTransform.Num(); ++i)
	{
		auto& a = tmpOutTransform[i];

		int parentBoneIndex = RefSkeleton.GetParentIndex(boneIndexTable[i]);

		int parentInHandTable = boneIndexTable.Find(parentBoneIndex);
		if (parentInHandTable >= 0)
		{
			a.Transform *= tmpOutTransform[parentInHandTable].Transform;
		}
		else
		{
			// root
			auto BoneSpace = EBoneControlSpace::BCS_ParentBoneSpace;
			FAnimationRuntime::ConvertBoneSpaceTransformToCS(ComponentTransform, Output.Pose, a.Transform, a.BoneIndex,
			                                                 BoneSpace);
		}
		OutBoneTransforms.Add(a);
	}
}

bool FAnimNode_VrmVMC::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	return true;
}

void FAnimNode_VrmVMC::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
}

void FAnimNode_VrmVMC::ConditionalDebugDraw(FPrimitiveDrawInterface* PDI, USkeletalMeshComponent* PreviewSkelMeshComp,
                                            bool bPreviewForeground) const
{
#if WITH_EDITOR

	if (VrmMetaObject_Internal == nullptr || PreviewSkelMeshComp == nullptr)
	{
		return;
	}
	if (PreviewSkelMeshComp->GetWorld() == nullptr)
	{
		return;
	}

	ESceneDepthPriorityGroup Priority = SDPG_World;
	if (bPreviewForeground) Priority = SDPG_Foreground;

#endif
}

bool FAnimNode_VrmVMC::TryGetLastAppliedRotation(const FString& HumanoidName, FQuat& OutRotation) const
{
	FScopeLock Lock(BoneStateLock.Get());
	if (const FOWLVMCPerBoneState* State = BoneStates.Find(HumanoidName))
	{
		if (State->bHasLastApplied)
		{
			OutRotation = State->LastAppliedRotation;
			return true;
		}
	}
	return false;
}

bool FAnimNode_VrmVMC::TryGetLastAppliedTransform(const FString& HumanoidName, FTransform& OutTransform) const
{
	FScopeLock Lock(BoneStateLock.Get());
	if (const FOWLVMCPerBoneState* State = BoneStates.Find(HumanoidName))
	{
		if (State->bHasLastApplied)
		{
			OutTransform = State->LastAppliedTransform;
			return true;
		}
	}
	return false;
}

void FAnimNode_VrmVMC::SetPreRebaseRotation(const FString& HumanoidName, const FQuat& Rotation)
{
	FScopeLock Lock(BoneStateLock.Get());
	FOWLVMCPerBoneState& State = BoneStates.FindOrAdd(HumanoidName);
	State.PreRebaseRotation = Rotation;
	State.bHasPreRebase = true;
	bStateSnapshotDirty = true;
}

void FAnimNode_VrmVMC::ClearPreRebaseRotation(const FString& HumanoidName)
{
	FScopeLock Lock(BoneStateLock.Get());
	if (FOWLVMCPerBoneState* State = BoneStates.Find(HumanoidName))
	{
		State->PreRebaseRotation = FQuat::Identity;
		State->bHasPreRebase = false;
	}
	bStateSnapshotDirty = true;
}

bool FAnimNode_VrmVMC::TryGetPreRebaseRotation(const FString& HumanoidName, FQuat& OutRotation) const
{
	FScopeLock Lock(BoneStateLock.Get());
	if (const FOWLVMCPerBoneState* State = BoneStates.Find(HumanoidName))
	{
		if (State->bHasPreRebase)
		{
			OutRotation = State->PreRebaseRotation;
			return true;
		}
	}
	return false;
}

bool FAnimNode_VrmVMC::TryGetPostRebaseRotation(const FString& HumanoidName, FQuat& OutRotation) const
{
	FScopeLock Lock(BoneStateLock.Get());
	if (const FOWLVMCPerBoneState* State = BoneStates.Find(HumanoidName))
	{
		if (State->bHasPostRebase)
		{
			OutRotation = State->PostRebaseRotation;
			return true;
		}
	}
	return false;
}

void FAnimNode_VrmVMC::ClearAllPreRebaseOverrides()
{
	FScopeLock Lock(BoneStateLock.Get());
	for (auto& Pair : BoneStates)
	{
		Pair.Value.PreRebaseRotation = FQuat::Identity;
		Pair.Value.bHasPreRebase = false;
	}
	bStateSnapshotDirty = true;
}

void FAnimNode_VrmVMC::SetPostRebaseRotation(const FString& HumanoidName, const FQuat& Rotation)
{
	FScopeLock Lock(BoneStateLock.Get());
	FOWLVMCPerBoneState& State = BoneStates.FindOrAdd(HumanoidName);
	State.PostRebaseRotation = Rotation;
	State.bHasPostRebase = true;
	bStateSnapshotDirty = true;
}

void FAnimNode_VrmVMC::ClearPostRebaseRotation(const FString& HumanoidName)
{
	FScopeLock Lock(BoneStateLock.Get());
	if (FOWLVMCPerBoneState* State = BoneStates.Find(HumanoidName))
	{
		State->PostRebaseRotation = FQuat::Identity;
		State->bHasPostRebase = false;
	}
	bStateSnapshotDirty = true;
}

void FAnimNode_VrmVMC::ClearAllPostRebaseOverrides()
{
	FScopeLock Lock(BoneStateLock.Get());
	for (auto& Pair : BoneStates)
	{
		Pair.Value.PostRebaseRotation = FQuat::Identity;
		Pair.Value.bHasPostRebase = false;
	}
	bStateSnapshotDirty = true;
}

void FAnimNode_VrmVMC::SetBoneMasked(const FString& HumanoidName, bool bMasked)
{
	FScopeLock Lock(BoneStateLock.Get());
	FOWLVMCPerBoneState& State = BoneStates.FindOrAdd(HumanoidName);
	State.bMasked = bMasked;
	bStateSnapshotDirty = true;
}

bool FAnimNode_VrmVMC::IsBoneMasked(const FString& HumanoidName) const
{
	FScopeLock Lock(BoneStateLock.Get());
	if (const FOWLVMCPerBoneState* State = BoneStates.Find(HumanoidName))
	{
		return State->bMasked;
	}
	return false;
}

void FAnimNode_VrmVMC::ClearAllMasks()
{
	FScopeLock Lock(BoneStateLock.Get());
	for (auto& Pair : BoneStates)
	{
		Pair.Value.bMasked = false;
	}
	bStateSnapshotDirty = true;
}

bool FAnimNode_VrmVMC::IsPreRebaseOverridden(const FString& HumanoidName) const
{
	FScopeLock Lock(BoneStateLock.Get());
	if (const FOWLVMCPerBoneState* State = BoneStates.Find(HumanoidName))
	{
		return State->bHasPreRebase;
	}
	return false;
}

bool FAnimNode_VrmVMC::IsPostRebaseOverridden(const FString& HumanoidName) const
{
	FScopeLock Lock(BoneStateLock.Get());
	if (const FOWLVMCPerBoneState* State = BoneStates.Find(HumanoidName))
	{
		return State->bHasPostRebase;
	}
	return false;
}

void FAnimNode_VrmVMC::ClearBoneState(const FString& HumanoidName)
{
	FScopeLock Lock(BoneStateLock.Get());
	if (FOWLVMCPerBoneState* State = BoneStates.Find(HumanoidName))
	{
		State->PreRebaseRotation = FQuat::Identity;
		State->bHasPreRebase = false;
		State->PostRebaseRotation = FQuat::Identity;
		State->bHasPostRebase = false;
		State->bMasked = false;
	}
	bStateSnapshotDirty = true;
}

bool FAnimNode_VrmVMC::TryGetLastAppliedCurveValue(const FString& CurveName, float& OutValue) const
{
	FScopeLock Lock(BoneStateLock.Get());
	if (const FOWLVMCPerCurveState* State = CurveStates.Find(CurveName))
	{
		if (State->bHasLastApplied)
		{
			OutValue = State->LastAppliedValue;
			return true;
		}
	}
	return false;
}

void FAnimNode_VrmVMC::SetCurveOverride(const FString& CurveName, float Value)
{
	FScopeLock Lock(BoneStateLock.Get());
	FOWLVMCPerCurveState& State = CurveStates.FindOrAdd(CurveName);
	State.OverrideValue = Value;
	State.bHasOverride = true;
	bStateSnapshotDirty = true;
}

void FAnimNode_VrmVMC::ClearCurveOverride(const FString& CurveName)
{
	FScopeLock Lock(BoneStateLock.Get());
	if (FOWLVMCPerCurveState* State = CurveStates.Find(CurveName))
	{
		State->OverrideValue = 0.0f;
		State->bHasOverride = false;
	}
	bStateSnapshotDirty = true;
}

void FAnimNode_VrmVMC::ClearAllCurveOverrides()
{
	FScopeLock Lock(BoneStateLock.Get());
	for (auto& Pair : CurveStates)
	{
		Pair.Value.OverrideValue = 0.0f;
		Pair.Value.bHasOverride = false;
	}
	bStateSnapshotDirty = true;
}

void FAnimNode_VrmVMC::SetCurveMasked(const FString& CurveName, bool bMasked)
{
	FScopeLock Lock(BoneStateLock.Get());
	FOWLVMCPerCurveState& State = CurveStates.FindOrAdd(CurveName);
	State.bMasked = bMasked;
	bStateSnapshotDirty = true;
}

bool FAnimNode_VrmVMC::IsCurveMasked(const FString& CurveName) const
{
	FScopeLock Lock(BoneStateLock.Get());
	if (const FOWLVMCPerCurveState* State = CurveStates.Find(CurveName))
	{
		return State->bMasked;
	}
	return false;
}

void FAnimNode_VrmVMC::ClearAllCurveMasks()
{
	FScopeLock Lock(BoneStateLock.Get());
	for (auto& Pair : CurveStates)
	{
		Pair.Value.bMasked = false;
	}
	bStateSnapshotDirty = true;
}

bool FAnimNode_VrmVMC::IsCurveOverridden(const FString& CurveName) const
{
	FScopeLock Lock(BoneStateLock.Get());
	if (const FOWLVMCPerCurveState* State = CurveStates.Find(CurveName))
	{
		return State->bHasOverride;
	}
	return false;
}