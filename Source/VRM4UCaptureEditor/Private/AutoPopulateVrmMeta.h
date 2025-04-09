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

private:
	static bool PopulateForMixamo(UVrmMetaObject* InMetaObject, USkeletalMesh* InSkeletalMesh);
	static bool PopulateForMetaHuman(UVrmMetaObject* InMetaObject, USkeletalMesh* InSkeletalMesh);
	static bool PopulateForDAZ(UVrmMetaObject* InMetaObject, USkeletalMesh* InSkeletalMesh);
	static bool PopulateForVRM(UVrmMetaObject* InMetaObject, USkeletalMesh* InSkeletalMesh);
	static bool ApplyCustomBoneOverrides(UVrmMetaObject* InMetaObject, USkeletalMesh* InSkeletalMesh);

};
