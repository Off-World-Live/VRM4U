#include "VrmVMCBlueprintLibrary.h"

#include "AnimNode_VrmVMC.h"
#include "OWLVrmVMCNodeRegistry.h"
#include "OWLVrmVMCPoseSnapshot.h"
#include "VrmMetaObject.h"

#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/PlatformTime.h"
#include "ReferenceSkeleton.h"

namespace
{
	// Look up the active VMC anim node for a mesh component. Returns nullptr
	// when the mesh has no node registered, or when the owning anim instance
	// has been destroyed since registration.
	FAnimNode_VrmVMC* ResolveNode(USkeletalMeshComponent* SkelMeshComp)
	{
		if (SkelMeshComp == nullptr)
		{
			return nullptr;
		}
		const FOWLVrmVMCRegistryEntry Entry = FOWLVrmVMCNodeRegistry::Find(SkelMeshComp);
		if (!Entry.IsValid())
		{
			return nullptr;
		}
		return Entry.Node;
	}

	// Find a humanoid table entry by case-insensitive key match.
	const TPair<FString, FString>* FindHumanoidEntry(const FAnimNode_VrmVMC* Node, FName HumanoidName)
	{
		if (Node == nullptr || !Node->VrmMetaObject_Internal.IsValid())
		{
			return nullptr;
		}

		const FString Key = HumanoidName.ToString();
		const auto* Meta = Node->VrmMetaObject_Internal.Get();
		if (Meta == nullptr)
		{
			return nullptr;
		}

		for (const auto& Pair : Meta->humanoidBoneTable)
		{
			if (Pair.Key.Compare(Key, ESearchCase::IgnoreCase) == 0)
			{
				return &Pair;
			}
		}
		return nullptr;
	}

	// Resolve a humanoid name to its skel-mesh bone index. Returns INDEX_NONE
	// on any failure (no mesh, no skeleton, no mapping, name not in skeleton).
	int32 ResolveBoneIndex(USkeletalMeshComponent* SkelMeshComp, FAnimNode_VrmVMC* Node, FName HumanoidName)
	{
		const TPair<FString, FString>* Entry = FindHumanoidEntry(Node, HumanoidName);
		if (Entry == nullptr)
		{
			return INDEX_NONE;
		}

		USkeletalMesh* Mesh = SkelMeshComp->GetSkeletalMeshAsset();
		if (Mesh == nullptr || Mesh->GetSkeleton() == nullptr)
		{
			return INDEX_NONE;
		}

		const FReferenceSkeleton& RefSkeleton = Mesh->GetSkeleton()->GetReferenceSkeleton();
		return RefSkeleton.FindBoneIndex(*Entry->Value);
	}

	// Compose a component-space transform with the actor transform to get
	// world space. Falls back to component transform alone if no owner.
	FTransform ComposeWorldFromComponent(USkeletalMeshComponent* SkelMeshComp, const FTransform& ComponentTransform)
	{
		if (SkelMeshComp == nullptr)
		{
			return ComponentTransform;
		}
		return ComponentTransform * SkelMeshComp->GetComponentTransform();
	}

	// Map of humanoid name (VMC casing) to component-space transform, indexed
	// by humanoid name string. Used to derive local-space transforms by
	// looking up the humanoid parent's component-space transform.
	using FHumanoidComponentMap = TMap<FString, FTransform>;

	// Lookup table for the humanoid parent chain. Hard-coded from the VRM
	// humanoid spec, matched case-insensitively against the keys produced by
	// the VMC stream. Names with no parent map to an empty string; "hips" is
	// the root and has no humanoid parent.
	const TMap<FString, FString>& GetHumanoidParentMap()
	{
		static const TMap<FString, FString> ParentMap = []
		{
			TMap<FString, FString> M;
			M.Add(TEXT("hips"), TEXT(""));
			M.Add(TEXT("spine"), TEXT("hips"));
			M.Add(TEXT("chest"), TEXT("spine"));
			M.Add(TEXT("upperChest"), TEXT("chest"));
			M.Add(TEXT("neck"), TEXT("upperChest"));
			M.Add(TEXT("head"), TEXT("neck"));

			M.Add(TEXT("leftShoulder"), TEXT("upperChest"));
			M.Add(TEXT("leftUpperArm"), TEXT("leftShoulder"));
			M.Add(TEXT("leftLowerArm"), TEXT("leftUpperArm"));
			M.Add(TEXT("leftHand"), TEXT("leftLowerArm"));

			M.Add(TEXT("rightShoulder"), TEXT("upperChest"));
			M.Add(TEXT("rightUpperArm"), TEXT("rightShoulder"));
			M.Add(TEXT("rightLowerArm"), TEXT("rightUpperArm"));
			M.Add(TEXT("rightHand"), TEXT("rightLowerArm"));

			M.Add(TEXT("leftUpperLeg"), TEXT("hips"));
			M.Add(TEXT("leftLowerLeg"), TEXT("leftUpperLeg"));
			M.Add(TEXT("leftFoot"), TEXT("leftLowerLeg"));
			M.Add(TEXT("leftToes"), TEXT("leftFoot"));

			M.Add(TEXT("rightUpperLeg"), TEXT("hips"));
			M.Add(TEXT("rightLowerLeg"), TEXT("rightUpperLeg"));
			M.Add(TEXT("rightFoot"), TEXT("rightLowerLeg"));
			M.Add(TEXT("rightToes"), TEXT("rightFoot"));

			M.Add(TEXT("leftEye"), TEXT("head"));
			M.Add(TEXT("rightEye"), TEXT("head"));
			M.Add(TEXT("jaw"), TEXT("head"));

			const TCHAR* FingerSegments[] = {TEXT("Proximal"), TEXT("Intermediate"), TEXT("Distal")};
			const TCHAR* FingerNames[] = {TEXT("Thumb"), TEXT("Index"), TEXT("Middle"), TEXT("Ring"), TEXT("Little")};
			const TCHAR* Sides[] = {TEXT("left"), TEXT("right")};
			for (const TCHAR* Side : Sides)
			{
				for (const TCHAR* Finger : FingerNames)
				{
					const FString Hand = FString::Printf(TEXT("%sHand"), Side);
					const FString Proximal = FString::Printf(TEXT("%s%s%s"), Side, Finger, FingerSegments[0]);
					const FString Intermediate = FString::Printf(TEXT("%s%s%s"), Side, Finger, FingerSegments[1]);
					const FString Distal = FString::Printf(TEXT("%s%s%s"), Side, Finger, FingerSegments[2]);
					M.Add(Proximal, Hand);
					M.Add(Intermediate, Proximal);
					M.Add(Distal, Intermediate);
				}
			}

			return M;
		}();
		return ParentMap;
	}

	// Returns the humanoid parent name for a given humanoid name, or empty
	// string for the root or unrecognized names.
	FString GetHumanoidParentName(const FString& HumanoidName)
	{
		const TMap<FString, FString>& ParentMap = GetHumanoidParentMap();
		for (const auto& Pair : ParentMap)
		{
			if (Pair.Key.Compare(HumanoidName, ESearchCase::IgnoreCase) == 0)
			{
				return Pair.Value;
			}
		}
		return FString();
	}

	// Derive a local-space (humanoid-parent-relative) transform from a
	// humanoid-component-space map. Bones with no humanoid parent return
	// their component-space transform verbatim.
	FTransform DeriveLocalFromComponentMap(const FString& HumanoidName, const FHumanoidComponentMap& ComponentMap)
	{
		const FTransform* Self = ComponentMap.Find(HumanoidName);
		if (Self == nullptr)
		{
			return FTransform::Identity;
		}

		const FString ParentName = GetHumanoidParentName(HumanoidName);
		if (ParentName.IsEmpty())
		{
			return *Self;
		}

		const FTransform* Parent = ComponentMap.Find(ParentName);
		if (Parent == nullptr)
		{
			return *Self;
		}

		return Self->GetRelativeTransform(*Parent);
	}
}

// Thin-forwarder guard: resolve the mesh's VMC node, or return false.
#define VMC_RESOLVE_NODE_OR_RETURN_FALSE(NodeVar, Comp) \
	FAnimNode_VrmVMC* NodeVar = ResolveNode(Comp);       \
	if ((NodeVar) == nullptr)                            \
	{                                                    \
		return false;                                    \
	}

// =============================================================================
// Mapping inspection
// =============================================================================

TArray<FName> UVrmVMCBlueprintLibrary::GetMappedHumanoidNames(USkeletalMeshComponent* SkelMeshComp)
{
	TArray<FName> Result;

	FAnimNode_VrmVMC* Node = ResolveNode(SkelMeshComp);
	if (Node == nullptr || !Node->VrmMetaObject_Internal.IsValid())
	{
		return Result;
	}

	const auto* Meta = Node->VrmMetaObject_Internal.Get();
	if (Meta == nullptr)
	{
		return Result;
	}

	Result.Reserve(Meta->humanoidBoneTable.Num());
	for (const auto& Pair : Meta->humanoidBoneTable)
	{
		Result.Add(*Pair.Key);
	}
	return Result;
}

bool UVrmVMCBlueprintLibrary::GetMappedBoneName(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName,
                                                FName& OutBoneName)
{
	FAnimNode_VrmVMC* Node = ResolveNode(SkelMeshComp);
	const TPair<FString, FString>* Entry = FindHumanoidEntry(Node, HumanoidName);
	if (Entry == nullptr)
	{
		return false;
	}
	OutBoneName = *Entry->Value;
	return true;
}

bool UVrmVMCBlueprintLibrary::GetMappedBoneIndex(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName,
                                                 int32& OutBoneIndex)
{
	FAnimNode_VrmVMC* Node = ResolveNode(SkelMeshComp);
	const int32 Index = ResolveBoneIndex(SkelMeshComp, Node, HumanoidName);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	OutBoneIndex = Index;
	return true;
}

// =============================================================================
// VMC source: last applied by the anim node
// =============================================================================

bool UVrmVMCBlueprintLibrary::GetVMCBoneTransformComponent(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName,
                                                           FTransform& OutTransform)
{
	VMC_RESOLVE_NODE_OR_RETURN_FALSE(Node, SkelMeshComp);
	return Node->TryGetLastAppliedTransform(HumanoidName.ToString(), OutTransform);
}

bool UVrmVMCBlueprintLibrary::GetVMCBoneTransformLocal(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName,
                                                       FTransform& OutTransform)
{
	VMC_RESOLVE_NODE_OR_RETURN_FALSE(Node, SkelMeshComp);

	FTransform Self;
	if (!Node->TryGetLastAppliedTransform(HumanoidName.ToString(), Self))
	{
		return false;
	}

	const FString ParentName = GetHumanoidParentName(HumanoidName.ToString());
	if (ParentName.IsEmpty())
	{
		OutTransform = Self;
		return true;
	}

	FTransform Parent;
	if (!Node->TryGetLastAppliedTransform(ParentName, Parent))
	{
		// Parent not yet cached this frame; fall back to component-space.
		OutTransform = Self;
		return true;
	}

	OutTransform = Self.GetRelativeTransform(Parent);
	return true;
}

bool UVrmVMCBlueprintLibrary::GetVMCBoneTransformWorld(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName,
                                                       FTransform& OutTransform)
{
	VMC_RESOLVE_NODE_OR_RETURN_FALSE(Node, SkelMeshComp);

	FTransform ComponentTransform;
	if (!Node->TryGetLastAppliedTransform(HumanoidName.ToString(), ComponentTransform))
	{
		return false;
	}

	OutTransform = ComposeWorldFromComponent(SkelMeshComp, ComponentTransform);
	return true;
}

// =============================================================================
// Final source: current composited bone transform on the skel mesh comp
// =============================================================================

bool UVrmVMCBlueprintLibrary::GetFinalBoneTransformComponent(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName,
                                                             FTransform& OutTransform)
{
	FAnimNode_VrmVMC* Node = ResolveNode(SkelMeshComp);
	const int32 Index = ResolveBoneIndex(SkelMeshComp, Node, HumanoidName);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	OutTransform = SkelMeshComp->GetBoneTransform(Index, FTransform::Identity);
	return true;
}

bool UVrmVMCBlueprintLibrary::GetFinalBoneTransformLocal(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName,
                                                         FTransform& OutTransform)
{
	FAnimNode_VrmVMC* Node = ResolveNode(SkelMeshComp);
	const TPair<FString, FString>* Entry = FindHumanoidEntry(Node, HumanoidName);
	if (Entry == nullptr || SkelMeshComp == nullptr)
	{
		return false;
	}

	const FName BoneName(*Entry->Value);
	const int32 BoneIndex = SkelMeshComp->GetBoneIndex(BoneName);
	if (BoneIndex == INDEX_NONE)
	{
		return false;
	}

	const FTransform Self = SkelMeshComp->GetBoneTransform(BoneIndex, FTransform::Identity);

	USkeletalMesh* Mesh = SkelMeshComp->GetSkeletalMeshAsset();
	if (Mesh == nullptr || Mesh->GetSkeleton() == nullptr)
	{
		return false;
	}

	const FReferenceSkeleton& RefSkeleton = Mesh->GetSkeleton()->GetReferenceSkeleton();
	const int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
	if (ParentIndex == INDEX_NONE)
	{
		OutTransform = Self;
		return true;
	}

	const FTransform Parent = SkelMeshComp->GetBoneTransform(ParentIndex, FTransform::Identity);
	OutTransform = Self.GetRelativeTransform(Parent);
	return true;
}

bool UVrmVMCBlueprintLibrary::GetFinalBoneTransformWorld(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName,
                                                         FTransform& OutTransform)
{
	FAnimNode_VrmVMC* Node = ResolveNode(SkelMeshComp);
	const TPair<FString, FString>* Entry = FindHumanoidEntry(Node, HumanoidName);
	if (Entry == nullptr || SkelMeshComp == nullptr)
	{
		return false;
	}

	const FName BoneName(*Entry->Value);
	const int32 BoneIndex = SkelMeshComp->GetBoneIndex(BoneName);
	if (BoneIndex == INDEX_NONE)
	{
		return false;
	}

	OutTransform = SkelMeshComp->GetBoneTransform(BoneIndex);
	return true;
}

// =============================================================================
// Ref source: bind pose / reference skeleton
// =============================================================================

bool UVrmVMCBlueprintLibrary::GetRefBoneTransformComponent(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName,
                                                           FTransform& OutTransform)
{
	FAnimNode_VrmVMC* Node = ResolveNode(SkelMeshComp);
	const int32 Index = ResolveBoneIndex(SkelMeshComp, Node, HumanoidName);
	if (Index == INDEX_NONE || Node == nullptr)
	{
		return false;
	}

	FScopeLock Lock(Node->BoneStateLock.Get());
	if (!Node->RefSkeletonTransform_global.IsValidIndex(Index))
	{
		return false;
	}
	OutTransform = Node->RefSkeletonTransform_global[Index];
	return true;
}

bool UVrmVMCBlueprintLibrary::GetRefBoneTransformLocal(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName,
                                                       FTransform& OutTransform)
{
	FAnimNode_VrmVMC* Node = ResolveNode(SkelMeshComp);
	const TPair<FString, FString>* Entry = FindHumanoidEntry(Node, HumanoidName);
	if (Entry == nullptr || SkelMeshComp == nullptr || Node == nullptr)
	{
		return false;
	}

	USkeletalMesh* Mesh = SkelMeshComp->GetSkeletalMeshAsset();
	if (Mesh == nullptr || Mesh->GetSkeleton() == nullptr)
	{
		return false;
	}

	const FReferenceSkeleton& RefSkeleton = Mesh->GetSkeleton()->GetReferenceSkeleton();
	const int32 BoneIndex = RefSkeleton.FindBoneIndex(*Entry->Value);
	if (BoneIndex == INDEX_NONE)
	{
		return false;
	}

	const int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);

	FScopeLock Lock(Node->BoneStateLock.Get());
	if (!Node->RefSkeletonTransform_global.IsValidIndex(BoneIndex))
	{
		return false;
	}

	const FTransform Self = Node->RefSkeletonTransform_global[BoneIndex];
	if (ParentIndex == INDEX_NONE || !Node->RefSkeletonTransform_global.IsValidIndex(ParentIndex))
	{
		OutTransform = Self;
		return true;
	}

	const FTransform Parent = Node->RefSkeletonTransform_global[ParentIndex];
	OutTransform = Self.GetRelativeTransform(Parent);
	return true;
}

bool UVrmVMCBlueprintLibrary::GetRefBoneTransformWorld(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName,
                                                       FTransform& OutTransform)
{
	FTransform ComponentTransform;
	if (!GetRefBoneTransformComponent(SkelMeshComp, HumanoidName, ComponentTransform))
	{
		return false;
	}
	OutTransform = ComposeWorldFromComponent(SkelMeshComp, ComponentTransform);
	return true;
}

// =============================================================================
// Pre-split convenience (Final + Component)
// =============================================================================

bool UVrmVMCBlueprintLibrary::GetFinalBoneLocationComponent(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName,
                                                            FVector& OutLocation)
{
	FTransform T;
	if (!GetFinalBoneTransformComponent(SkelMeshComp, HumanoidName, T))
	{
		return false;
	}
	OutLocation = T.GetLocation();
	return true;
}

bool UVrmVMCBlueprintLibrary::GetFinalBoneRotationComponent(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName,
                                                            FQuat& OutRotation)
{
	FTransform T;
	if (!GetFinalBoneTransformComponent(SkelMeshComp, HumanoidName, T))
	{
		return false;
	}
	OutRotation = T.GetRotation();
	return true;
}

bool UVrmVMCBlueprintLibrary::GetFinalBoneScaleComponent(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName,
                                                         FVector& OutScale)
{
	FTransform T;
	if (!GetFinalBoneTransformComponent(SkelMeshComp, HumanoidName, T))
	{
		return false;
	}
	OutScale = T.GetScale3D();
	return true;
}

// =============================================================================
// Pose snapshot
// =============================================================================

bool UVrmVMCBlueprintLibrary::GetVMCPoseSnapshot(USkeletalMeshComponent* SkelMeshComp, FVrmVMCPoseSnapshot& OutSnapshot)
{
	FAnimNode_VrmVMC* Node = ResolveNode(SkelMeshComp);
	if (Node == nullptr || !Node->VrmMetaObject_Internal.IsValid())
	{
		return false;
	}
	const auto* Meta = Node->VrmMetaObject_Internal.Get();
	if (Meta == nullptr)
	{
		return false;
	}

	TArray<FName> Names;
	FHumanoidComponentMap ComponentMap;
	Names.Reserve(Meta->humanoidBoneTable.Num());
	ComponentMap.Reserve(Meta->humanoidBoneTable.Num());

	// Hold the lock for one tight pass over the bone table, copying each
	// bone's component-space cache value. Derivation of local-space happens
	// outside the lock from the copied map.
	{
		// Inline the cache lookup under the single lock. Calling the public
		// TryGetLastAppliedTransform here would re-enter BoneStateLock (it locks
		// internally); read BoneStates directly (this class is a friend of the
		// node) to keep one non-recursive lock acquisition.
		FScopeLock Lock(Node->BoneStateLock.Get());
		for (const auto& Pair : Meta->humanoidBoneTable)
		{
			if (const FOWLVMCPerBoneState* State = Node->BoneStates.Find(Pair.Key))
			{
				if (State->bHasLastApplied)
				{
					Names.Add(*Pair.Key);
					ComponentMap.Add(Pair.Key, State->LastAppliedTransform);
				}
			}
		}
	}

	if (Names.Num() == 0)
	{
		return false;
	}

	OutSnapshot.HumanoidNames = MoveTemp(Names);
	OutSnapshot.ComponentTransforms.Reserve(OutSnapshot.HumanoidNames.Num());
	OutSnapshot.LocalTransforms.Reserve(OutSnapshot.HumanoidNames.Num());

	for (const FName& Name : OutSnapshot.HumanoidNames)
	{
		const FString Key = Name.ToString();
		const FTransform* Comp = ComponentMap.Find(Key);
		OutSnapshot.ComponentTransforms.Add(Comp != nullptr ? *Comp : FTransform::Identity);
		OutSnapshot.LocalTransforms.Add(DeriveLocalFromComponentMap(Key, ComponentMap));
	}

	OutSnapshot.CaptureTimeSeconds = static_cast<float>(FPlatformTime::Seconds());
	OutSnapshot.SourceSkelMeshCompName = SkelMeshComp->GetFName();
	return true;
}

bool UVrmVMCBlueprintLibrary::GetBoneFromSnapshot(const FVrmVMCPoseSnapshot& Snapshot, FName HumanoidName,
                                                  FTransform& OutLocalTransform, FTransform& OutComponentTransform)
{
	const int32 Index = Snapshot.HumanoidNames.IndexOfByPredicate([&HumanoidName](const FName& N)
	{
		return N.IsEqual(HumanoidName, ENameCase::IgnoreCase);
	});
	if (Index == INDEX_NONE)
	{
		return false;
	}
	if (!Snapshot.LocalTransforms.IsValidIndex(Index) || !Snapshot.ComponentTransforms.IsValidIndex(Index))
	{
		return false;
	}
	OutLocalTransform = Snapshot.LocalTransforms[Index];
	OutComponentTransform = Snapshot.ComponentTransforms[Index];
	return true;
}

TArray<FName> UVrmVMCBlueprintLibrary::GetSnapshotHumanoidNames(const FVrmVMCPoseSnapshot& Snapshot)
{
	return Snapshot.HumanoidNames;
}

bool UVrmVMCBlueprintLibrary::CompareSnapshots(const FVrmVMCPoseSnapshot& A, const FVrmVMCPoseSnapshot& B,
                                               TArray<FName>& OutDifferingBones, float AngleThresholdDegrees)
{
	OutDifferingBones.Reset();

	const float ThresholdRadians = FMath::DegreesToRadians(FMath::Max(0.f, AngleThresholdDegrees));
	const float CosHalfThreshold = FMath::Cos(ThresholdRadians * 0.5f);

	for (int32 i = 0; i < A.HumanoidNames.Num(); ++i)
	{
		const FName& Name = A.HumanoidNames[i];
		const int32 BIndex = B.HumanoidNames.IndexOfByPredicate([&Name](const FName& N)
		{
			return N.IsEqual(Name, ENameCase::IgnoreCase);
		});
		if (BIndex == INDEX_NONE)
		{
			OutDifferingBones.Add(Name);
			continue;
		}

		if (!A.ComponentTransforms.IsValidIndex(i) || !B.ComponentTransforms.IsValidIndex(BIndex))
		{
			OutDifferingBones.Add(Name);
			continue;
		}

		const FQuat QA = A.ComponentTransforms[i].GetRotation();
		const FQuat QB = B.ComponentTransforms[BIndex].GetRotation();
		const float Dot = FMath::Abs(QA | QB);
		if (Dot < CosHalfThreshold)
		{
			OutDifferingBones.Add(Name);
		}
	}

	// Bones present only in B are also differences.
	for (int32 i = 0; i < B.HumanoidNames.Num(); ++i)
	{
		const FName& Name = B.HumanoidNames[i];
		const bool bInA = A.HumanoidNames.ContainsByPredicate([&Name](const FName& N)
		{
			return N.IsEqual(Name, ENameCase::IgnoreCase);
		});
		if (!bInA)
		{
			OutDifferingBones.AddUnique(Name);
		}
	}

	return OutDifferingBones.Num() > 0;
}

bool UVrmVMCBlueprintLibrary::IsSnapshotValid(const FVrmVMCPoseSnapshot& Snapshot)
{
	if (Snapshot.HumanoidNames.Num() == 0)
	{
		return false;
	}
	if (Snapshot.LocalTransforms.Num() != Snapshot.HumanoidNames.Num())
	{
		return false;
	}
	if (Snapshot.ComponentTransforms.Num() != Snapshot.HumanoidNames.Num())
	{
		return false;
	}
	return true;
}

// =============================================================================
// Pre-rebase overrides
// =============================================================================

bool UVrmVMCBlueprintLibrary::SetPreRebaseRotation(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName,
                                                   FQuat Rotation)
{
	VMC_RESOLVE_NODE_OR_RETURN_FALSE(Node, SkelMeshComp);
	Node->SetPreRebaseRotation(HumanoidName.ToString(), Rotation);
	return true;
}

bool UVrmVMCBlueprintLibrary::ClearPreRebaseRotation(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName)
{
	VMC_RESOLVE_NODE_OR_RETURN_FALSE(Node, SkelMeshComp);
	Node->ClearPreRebaseRotation(HumanoidName.ToString());
	return true;
}

bool UVrmVMCBlueprintLibrary::ClearAllPreRebaseOverrides(USkeletalMeshComponent* SkelMeshComp)
{
	VMC_RESOLVE_NODE_OR_RETURN_FALSE(Node, SkelMeshComp);
	Node->ClearAllPreRebaseOverrides();
	return true;
}

// =============================================================================
// Post-rebase overrides
// =============================================================================

bool UVrmVMCBlueprintLibrary::SetPostRebaseRotation(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName,
                                                    FQuat Rotation)
{
	VMC_RESOLVE_NODE_OR_RETURN_FALSE(Node, SkelMeshComp);
	Node->SetPostRebaseRotation(HumanoidName.ToString(), Rotation);
	return true;
}

bool UVrmVMCBlueprintLibrary::ClearPostRebaseRotation(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName)
{
	VMC_RESOLVE_NODE_OR_RETURN_FALSE(Node, SkelMeshComp);
	Node->ClearPostRebaseRotation(HumanoidName.ToString());
	return true;
}

bool UVrmVMCBlueprintLibrary::ClearAllPostRebaseOverrides(USkeletalMeshComponent* SkelMeshComp)
{
	VMC_RESOLVE_NODE_OR_RETURN_FALSE(Node, SkelMeshComp);
	Node->ClearAllPostRebaseOverrides();
	return true;
}

// =============================================================================
// Bone masking
// =============================================================================

bool UVrmVMCBlueprintLibrary::SetBoneMasked(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName, bool bMasked)
{
	VMC_RESOLVE_NODE_OR_RETURN_FALSE(Node, SkelMeshComp);
	Node->SetBoneMasked(HumanoidName.ToString(), bMasked);
	return true;
}

bool UVrmVMCBlueprintLibrary::IsBoneMasked(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName)
{
	VMC_RESOLVE_NODE_OR_RETURN_FALSE(Node, SkelMeshComp);
	return Node->IsBoneMasked(HumanoidName.ToString());
}

bool UVrmVMCBlueprintLibrary::ClearAllMasks(USkeletalMeshComponent* SkelMeshComp)
{
	VMC_RESOLVE_NODE_OR_RETURN_FALSE(Node, SkelMeshComp);
	Node->ClearAllMasks();
	return true;
}

// =============================================================================
// Bone state introspection
// =============================================================================

bool UVrmVMCBlueprintLibrary::IsPreRebaseOverridden(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName)
{
	VMC_RESOLVE_NODE_OR_RETURN_FALSE(Node, SkelMeshComp);
	return Node->IsPreRebaseOverridden(HumanoidName.ToString());
}

bool UVrmVMCBlueprintLibrary::IsPostRebaseOverridden(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName)
{
	VMC_RESOLVE_NODE_OR_RETURN_FALSE(Node, SkelMeshComp);
	return Node->IsPostRebaseOverridden(HumanoidName.ToString());
}

bool UVrmVMCBlueprintLibrary::GetPreRebaseRotation(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName,
                                                   FQuat& OutRotation)
{
	VMC_RESOLVE_NODE_OR_RETURN_FALSE(Node, SkelMeshComp);
	return Node->TryGetPreRebaseRotation(HumanoidName.ToString(), OutRotation);
}

bool UVrmVMCBlueprintLibrary::GetPostRebaseRotation(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName,
                                                    FQuat& OutRotation)
{
	VMC_RESOLVE_NODE_OR_RETURN_FALSE(Node, SkelMeshComp);
	return Node->TryGetPostRebaseRotation(HumanoidName.ToString(), OutRotation);
}

bool UVrmVMCBlueprintLibrary::ClearBoneState(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName)
{
	VMC_RESOLVE_NODE_OR_RETURN_FALSE(Node, SkelMeshComp);
	Node->ClearBoneState(HumanoidName.ToString());
	return true;
}

// =============================================================================
// Curve overrides
// =============================================================================

bool UVrmVMCBlueprintLibrary::GetLastAppliedCurveValue(USkeletalMeshComponent* SkelMeshComp, FName CurveName,
                                                       float& OutValue)
{
	VMC_RESOLVE_NODE_OR_RETURN_FALSE(Node, SkelMeshComp);
	return Node->TryGetLastAppliedCurveValue(CurveName.ToString(), OutValue);
}

bool UVrmVMCBlueprintLibrary::SetCurveOverride(USkeletalMeshComponent* SkelMeshComp, FName CurveName, float Value)
{
	VMC_RESOLVE_NODE_OR_RETURN_FALSE(Node, SkelMeshComp);
	Node->SetCurveOverride(CurveName.ToString(), Value);
	return true;
}

bool UVrmVMCBlueprintLibrary::ClearCurveOverride(USkeletalMeshComponent* SkelMeshComp, FName CurveName)
{
	VMC_RESOLVE_NODE_OR_RETURN_FALSE(Node, SkelMeshComp);
	Node->ClearCurveOverride(CurveName.ToString());
	return true;
}

bool UVrmVMCBlueprintLibrary::ClearAllCurveOverrides(USkeletalMeshComponent* SkelMeshComp)
{
	VMC_RESOLVE_NODE_OR_RETURN_FALSE(Node, SkelMeshComp);
	Node->ClearAllCurveOverrides();
	return true;
}

bool UVrmVMCBlueprintLibrary::SetCurveMasked(USkeletalMeshComponent* SkelMeshComp, FName CurveName, bool bMasked)
{
	VMC_RESOLVE_NODE_OR_RETURN_FALSE(Node, SkelMeshComp);
	Node->SetCurveMasked(CurveName.ToString(), bMasked);
	return true;
}

bool UVrmVMCBlueprintLibrary::IsCurveMasked(USkeletalMeshComponent* SkelMeshComp, FName CurveName)
{
	VMC_RESOLVE_NODE_OR_RETURN_FALSE(Node, SkelMeshComp);
	return Node->IsCurveMasked(CurveName.ToString());
}

bool UVrmVMCBlueprintLibrary::ClearAllCurveMasks(USkeletalMeshComponent* SkelMeshComp)
{
	VMC_RESOLVE_NODE_OR_RETURN_FALSE(Node, SkelMeshComp);
	Node->ClearAllCurveMasks();
	return true;
}

bool UVrmVMCBlueprintLibrary::IsCurveOverridden(USkeletalMeshComponent* SkelMeshComp, FName CurveName)
{
	VMC_RESOLVE_NODE_OR_RETURN_FALSE(Node, SkelMeshComp);
	return Node->IsCurveOverridden(CurveName.ToString());
}

// =============================================================================
// Multi-server inspection
// =============================================================================

bool UVrmVMCBlueprintLibrary::GetRigServer(USkeletalMeshComponent* SkelMeshComp, FString& OutAddress, int32& OutPort)
{
	VMC_RESOLVE_NODE_OR_RETURN_FALSE(Node, SkelMeshComp);
	OutAddress = Node->ServerAddress;
	OutPort = Node->Port;
	return true;
}

// =============================================================================
// Diagnostics
// =============================================================================

bool UVrmVMCBlueprintLibrary::CompareRigPoseDirections(USkeletalMeshComponent* TargetMesh,
                                                       USkeletalMeshComponent* ReferenceMesh,
                                                       TMap<FName, float>& OutPerSegmentErrorDeg,
                                                       FName& OutWorstSegment,
                                                       float& OutWorstErrorDeg,
                                                       float& OutAverageErrorDeg)
{
	OutPerSegmentErrorDeg.Reset();
	OutWorstSegment = NAME_None;
	OutWorstErrorDeg = 0.0f;
	OutAverageErrorDeg = 0.0f;

	if (TargetMesh == nullptr || ReferenceMesh == nullptr)
	{
		return false;
	}

	// Humanoid limb segments (parent -> child). The error is the angle between
	// the parent->child DIRECTION on the two rigs, in component space. Direction
	// is independent of per-rig bind/axis convention, so this isolates the
	// visible limb misalignment from constant bind differences. Keyed by parent.
	static const TCHAR* const Segments[][2] = {
		{ TEXT("hips"),          TEXT("spine") },
		{ TEXT("spine"),         TEXT("chest") },
		{ TEXT("chest"),         TEXT("neck") },
		{ TEXT("neck"),          TEXT("head") },
		{ TEXT("leftShoulder"),  TEXT("leftUpperArm") },
		{ TEXT("leftUpperArm"),  TEXT("leftLowerArm") },
		{ TEXT("leftLowerArm"),  TEXT("leftHand") },
		{ TEXT("rightShoulder"), TEXT("rightUpperArm") },
		{ TEXT("rightUpperArm"), TEXT("rightLowerArm") },
		{ TEXT("rightLowerArm"), TEXT("rightHand") },
		{ TEXT("leftUpperLeg"),  TEXT("leftLowerLeg") },
		{ TEXT("leftLowerLeg"),  TEXT("leftFoot") },
		{ TEXT("leftFoot"),      TEXT("leftToes") },
		{ TEXT("rightUpperLeg"), TEXT("rightLowerLeg") },
		{ TEXT("rightLowerLeg"), TEXT("rightFoot") },
		{ TEXT("rightFoot"),     TEXT("rightToes") },
	};

	// Normalize out each rig's global frame so the metric is body-relative.
	// Different skeletons author their component-space orientation differently,
	// which would add a constant baseline to every segment (legs reading high
	// while visually fine). Build a convention-free body frame from geometric
	// landmarks (up = hips->head, right = left->right hip, forward = up x right)
	// and express each segment direction in it. The frame is built identically
	// for both rigs, so its handedness cancels in the comparison.
	auto BuildBodyFrame = [](USkeletalMeshComponent* Mesh) -> FMatrix
	{
		FVector Hips, Head, LHip, RHip;
		if (!UVrmVMCBlueprintLibrary::GetFinalBoneLocationComponent(Mesh, FName(TEXT("hips")), Hips) ||
			!UVrmVMCBlueprintLibrary::GetFinalBoneLocationComponent(Mesh, FName(TEXT("head")), Head) ||
			!UVrmVMCBlueprintLibrary::GetFinalBoneLocationComponent(Mesh, FName(TEXT("leftUpperLeg")), LHip) ||
			!UVrmVMCBlueprintLibrary::GetFinalBoneLocationComponent(Mesh, FName(TEXT("rightUpperLeg")), RHip))
		{
			return FMatrix::Identity;
		}
		const FVector Up = (Head - Hips).GetSafeNormal();
		FVector Right = (RHip - LHip).GetSafeNormal();
		if (Up.IsNearlyZero() || Right.IsNearlyZero()) return FMatrix::Identity;
		const FVector Fwd = FVector::CrossProduct(Up, Right).GetSafeNormal();
		if (Fwd.IsNearlyZero()) return FMatrix::Identity;
		Right = FVector::CrossProduct(Fwd, Up).GetSafeNormal(); // re-orthogonalize
		return FMatrix(Right, Up, Fwd, FVector::ZeroVector);
	};
	const FMatrix FrameT = BuildBodyFrame(TargetMesh);
	const FMatrix FrameR = BuildBodyFrame(ReferenceMesh);

	float Sum = 0.0f;
	int32 Count = 0;
	for (const TCHAR* const* Seg : Segments)
	{
		const FName Parent(Seg[0]);
		const FName Child(Seg[1]);

		FVector PT, CT, PR, CR;
		if (!GetFinalBoneLocationComponent(TargetMesh, Parent, PT)) continue;
		if (!GetFinalBoneLocationComponent(TargetMesh, Child, CT)) continue;
		if (!GetFinalBoneLocationComponent(ReferenceMesh, Parent, PR)) continue;
		if (!GetFinalBoneLocationComponent(ReferenceMesh, Child, CR)) continue;

		const FVector DirT = FrameT.InverseTransformVector((CT - PT).GetSafeNormal()).GetSafeNormal();
		const FVector DirR = FrameR.InverseTransformVector((CR - PR).GetSafeNormal()).GetSafeNormal();
		if (DirT.IsNearlyZero() || DirR.IsNearlyZero()) continue;

		const float Dot = FMath::Clamp(static_cast<float>(FVector::DotProduct(DirT, DirR)), -1.0f, 1.0f);
		const float ErrDeg = FMath::RadiansToDegrees(FMath::Acos(Dot));

		OutPerSegmentErrorDeg.Add(Parent, ErrDeg);
		Sum += ErrDeg;
		++Count;
		if (ErrDeg > OutWorstErrorDeg)
		{
			OutWorstErrorDeg = ErrDeg;
			OutWorstSegment = Parent;
		}
	}

	if (Count == 0)
	{
		return false;
	}
	OutAverageErrorDeg = Sum / static_cast<float>(Count);
	return true;
}

// =============================================================================
// Editor metadata
// =============================================================================

TArray<FString> UVrmVMCBlueprintLibrary::GetHumanoidNameOptions()
{
	// Canonical VMC humanoid name list. Order matches the VRM 1.0 humanoid
	// bone enumeration.
	static const TArray<FString> Options = {
		TEXT("hips"), TEXT("spine"), TEXT("chest"), TEXT("upperChest"), TEXT("neck"), TEXT("head"),
		TEXT("leftShoulder"), TEXT("leftUpperArm"), TEXT("leftLowerArm"), TEXT("leftHand"),
		TEXT("rightShoulder"), TEXT("rightUpperArm"), TEXT("rightLowerArm"), TEXT("rightHand"),
		TEXT("leftUpperLeg"), TEXT("leftLowerLeg"), TEXT("leftFoot"), TEXT("leftToes"),
		TEXT("rightUpperLeg"), TEXT("rightLowerLeg"), TEXT("rightFoot"), TEXT("rightToes"),
		TEXT("leftEye"), TEXT("rightEye"), TEXT("jaw"),
		TEXT("leftThumbProximal"), TEXT("leftThumbIntermediate"), TEXT("leftThumbDistal"),
		TEXT("leftIndexProximal"), TEXT("leftIndexIntermediate"), TEXT("leftIndexDistal"),
		TEXT("leftMiddleProximal"), TEXT("leftMiddleIntermediate"), TEXT("leftMiddleDistal"),
		TEXT("leftRingProximal"), TEXT("leftRingIntermediate"), TEXT("leftRingDistal"),
		TEXT("leftLittleProximal"), TEXT("leftLittleIntermediate"), TEXT("leftLittleDistal"),
		TEXT("rightThumbProximal"), TEXT("rightThumbIntermediate"), TEXT("rightThumbDistal"),
		TEXT("rightIndexProximal"), TEXT("rightIndexIntermediate"), TEXT("rightIndexDistal"),
		TEXT("rightMiddleProximal"), TEXT("rightMiddleIntermediate"), TEXT("rightMiddleDistal"),
		TEXT("rightRingProximal"), TEXT("rightRingIntermediate"), TEXT("rightRingDistal"),
		TEXT("rightLittleProximal"), TEXT("rightLittleIntermediate"), TEXT("rightLittleDistal"),
	};
	return Options;
}

#undef VMC_RESOLVE_NODE_OR_RETURN_FALSE
