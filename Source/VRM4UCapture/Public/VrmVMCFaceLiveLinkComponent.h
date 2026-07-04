// VRM4U Copyright (c) 2021-2026 Haruyoshi Yamamoto. This software is released under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VrmVMCFaceLiveLinkComponent.generated.h"

class USkeletalMeshComponent;
class UAnimInstance;

/**
 * Level wiring for the VMC -> LiveLink ARKit face bridge. Add to a MetaHuman (or any
 * LiveLink-Face-driven) actor whose body runs from the VMC stream; on BeginPlay it
 *  1. starts publishing the VMC endpoint's blendshape curves as a LiveLink ARKit subject
 *     (UVRM4U_VMCSubsystem::StartVMCFaceLiveLink), and
 *  2. pushes SubjectName into the face mesh's anim instance (the FLiveLinkSubjectName
 *     variable of ABP_MH_LiveLink or any equivalent), so no per-project Blueprint edits
 *     are needed.
 *
 * The whole path is optional-by-construction: with the engine's LiveLink plugin disabled
 * the bridge logs one warning and does nothing — the rest of VRM4U is unaffected.
 * "VRM4U: Auto-Setup VMC Face (LiveLink)" adds + configures this component.
 */
UCLASS(ClassGroup = (VRM4U), meta = (BlueprintSpawnableComponent, DisplayName = "VRM4U VMC Face LiveLink"),
	HideCategories = (Activation, Cooking, AssetUserData, Navigation))
class VRM4UCAPTURE_API UVrmVMCFaceLiveLinkComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVrmVMCFaceLiveLinkComponent();

	/** LiveLink subject the VMC face curves are published under (what the face anim
	 *  blueprint's LiveLink Pose / Evaluate node should point at). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRM4U|VMC Face")
	FName SubjectName = TEXT("VMCFace");

	/** VMC OSC endpoint to read curves from — match the body's VMC node settings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRM4U|VMC Face")
	FString ServerAddress = TEXT("0.0.0.0");

	/** VMC OSC port — match the body's VMC node settings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRM4U|VMC Face")
	int32 Port = 39539;

	/** Start the bridge automatically on BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRM4U|VMC Face")
	bool bAutoStart = true;

	/** On BeginPlay, set SubjectName on the face anim instance's FLiveLinkSubjectName
	 *  variable (retries briefly — anim instances can spawn a few frames late). Disable
	 *  if you wire the subject in your own anim blueprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRM4U|VMC Face")
	bool bBindFaceAnimInstanceSubject = true;

	/** Explicit face component name. Empty = auto-detect: first sibling skeletal mesh whose
	 *  skeleton is a MetaHuman face archetype, else a component literally named "Face". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRM4U|VMC Face")
	FName FaceComponentName;

	/** Start publishing (no-op with a warning if the LiveLink plugin is disabled). */
	UFUNCTION(BlueprintCallable, Category = "VRM4U|VMC Face")
	bool StartBridge();

	/** Stop publishing and remove the LiveLink subject. */
	UFUNCTION(BlueprintCallable, Category = "VRM4U|VMC Face")
	void StopBridge();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC Face")
	bool IsBridgeActive() const;

	/** True when the engine-bundled LiveLink plugin is enabled in this project. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC Face")
	static bool IsLiveLinkClientAvailable();

	/** The face mesh this component would bind (see FaceComponentName). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC Face")
	USkeletalMeshComponent* FindFaceMeshComponent() const;

	/** Shared detection helper (also used by the editor wizard / scene lint). */
	static USkeletalMeshComponent* FindFaceMeshOnActor(const AActor* Actor, FName ExplicitComponentName = NAME_None);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	// Returns true when binding is finished (bound, or determined impossible).
	bool TryBindFaceSubject();

	// Default SubjectName value; a component still carrying this in BeginPlay gets a
	// per-actor-unique name so two default-configured actors don't share one subject.
	static const FName DefaultSubjectName;

	// Anim instance the subject was last bound into; used to detect a runtime
	// anim-instance swap (SetAnimInstanceClass / mesh swap) and re-arm the bind.
	TWeakObjectPtr<UAnimInstance> BoundAnimInstance;

	double BindDeadlineSeconds = 0.0;
	bool bBindPending = false;
};
