#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "AutoPopulateVrmMeta.generated.h"

class UVrmMetaObject;
class USkeletalMesh;

UENUM(BlueprintType)
enum class ESkeletonType : uint8
{
	Unknown,
	VRM,
	Mixamo,
	MetaHuman,
	DAZ
};

// Outcome of a UI-driven auto-populate run (see AutoPopulateWithUi).
struct FVrmAutoPopulateUiResult
{
	bool bSuccess = false;
	FString TypeName;
	int32 MappedBones = 0;
};

/**
 * Utility class for auto-populating VrmMetaObject based on skeleton detection
 */
UCLASS(BlueprintType)
class VRM4UCAPTUREEDITOR_API UAutoPopulateVrmMeta : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "VRM4U|Utilities")
	static ESkeletonType DetectSkeletonType(USkeletalMesh* InSkeletalMesh);

	UFUNCTION(BlueprintCallable, Category = "VRM4U|Utilities")
	static bool AutoPopulateMetaObject(UVrmMetaObject* InMetaObject, USkeletalMesh* InSkeletalMesh);

	// Shared editor-UI wrapper for every Auto-Populate entry point (meta-asset
	// details button, anim-node auto-trigger). Resolves the skeleton type name
	// (an explicit SkeletonType on the meta wins over detection), runs the
	// populate inside an undo transaction, flushes change/dirty state, and shows
	// one consistent toast for every outcome. Callers add only entry-point-
	// specific follow-up (e.g. refreshing a details panel).
	// bShowAssignReminder: append the "assign this asset to your VRM VMC node"
	// next step - pass false when the meta is already being assigned to a node.
	static FVrmAutoPopulateUiResult AutoPopulateWithUi(UVrmMetaObject* MetaObject, bool bShowAssignReminder);

private:
	static bool PopulateForMixamo(UVrmMetaObject* InMetaObject, USkeletalMesh* InSkeletalMesh);
	static bool PopulateForMetaHuman(UVrmMetaObject* InMetaObject, USkeletalMesh* InSkeletalMesh);
	static bool PopulateForDAZ(UVrmMetaObject* InMetaObject, USkeletalMesh* InSkeletalMesh);
	static bool PopulateForVRM(UVrmMetaObject* InMetaObject, USkeletalMesh* InSkeletalMesh);
	static bool ApplyCustomBoneOverrides(UVrmMetaObject* InMetaObject, USkeletalMesh* InSkeletalMesh);

};
