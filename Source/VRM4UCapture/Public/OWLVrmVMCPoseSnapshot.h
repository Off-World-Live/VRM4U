#pragma once

#include "CoreMinimal.h"

#include "OWLVrmVMCPoseSnapshot.generated.h"

/**
 * Snapshot of a VMC-driven pose captured at a single point in time. Indexed
 * by humanoid name (VMC protocol casing, e.g. "leftUpperArm"). All three
 * arrays are parallel: HumanoidNames[i] describes the bone whose component
 * and local transforms live at LocalTransforms[i] and ComponentTransforms[i].
 *
 * Scope is humanoid-mapped bones only. 
 *
 * Component transforms are relative to the source skeletal mesh component.
 * Local transforms are relative to the bone's humanoid parent in the same
 * snapshot. World-space transforms are not stored, since the world matrix
 * at snapshot time may not be meaningful at injection time. Compose with
 * the target mesh component's world transform at apply time if world space
 * is needed.
 */
USTRUCT(BlueprintType)
struct VRM4UCAPTURE_API FVrmVMCPoseSnapshot
{
	GENERATED_BODY()

	// Humanoid bone names. Order is stable for a given source rig but is not
	// guaranteed to match the canonical humanoid name list returned by
	// UVrmVMCBlueprintLibrary::GetHumanoidNameOptions.
	UPROPERTY(BlueprintReadOnly, Category = "VRM4U|VMC")
	TArray<FName> HumanoidNames;

	// Per-bone transform relative to the bone's humanoid parent in this
	// snapshot. Bones whose humanoid parent is not mapped store their
	// component-space transform here as a fallback.
	UPROPERTY(BlueprintReadOnly, Category = "VRM4U|VMC")
	TArray<FTransform> LocalTransforms;

	// Per-bone transform relative to the source skeletal mesh component.
	UPROPERTY(BlueprintReadOnly, Category = "VRM4U|VMC")
	TArray<FTransform> ComponentTransforms;

	// FPlatformTime::Seconds() at capture.
	UPROPERTY(BlueprintReadOnly, Category = "VRM4U|VMC")
	float CaptureTimeSeconds = 0.f;

	// Debug only. Do not use for routing or comparison.
	UPROPERTY(BlueprintReadOnly, Category = "VRM4U|VMC")
	FName SourceSkelMeshCompName = NAME_None;
};
