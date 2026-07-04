// VRM4U Copyright (c) 2021-2026 Haruyoshi Yamamoto. This software is released under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VrmSceneLint.generated.h"

class USkeletalMeshComponent;
class UWorld;

UENUM(BlueprintType)
enum class EVrmLintSeverity : uint8
{
	Error,   // will visibly break live mocap
	Warning, // degrades fidelity or hygiene
};

USTRUCT(BlueprintType)
struct FVrmSceneLintFinding
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM4U")
	EVrmLintSeverity Severity = EVrmLintSeverity::Warning;

	// Stable machine-readable id, e.g. "VmcNodeOnNonVrmRig".
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM4U")
	FString CheckId;

	// What is wrong and where (actor/component/asset names included).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM4U")
	FString Detail;

	// The concrete next step that fixes it.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VRM4U")
	FString FixHint;
};

/**
 * Detects the VMC-retargeting misconfigurations that actually bit us (2026-05..07
 * MetaHuman investigation), each paired with a fix hint:
 *  - VmcNodeOnNonVrmRig:   the direct VMC anim node driving a MetaHuman/DAZ/etc. rig
 *  - VmcNodeInPostProcess: a VMC node inside a mesh's post-process AnimBP
 *  - ZeroOffsetRetargetPose: an RTG whose current target pose has no offsets (arms ride low)
 *  - RigMissingStandardChains / RigMissingFingerChains: clavicle/finger gaps on either rig
 *  - StalePerOpIKRig:      an op resolving chains against a different rig than the asset target
 *  - RetargetInstanceNotUsed: an RTG exists for a mesh but the component doesn't use
 *                          UVrmVMCRetargetAnimInstance
 *  - FaceNotDriven / FaceBridgeMissing / LiveLinkPluginDisabled: a MetaHuman face riding on
 *                          a VMC-driven body without a working VMC->LiveLink face bridge
 *
 * Static + BlueprintCallable so editor Python / automation can run it headlessly.
 */
UCLASS()
class VRM4UCAPTUREEDITOR_API UVrmSceneLint : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** Lint every skeletal mesh component in the world (pass any object in that world). */
	UFUNCTION(BlueprintCallable, Category = "VRM4U|Scene Lint", meta = (WorldContext = "WorldContextObject"))
	static TArray<FVrmSceneLintFinding> LintWorld(UObject* WorldContextObject);

	/** Lint a single component (all component- and asset-level checks that apply to it). */
	UFUNCTION(BlueprintCallable, Category = "VRM4U|Scene Lint")
	static TArray<FVrmSceneLintFinding> LintComponent(USkeletalMeshComponent* Component);

	/** LintWorld + one-line-per-finding dump to the log. Returns the number of findings. */
	UFUNCTION(BlueprintCallable, Category = "VRM4U|Scene Lint", meta = (WorldContext = "WorldContextObject"))
	static int32 LintWorldAndLog(UObject* WorldContextObject);
};
