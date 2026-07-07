// VRM4U Copyright (c) 2021-2026 Haruyoshi Yamamoto. This software is released under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Misc/EngineVersionComparison.h"
#include "VrmRetargetSetupUtil.generated.h"

// The controller APIs the wizard/lint rely on (AddDefaultOps, per-op AutoMapChains,
// AutoAlignAllBones, GetTargetIKRigForOp) exist in this shape from UE 5.6.
#define VRM4U_RETARGET_SETUP (WITH_EDITOR && !UE_VERSION_OLDER_THAN(5, 6, 0))

class AActor;
class UIKRigDefinition;
class UIKRetargeter;
class USkeletalMesh;
class USkeletalMeshComponent;

USTRUCT(BlueprintType)
struct FVrmRetargetSetupReport
{
	GENERATED_BODY()

	// True when a usable source IK rig, target IK rig and retargeter all exist on return.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM4U")
	bool bSuccess = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM4U")
	TObjectPtr<UIKRigDefinition> SourceIKRig = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM4U")
	TObjectPtr<UIKRigDefinition> TargetIKRig = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM4U")
	TObjectPtr<UIKRetargeter> Retargeter = nullptr;

	// Step-by-step log: every reuse/creation decision and warning, in order.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM4U")
	TArray<FString> Messages;
};

/**
 * One-click VMC retarget setup for non-VRM targets (MetaHuman / DAZ / Mixamo / CC4 ...).
 *
 * Builds the full IK-Retargeter pipeline that docs/ik-retargeter-asset-setup.md describes
 * click-by-click: a source IK rig for the VRM (standard chain names, incl. clavicles and
 * fingers), a target IK rig via the engine's auto-characterizer, and an RTG with exact
 * chain mapping and an auto-aligned target retarget pose ("VRM4U_AutoAligned"). Assets are
 * created with source/target rigs assigned BEFORE ops are added, so the stale-per-op-rig
 * failure mode cannot occur.
 *
 * All entry points are static and BlueprintCallable, so they are reachable from editor
 * Python for headless validation. UE 5.6+ only. Earlier engines get a failure report.
 */
UCLASS()
class VRM4UCAPTUREEDITOR_API UVrmRetargetSetupUtil : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** Create (or reuse) both IK rigs plus the retargeter for SourceVrmMesh to TargetMesh.
	 *  AssetFolder optional, defaults to the target mesh's folder (or /Game/VRM4U/Retarget
	 *  when the target lives outside /Game). Creates nothing that already exists and is valid. */
	UFUNCTION(BlueprintCallable, Category = "VRM4U|Retarget Setup")
	static FVrmRetargetSetupReport SetupRetarget(USkeletalMesh* SourceVrmMesh, USkeletalMesh* TargetMesh, const FString& AssetFolder);

	/** Full flow for a level component: auto-detect the VRM source in the world, run
	 *  SetupRetarget, then point the component at UVrmVMCRetargetAnimInstance (the runtime
	 *  auto-resolve wires source + retargeter at play time). */
	UFUNCTION(BlueprintCallable, Category = "VRM4U|Retarget Setup")
	static FVrmRetargetSetupReport SetupRetargetForComponent(USkeletalMeshComponent* TargetComponent);

	/** Face half of the VMC pipeline (MetaHuman): make the actor's face follow the VMC
	 *  blendshape stream via LiveLink. Assigns ABP_MH_LiveLink (or keeps an existing
	 *  LiveLink-consuming anim class) on the face mesh and adds a configured
	 *  UVrmVMCFaceLiveLinkComponent, which at play time publishes the VMC curves as an
	 *  ARKit LiveLink subject and binds it into the face anim instance. Transactional.
	 *  SubjectName NAME_None uses the component default ("VMCFace"). */
	UFUNCTION(BlueprintCallable, Category = "VRM4U|Retarget Setup")
	static FVrmRetargetSetupReport SetupVMCFaceForActor(AActor* Actor, FName SubjectName);

	/** Set the component's anim class to UVrmVMCRetargetAnimInstance (transactional). */
	UFUNCTION(BlueprintCallable, Category = "VRM4U|Retarget Setup")
	static bool ApplyRetargetAnimClassToComponent(USkeletalMeshComponent* TargetComponent);

	/** Find the most plausible live VRM source component in the same world (VRM meta object
	 *  match first, then VRM-humanoid-resolvable bones). Returns null if none. */
	UFUNCTION(BlueprintCallable, Category = "VRM4U|Retarget Setup")
	static USkeletalMeshComponent* FindLikelySourceComponent(USkeletalMeshComponent* ExcludeComponent);

	/** True if the mesh's bones resolve as a VRM humanoid by ANY route (meta table,
	 *  humanoid-renamed bones, or VRoid J_Bip naming). */
	UFUNCTION(BlueprintCallable, Category = "VRM4U|Retarget Setup")
	static bool IsVrmHumanoidMesh(USkeletalMesh* Mesh);

	/** True only when the mesh's OWN bone names are VRM humanoid (humanoid-renamed or
	 *  J_Bip). A DAZ/MetaHuman that merely has an AutoPopulate meta table is NOT native.
	 *  The direct VMC node path is broken for those, so they are retarget targets. */
	UFUNCTION(BlueprintCallable, Category = "VRM4U|Retarget Setup")
	static bool IsNativeVrmHumanoidMesh(USkeletalMesh* Mesh);

	// -- C++ helpers shared with the scene lint (no-ops before UE 5.6) --

	/** First retargeter asset whose TARGET rig's preview mesh is this mesh (loads assets). */
	static UIKRetargeter* FindRetargeterTargetingMesh(USkeletalMesh* Mesh);

	/** True when the anim class carries an FLiveLinkSubjectName variable, i.e. it can be fed
	 *  by a LiveLink subject (ABP_MH_LiveLink and friends). Name-based check on purpose so
	 *  this module needs no LiveLink dependency. Available on every editor build. */
	static bool AnimClassConsumesLiveLink(const UClass* AnimClass);
};
