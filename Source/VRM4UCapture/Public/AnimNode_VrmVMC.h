// VRM4U Copyright (c) 2021-2026 Haruyoshi Yamamoto. This software is released under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "BoneContainer.h"
#include "BonePose.h"
#include "BoneControllers/AnimNode_ModifyBone.h"
#include "Misc/EngineVersionComparison.h"
#include "HAL/CriticalSection.h"

#include "OWLVrmVMCBoneState.h"

#include "AnimNode_VrmVMC.generated.h"

class USkeletalMeshComponent;
class UVrmMetaObject;
class UVrmAssetListObject;
class UVrmVMCBlueprintLibrary;

UENUM(BlueprintType)
enum class EVMCPerformanceMode : uint8
{
	Performance    UMETA(DisplayName = "Performance (30 FPS)", Comment = "30 FPS updates - best performance, adequate quality"),
    
	Balanced       UMETA(DisplayName = "Balanced (60 FPS)", Comment = "60 FPS updates - good balance of performance and quality"),
    
	Streaming      UMETA(DisplayName = "Streaming (90 FPS)", Comment = "90 FPS updates - best quality for live streaming"),
    
	Adaptive       UMETA(DisplayName = "Adaptive", Comment = "Automatically adjusts update rate based on system performance (30-90 FPS)"),
    
	Custom         UMETA(DisplayName = "Custom", Comment = "Use custom update rate defined below")
};

/**
*   Simple controller that replaces or adds to the translation/rotation of a single bone.
*/
USTRUCT(BlueprintInternalUseOnly)
struct VRM4UCAPTURE_API FAnimNode_VrmVMC : public FAnimNode_SkeletalControlBase
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Skeleton, meta = (PinHiddenByDefault))
	bool EnableAutoSearchMetaData = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Skeleton, meta=(PinHiddenByDefault))
	const UVrmMetaObject* VrmMetaObject = nullptr;

#if UE_VERSION_OLDER_THAN(5, 0, 0)
    TAssetPtr<UVrmMetaObject> VrmMetaObject_Internal = nullptr;
    TAssetPtr<UVrmAssetListObject> VrmAssetListObject_Internal = nullptr;
#else
	TSoftObjectPtr<const UVrmMetaObject> VrmMetaObject_Internal = nullptr;
	TSoftObjectPtr<UVrmAssetListObject> VrmAssetListObject_Internal = nullptr;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Skeleton, meta = (PinShownByDefault))
	bool bUseRemoteCenterPos = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Skeleton, meta = (PinShownByDefault))
	float ModelRelativeScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Skeleton, meta = (PinShownByDefault))
	bool bIgnoreLocalRotation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Skeleton, meta = (PinShownByDefault))
	FString ServerAddress = "0.0.0.0";

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Skeleton, meta = (PinShownByDefault))
	int Port = 39539;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Skeleton, meta = (PinShownByDefault))
	bool bApplyPerfectSync = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Skeleton, meta = (PinHiddenByDefault))
	bool bForceUpdate = false;

	// Simplified Performance Settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VMC Performance", meta = (PinHiddenByDefault, ToolTip = "Performance preset for different use cases"))
	EVMCPerformanceMode PerformanceMode = EVMCPerformanceMode::Streaming;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VMC Performance", meta = (PinHiddenByDefault,
		EditCondition = "PerformanceMode == EVMCPerformanceMode::Custom", EditConditionHides, ToolTip = "Custom update rate in FPS (30-120). Only visible when using Custom mode"))
	int CustomUpdateRate = 60;

	bool bCreateServer = false;

	TArray<FTransform> RefSkeletonTransform_global;

	FAnimNode_VrmVMC();
	virtual ~FAnimNode_VrmVMC();

	// FAnimNode_Base interface
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	// End of FAnimNode_Base interface

	// FAnimNode_SkeletalControlBase interface
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,
	                                               TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual void EvaluateComponentPose_AnyThread(FComponentSpacePoseContext& Output) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;

	virtual void UpdateInternal(const FAnimationUpdateContext& Context) override;
	// End of FAnimNode_SkeletalControlBase interface

	virtual void ConditionalDebugDraw(FPrimitiveDrawInterface* PDI, USkeletalMeshComponent* PreviewSkelMeshComp,
	                                  bool bPreviewForeground = false) const;

	// -- OWL VMC Blueprint API accessors --
	// Thread-safe reads and writes against the per-bone state cache.
	// HumanoidName uses the same casing as the VMC stream (e.g. "leftUpperArm").
	// Getters return false and leave the out parameter unchanged when the
	// bone has no cached state. Setters create the entry if it does not exist.

	bool TryGetLastAppliedRotation(const FString& HumanoidName, FQuat& OutRotation) const;
	bool TryGetLastAppliedTransform(const FString& HumanoidName, FTransform& OutTransform) const;

	void SetPreRebaseRotation(const FString& HumanoidName, const FQuat& Rotation);
	void ClearPreRebaseRotation(const FString& HumanoidName);
	void ClearAllPreRebaseOverrides();
	bool TryGetPreRebaseRotation(const FString& HumanoidName, FQuat& OutRotation) const;

	void SetPostRebaseRotation(const FString& HumanoidName, const FQuat& Rotation);
	void ClearPostRebaseRotation(const FString& HumanoidName);
	void ClearAllPostRebaseOverrides();
	bool TryGetPostRebaseRotation(const FString& HumanoidName, FQuat& OutRotation) const;

	void SetBoneMasked(const FString& HumanoidName, bool bMasked);
	bool IsBoneMasked(const FString& HumanoidName) const;
	void ClearAllMasks();

	bool IsPreRebaseOverridden(const FString& HumanoidName) const;
	bool IsPostRebaseOverridden(const FString& HumanoidName) const;
	void ClearBoneState(const FString& HumanoidName);
	
	// -- Curve overrides --
	// Substitutes for the incoming VMC curve value. Masked curves skip the
	// apply entirely, leaving the upstream curve value intact.

	bool TryGetLastAppliedCurveValue(const FString& CurveName, float& OutValue) const;

	void SetCurveOverride(const FString& CurveName, float Value);
	void ClearCurveOverride(const FString& CurveName);
	void ClearAllCurveOverrides();

	void SetCurveMasked(const FString& CurveName, bool bMasked);
	bool IsCurveMasked(const FString& CurveName) const;
	void ClearAllCurveMasks();

	bool IsCurveOverridden(const FString& CurveName) const;

private:
	// FAnimNode_SkeletalControlBase interface
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
	// End of FAnimNode_SkeletalControlBase interface

	// The BP library reads RefSkeletonTransform_global and per-bone caches
	// under BoneStateLock to remain race-free with the anim worker thread.
	friend class UVrmVMCBlueprintLibrary;

	// Heap-allocated so the USTRUCT remains copy-assignable (FCriticalSection
	// is non-copyable). Each anim node instance constructs its own lock.
	TSharedPtr<FCriticalSection> BoneStateLock;
	TMap<FString, FOWLVMCPerBoneState> BoneStates;
	TMap<FString, FOWLVMCPerCurveState> CurveStates;
};
