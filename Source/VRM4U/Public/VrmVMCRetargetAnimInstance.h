// VRM4U Copyright (c) 2021-2026 Haruyoshi Yamamoto. This software is released under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Misc/EngineVersionComparison.h"

#if	UE_VERSION_OLDER_THAN(5,2,0)
static_assert(0, "Available for UE5.2+. delete h/cpp files");
#endif

#include "Retargeter/IKRetargeter.h"
#include "VrmVMCRetargetAnimInstance.generated.h"

class USkeletalMeshComponent;
struct FAnimNode_RetargetPoseFromMesh;

/**
 * Minimal, source-agnostic anim instance proxy that runs Epic's FAnimNode_RetargetPoseFromMesh
 * with an explicit source mesh (RetargetFrom = CustomSkeletalMeshComponent). Unlike
 * UVrmAnimInstanceRetargetFromMannequin this does NOT require a VRM asset list on the target, so
 * it is safe to drive a non-VRM target (MetaHuman / DAZ) from a VRM/VRoid (or any) source rig.
 */
USTRUCT()
struct VRM4U_API FVrmVMCRetargetAnimInstanceProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()

	FVrmVMCRetargetAnimInstanceProxy() {}
	FVrmVMCRetargetAnimInstanceProxy(UAnimInstance* InAnimInstance) : FAnimInstanceProxy(InAnimInstance) {}

	// Pushed from the owning UAnimInstance on the game thread each update.
	TWeakObjectPtr<USkeletalMeshComponent> SourceMeshComponent;
	TWeakObjectPtr<UIKRetargeter> Retargeter;

	TSharedPtr<FAnimNode_RetargetPoseFromMesh> Node_Retarget;

	virtual void Initialize(UAnimInstance* InAnimInstance) override;
	virtual bool Evaluate(FPoseContext& Output) override;
	virtual void UpdateAnimationNode(const FAnimationUpdateContext& InContext);
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds);

	void EnsureNode();
};

/**
 * Drives this skeletal mesh by retargeting a live source rig onto it via a UIKRetargeter asset.
 * Intended for the VMC pipeline: source = the VMC-driven rig (your VRM/VRoid template), target =
 * MetaHuman / DAZ. Set SourceMeshComponent + Retargeter (in the editor or via SetRetargetSource).
 */
UCLASS(Blueprintable, BlueprintType)
class VRM4U_API UVrmVMCRetargetAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UVrmVMCRetargetAnimInstance(const FObjectInitializer& ObjectInitializer);

	/** The rig to retarget FROM — the live-driven source (e.g. your VRM/VRoid template). It must
	 *  be animated and tick BEFORE this mesh (see bAddSourceTickPrerequisite). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRM4U|VMC Retarget")
	TObjectPtr<USkeletalMeshComponent> SourceMeshComponent = nullptr;

	/** The Source->Target IK Retargeter asset (RTG_*). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRM4U|VMC Retarget")
	TObjectPtr<UIKRetargeter> Retargeter = nullptr;

	/** Add a tick prerequisite so the source mesh evaluates before this one (avoids 1-frame lag). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRM4U|VMC Retarget")
	bool bAddSourceTickPrerequisite = true;

	/** When Retargeter / SourceMeshComponent are unset at play time, resolve them automatically:
	 *  pick the IK Retargeter asset whose TARGET rig matches this mesh (preview mesh or pelvis
	 *  bone), then the skeletal mesh component in the world matching that retargeter's SOURCE rig.
	 *  This lets the plain C++ class drive a mesh with zero Blueprint wiring: set the mesh's Anim
	 *  Class to this class and press play. Explicitly-set properties always win. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRM4U|VMC Retarget")
	bool bAutoResolveSourceAndRetargeter = true;

	/** Editor-build debug: while playing (PIE), periodically append the SOURCE component pose and
	 *  this mesh's FINAL pose (i.e. after any post-process AnimBP) to
	 *  Saved/VRM4U/PoseCapture_<time>.txt for offline replay analysis
	 *  (VRM4U.VMC.RetargetPoseReplay.CapturedPose). No-op in packaged builds.
	 *  Default OFF: it writes ~4MB/min per instance while enabled — turn it on per-instance
	 *  when you want to (re)capture a session for the replay/fidelity tests. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRM4U|VMC Retarget|Debug")
	bool bDebugCapturePoses = false;

	/** Seconds between debug pose captures. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRM4U|VMC Retarget|Debug", meta = (ClampMin = "0.1"))
	float DebugCaptureIntervalSeconds = 0.5f;

	/** Set the source rig + retargeter at runtime (alternative to the properties above). */
	UFUNCTION(BlueprintCallable, Category = "VRM4U|VMC Retarget")
	void SetRetargetSource(USkeletalMeshComponent* InSource, UIKRetargeter* InRetargeter);

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual bool CanRunParallelWork() const { return false; }

	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	void CapturePosesForDebug();
	void AutoResolveSourceAndRetargeter();

	FVrmVMCRetargetAnimInstanceProxy* MyProxy = nullptr;
	bool bTickPrereqApplied = false;
	double LastDebugCaptureTimeSeconds = -1.0e9;
	double LastAutoResolveTimeSeconds = -1.0e9;
	double LastResolveFailLogTimeSeconds = -1.0e9;
	FString DebugCaptureFilePath;
};
