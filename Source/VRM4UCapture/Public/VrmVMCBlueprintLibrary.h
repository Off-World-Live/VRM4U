#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "OWLVrmVMCPoseSnapshot.h"

#include "VrmVMCBlueprintLibrary.generated.h"

class USkeletalMeshComponent;

/**
 * Blueprint function library for inspecting per-rig VMC state on a skeletal
 * mesh component running the VRM VMC anim node.
 *
 * All functions resolve the mesh component to its active anim node through
 * the module-internal registry, then call thread-safe accessor methods on the
 * node. Functions return false and leave the out parameter unchanged when the
 * mesh has no active VMC node or no data is available for the requested bone.
 *
 * Humanoid names use the VMC protocol casing (e.g. "leftUpperArm"). FName is
 * accepted at the BP boundary and converted to FString once for lookup.
 *
 * Transform getter naming follows three orthogonal axes:
 *   Source: VMC (last applied by the anim node), Final (current skel mesh
 *           comp state), Ref (bind pose).
 *   Space: Local (vs parent humanoid bone), Component (vs skel mesh comp),
 *          World (vs world origin).
 *   Decomp: Full FTransform, or pre-split Location / Rotation / Scale
 *           (Final + Component only, as a convenience).
 *
 * World-space transforms use the actor transform sampled at the moment of
 * the BP call, not at anim eval time. If the actor moved between eval and
 * read, the world-space result reflects the new actor position with the
 * eval-time component-space bone.
 */
UCLASS()
class VRM4UCAPTURE_API UVrmVMCBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// -- Mapping inspection --

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC")
	static TArray<FName> GetMappedHumanoidNames(USkeletalMeshComponent* SkelMeshComp);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC", meta = (GetOptions = "GetHumanoidNameOptions"))
	static bool GetMappedBoneName(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName, FName& OutBoneName);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC", meta = (GetOptions = "GetHumanoidNameOptions"))
	static bool GetMappedBoneIndex(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName, int32& OutBoneIndex);

	// -- VMC source: last applied by the anim node --
	// Reads the per-bone cache updated each evaluation. The Component variant
	// reads the cache directly. The Local variant composes with the parent
	// humanoid bone's cached component-space transform. The World variant
	// composes the component-space cache with the current actor transform.

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC|Pose",
		meta = (GetOptions = "GetHumanoidNameOptions"))
	static bool GetVMCBoneTransformLocal(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName,
	                                     FTransform& OutTransform);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC|Pose",
		meta = (GetOptions = "GetHumanoidNameOptions"))
	static bool GetVMCBoneTransformComponent(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName,
	                                         FTransform& OutTransform);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC|Pose",
		meta = (GetOptions = "GetHumanoidNameOptions"))
	static bool GetVMCBoneTransformWorld(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName,
	                                     FTransform& OutTransform);

	// -- Final source: current composited bone transform on the skel mesh comp --
	// Reads through USkeletalMeshComponent::GetBoneTransform. Reflects the
	// pose visible on the rig right now, including any post-VMC anim nodes
	// that ran after the VMC node in the graph.

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC|Pose",
		meta = (GetOptions = "GetHumanoidNameOptions"))
	static bool GetFinalBoneTransformLocal(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName,
	                                       FTransform& OutTransform);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC|Pose",
		meta = (GetOptions = "GetHumanoidNameOptions"))
	static bool GetFinalBoneTransformComponent(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName,
	                                           FTransform& OutTransform);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC|Pose",
		meta = (GetOptions = "GetHumanoidNameOptions"))
	static bool GetFinalBoneTransformWorld(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName,
	                                       FTransform& OutTransform);

	// -- Ref source: bind pose / reference skeleton --
	// Reads from RefSkeletonTransform_global on the anim node. Useful for
	// verifying retarget math by comparing VMC and Ref values for a bone.

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC|Pose",
		meta = (GetOptions = "GetHumanoidNameOptions"))
	static bool GetRefBoneTransformLocal(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName,
	                                     FTransform& OutTransform);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC|Pose",
		meta = (GetOptions = "GetHumanoidNameOptions"))
	static bool GetRefBoneTransformComponent(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName,
	                                         FTransform& OutTransform);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC|Pose",
		meta = (GetOptions = "GetHumanoidNameOptions"))
	static bool GetRefBoneTransformWorld(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName,
	                                     FTransform& OutTransform);

	// -- Pre-split convenience --
	// Saves Split Struct Pin gymnastics on the most common BP query.
	// Only Final + Component is pre-split; other source/space combinations
	// use the full FTransform getter plus Split Struct Pin downstream.

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC|Pose",
		meta = (GetOptions = "GetHumanoidNameOptions"))
	static bool GetFinalBoneLocationComponent(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName,
	                                          FVector& OutLocation);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC|Pose",
		meta = (GetOptions = "GetHumanoidNameOptions"))
	static bool GetFinalBoneRotationComponent(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName,
	                                          FQuat& OutRotation);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC|Pose",
		meta = (GetOptions = "GetHumanoidNameOptions"))
	static bool GetFinalBoneScaleComponent(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName, FVector& OutScale);

	// -- Pose snapshot --
	// Capture / query / diff / validate a humanoid pose. Snapshot is a value
	// type and may be stored, copied, and compared freely in BP. Scope is
	// humanoid-mapped bones only.

	UFUNCTION(BlueprintCallable, Category = "VRM4U|VMC|Snapshot")
	static bool GetVMCPoseSnapshot(USkeletalMeshComponent* SkelMeshComp, FVrmVMCPoseSnapshot& OutSnapshot);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC|Snapshot",
		meta = (GetOptions = "GetHumanoidNameOptions"))
	static bool GetBoneFromSnapshot(const FVrmVMCPoseSnapshot& Snapshot, FName HumanoidName,
	                                FTransform& OutLocalTransform, FTransform& OutComponentTransform);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC|Snapshot")
	static TArray<FName> GetSnapshotHumanoidNames(const FVrmVMCPoseSnapshot& Snapshot);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC|Snapshot")
	static bool CompareSnapshots(const FVrmVMCPoseSnapshot& A, const FVrmVMCPoseSnapshot& B,
	                             TArray<FName>& OutDifferingBones, float AngleThresholdDegrees = 0.5f);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC|Snapshot")
	static bool IsSnapshotValid(const FVrmVMCPoseSnapshot& Snapshot);

	// -- Pre-rebase overrides --
	// Substitutes the source-space VMC rotation before the rebase formula runs.
	// Useful for forcing a bone's orientation in the universal humanoid frame.

	UFUNCTION(BlueprintCallable, Category = "VRM4U|VMC", meta = (GetOptions = "GetHumanoidNameOptions"))
	static bool SetPreRebaseRotation(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName, FQuat Rotation);

	UFUNCTION(BlueprintCallable, Category = "VRM4U|VMC", meta = (GetOptions = "GetHumanoidNameOptions"))
	static bool ClearPreRebaseRotation(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName);

	UFUNCTION(BlueprintCallable, Category = "VRM4U|VMC")
	static bool ClearAllPreRebaseOverrides(USkeletalMeshComponent* SkelMeshComp);

	// -- Post-rebase overrides --
	// Replaces the final target-space rotation, bypassing the rebase formula.
	// When set alongside a pre-rebase override, post-rebase wins.

	UFUNCTION(BlueprintCallable, Category = "VRM4U|VMC", meta = (GetOptions = "GetHumanoidNameOptions"))
	static bool SetPostRebaseRotation(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName, FQuat Rotation);

	UFUNCTION(BlueprintCallable, Category = "VRM4U|VMC", meta = (GetOptions = "GetHumanoidNameOptions"))
	static bool ClearPostRebaseRotation(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName);

	UFUNCTION(BlueprintCallable, Category = "VRM4U|VMC")
	static bool ClearAllPostRebaseOverrides(USkeletalMeshComponent* SkelMeshComp);

	// -- Bone masking --
	// Masked bones skip the rebase entirely and do not contribute to the output
	// pose, leaving the upstream pose (typically bind) intact for that bone.

	UFUNCTION(BlueprintCallable, Category = "VRM4U|VMC", meta = (GetOptions = "GetHumanoidNameOptions"))
	static bool SetBoneMasked(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName, bool bMasked);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC", meta = (GetOptions = "GetHumanoidNameOptions"))
	static bool IsBoneMasked(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName);

	UFUNCTION(BlueprintCallable, Category = "VRM4U|VMC")
	static bool ClearAllMasks(USkeletalMeshComponent* SkelMeshComp);

	// -- Bone state introspection --

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC", meta = (GetOptions = "GetHumanoidNameOptions"))
	static bool IsPreRebaseOverridden(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC", meta = (GetOptions = "GetHumanoidNameOptions"))
	static bool IsPostRebaseOverridden(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC", meta = (GetOptions = "GetHumanoidNameOptions"))
	static bool GetPreRebaseRotation(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName, FQuat& OutRotation);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC", meta = (GetOptions = "GetHumanoidNameOptions"))
	static bool GetPostRebaseRotation(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName, FQuat& OutRotation);

	UFUNCTION(BlueprintCallable, Category = "VRM4U|VMC", meta = (GetOptions = "GetHumanoidNameOptions"))
	static bool ClearBoneState(USkeletalMeshComponent* SkelMeshComp, FName HumanoidName);

	// -- Curve overrides --

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC")
	static bool GetLastAppliedCurveValue(USkeletalMeshComponent* SkelMeshComp, FName CurveName, float& OutValue);

	UFUNCTION(BlueprintCallable, Category = "VRM4U|VMC")
	static bool SetCurveOverride(USkeletalMeshComponent* SkelMeshComp, FName CurveName, float Value);

	UFUNCTION(BlueprintCallable, Category = "VRM4U|VMC")
	static bool ClearCurveOverride(USkeletalMeshComponent* SkelMeshComp, FName CurveName);

	UFUNCTION(BlueprintCallable, Category = "VRM4U|VMC")
	static bool ClearAllCurveOverrides(USkeletalMeshComponent* SkelMeshComp);

	UFUNCTION(BlueprintCallable, Category = "VRM4U|VMC")
	static bool SetCurveMasked(USkeletalMeshComponent* SkelMeshComp, FName CurveName, bool bMasked);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC")
	static bool IsCurveMasked(USkeletalMeshComponent* SkelMeshComp, FName CurveName);

	UFUNCTION(BlueprintCallable, Category = "VRM4U|VMC")
	static bool ClearAllCurveMasks(USkeletalMeshComponent* SkelMeshComp);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC")
	static bool IsCurveOverridden(USkeletalMeshComponent* SkelMeshComp, FName CurveName);

	// -- Multi-server inspection --

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC")
	static bool GetRigServer(USkeletalMeshComponent* SkelMeshComp, FString& OutAddress, int32& OutPort);

	// -- Diagnostics --
	// Per-segment direction error (degrees) between a target rig and a reference
	// ("ghost") rig, both VMC-driven on the same stream. Compares each humanoid
	// limb segment's parent->child DIRECTION in component space, which is
	// independent of per-rig bind/axis convention, so it measures the *visible*
	// limb misalignment rather than bind-pose differences. Keyed by the segment's
	// parent humanoid name. Read-only. Returns false if no shared segments
	// resolve on both rigs. Pairs with the debug panel's live compare readout.
	UFUNCTION(BlueprintCallable, Category = "VRM4U|VMC|Diagnostics")
	static bool CompareRigPoseDirections(USkeletalMeshComponent* TargetMesh,
	                                     USkeletalMeshComponent* ReferenceMesh,
	                                     TMap<FName, float>& OutPerSegmentErrorDeg,
	                                     FName& OutWorstSegment,
	                                     float& OutWorstErrorDeg,
	                                     float& OutAverageErrorDeg);

	// -- Editor metadata --

	UFUNCTION()
	static TArray<FString> GetHumanoidNameOptions();
};
