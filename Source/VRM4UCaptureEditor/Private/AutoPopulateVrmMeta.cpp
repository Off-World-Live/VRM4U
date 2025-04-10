#include "AutoPopulateVrmMeta.h"
#include "Animation/Skeleton.h"
#include "Misc/EngineVersionComparison.h"
#include "VrmMetaObject.h"
#include "VrmUtil.h"

ESkeletonType UAutoPopulateVrmMeta::DetectSkeletonType(USkeletalMesh* InSkeletalMesh)
{
	if (!InSkeletalMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("DetectSkeletonType: Null skeleton mesh provided"));
		return ESkeletonType::Unknown;
	}

	USkeleton* Skeleton = VRMGetSkeleton(InSkeletalMesh);
	if (!Skeleton)
	{
		UE_LOG(LogTemp, Warning, TEXT("DetectSkeletonType: Could not get skeleton from mesh"));
		return ESkeletonType::Unknown;
	}

	// Get bone names to detect skeleton type
	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
	TArray<FName> BoneNames;
	for (int32 i = 0; i < RefSkeleton.GetNum(); ++i)
	{
		BoneNames.Add(RefSkeleton.GetBoneName(i));
	}

	// Check for VRM specific bones
	if (BoneNames.Contains(FName("J_Bip_C_Hips")) ||
		BoneNames.Contains(FName("vrm_hips")) ||
		BoneNames.Contains(FName("J_Bip_L_UpperArm")) ||
		BoneNames.Contains(FName("J_Adj_L_FaceEye")))
	{
		UE_LOG(LogTemp, Log, TEXT("Detected VRM skeleton type"));
		return ESkeletonType::VRM;
	}

	// Check for Standard MetaHuman (Mixamo-like naming)
	if (BoneNames.Contains(FName("Hips")) &&
		BoneNames.Contains(FName("Spine1")) &&
		BoneNames.Contains(FName("LeftArm")) &&
		(BoneNames.Contains(FName("LeftEye")) || BoneNames.Contains(FName("RightEye"))))
	{
		UE_LOG(LogTemp, Log, TEXT("Detected MetaHuman skeleton type (standard naming)"));
		return ESkeletonType::MetaHuman;
	}

	// Check for Epic-style MetaHuman
	if (BoneNames.Contains(FName("pelvis")) &&
		BoneNames.Contains(FName("spine_01")) &&
		BoneNames.Contains(FName("clavicle_l")))
	{
		UE_LOG(LogTemp, Log, TEXT("Detected MetaHuman skeleton type (Epic naming)"));
		return ESkeletonType::MetaHuman;
	}

	// Check for Mixamo specific bones
	if (BoneNames.Contains(FName("Hips")) &&
		BoneNames.Contains(FName("Spine")) &&
		BoneNames.Contains(FName("LeftArm")))
	{
		UE_LOG(LogTemp, Log, TEXT("Detected Mixamo skeleton type"));
		return ESkeletonType::Mixamo;
	}

	// Check for DAZ specific bones
	if (BoneNames.Contains(FName("hip")) &&
		BoneNames.Contains(FName("abdomen")) &&
		BoneNames.Contains(FName("lShldr")))
	{
		UE_LOG(LogTemp, Log, TEXT("Detected DAZ skeleton type"));
		return ESkeletonType::DAZ;
	}

	UE_LOG(LogTemp, Warning, TEXT("Could not detect skeleton type"));
	return ESkeletonType::Unknown;
}

bool UAutoPopulateVrmMeta::AutoPopulateMetaObject(UVrmMetaObject* InMetaObject, USkeletalMesh* InSkeletalMesh)
{
	if (!InMetaObject)
	{
		UE_LOG(LogTemp, Error, TEXT("AutoPopulateMetaObject: Null VrmMetaObject provided"));
		return false;
	}

	if (!InSkeletalMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("AutoPopulateMetaObject: Null SkeletalMesh provided"));
		return false;
	}

	// Set the skeletal mesh first
	InMetaObject->SkeletalMesh = InSkeletalMesh;

	// Determine skeleton type based on preference or auto-detection
	ESkeletonType SkeletonType = ESkeletonType::Unknown;

	if (InMetaObject->SkeletonType == EVrmSkeletonType::Auto)
	{
		// Auto detect
		SkeletonType = DetectSkeletonType(InSkeletalMesh);
		if (SkeletonType == ESkeletonType::Unknown)
		{
			UE_LOG(LogTemp, Warning, TEXT("Failed to auto-detect skeleton type, no mapping will be applied"));
		}
		else
		{
			FString TypeName;
			switch (SkeletonType)
			{
			case ESkeletonType::VRM: TypeName = TEXT("VRM");
				break;
			case ESkeletonType::Mixamo: TypeName = TEXT("Mixamo");
				break;
			case ESkeletonType::MetaHuman: TypeName = TEXT("MetaHuman");
				break;
			case ESkeletonType::DAZ: TypeName = TEXT("DAZ");
				break;
			default: TypeName = TEXT("Unknown");
			}
			UE_LOG(LogTemp, Log, TEXT("Auto-detected skeleton type: %s"), *TypeName);
		}
	}
	else
	{
		// Use the user-specified type
		switch (InMetaObject->SkeletonType)
		{
		case EVrmSkeletonType::VRM:
			SkeletonType = ESkeletonType::VRM;
			UE_LOG(LogTemp, Log, TEXT("Using user-specified skeleton type: VRM"));
			break;
		case EVrmSkeletonType::Mixamo:
			SkeletonType = ESkeletonType::Mixamo;
			UE_LOG(LogTemp, Log, TEXT("Using user-specified skeleton type: Mixamo"));
			break;
		case EVrmSkeletonType::MetaHuman:
			SkeletonType = ESkeletonType::MetaHuman;
			UE_LOG(LogTemp, Log, TEXT("Using user-specified skeleton type: MetaHuman"));
			break;
		case EVrmSkeletonType::DAZ:
			SkeletonType = ESkeletonType::DAZ;
			UE_LOG(LogTemp, Log, TEXT("Using user-specified skeleton type: DAZ"));
			break;
		default:
			SkeletonType = ESkeletonType::Unknown;
			UE_LOG(LogTemp, Warning, TEXT("Invalid user-specified skeleton type"));
			break;
		}
	}

	// Based on the determined type, populate the bone mappings
	bool bSuccess = false;
	switch (SkeletonType)
	{
	case ESkeletonType::Mixamo:
		bSuccess = PopulateForMixamo(InMetaObject, InSkeletalMesh);
		break;
	case ESkeletonType::MetaHuman:
		bSuccess = PopulateForMetaHuman(InMetaObject, InSkeletalMesh);
		break;
	case ESkeletonType::DAZ:
		bSuccess = PopulateForDAZ(InMetaObject, InSkeletalMesh);
		break;
	case ESkeletonType::VRM:
		bSuccess = PopulateForVRM(InMetaObject, InSkeletalMesh);
		break;
	default:
		UE_LOG(LogTemp, Warning, TEXT("No skeleton type identified for bone mapping"));
		return false;
	}

	// Apply custom bone overrides (implementation for Improvement #4)
	ApplyCustomBoneOverrides(InMetaObject, InSkeletalMesh);

	if (bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("Successfully mapped %d bones for skeleton"), InMetaObject->humanoidBoneTable.Num());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Failed to map bones for %s skeleton type"),
				SkeletonType == ESkeletonType::VRM ? TEXT("VRM") :
				SkeletonType == ESkeletonType::Mixamo ? TEXT("Mixamo") :
				SkeletonType == ESkeletonType::MetaHuman ? TEXT("MetaHuman") :
				SkeletonType == ESkeletonType::DAZ ? TEXT("DAZ") : TEXT("Unknown"));
	}

	return bSuccess;
}

bool UAutoPopulateVrmMeta::PopulateForVRM(UVrmMetaObject* InMetaObject, USkeletalMesh* InSkeletalMesh)
{
    if (!InMetaObject)
    {
        UE_LOG(LogTemp, Error, TEXT("PopulateForVRM: Null meta object"));
        return false;
    }

    if (!InSkeletalMesh)
    {
        UE_LOG(LogTemp, Error, TEXT("PopulateForVRM: Null skeletal mesh"));
        return false;
    }

    // Clear existing mappings
    InMetaObject->humanoidBoneTable.Empty();

    // Get skeleton to find available bones
    USkeleton* Skeleton = VRMGetSkeleton(InSkeletalMesh);
    if (!Skeleton)
    {
        UE_LOG(LogTemp, Error, TEXT("PopulateForVRM: Could not get skeleton"));
        return false;
    }

    const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
    TSet<FName> AvailableBones;
    for (int32 i = 0; i < RefSkeleton.GetNum(); ++i)
    {
        AvailableBones.Add(RefSkeleton.GetBoneName(i));
    }
    
    UE_LOG(LogTemp, Log, TEXT("VRM mapping: Found %d bones in skeleton"), AvailableBones.Num());

    // Define mappings for VRoid-style prefixed bones with critical flags
    struct FBoneMapEntry
    {
        FString HumanoidName;
        FString SkeletonName;
        bool bIsCritical;
    };

    TArray<FBoneMapEntry> VRMBoneMap = {
        // Main body - critical bones
        { TEXT("hips"), TEXT("J_Bip_C_Hips"), true },
        { TEXT("spine"), TEXT("J_Bip_C_Spine"), true },
        { TEXT("chest"), TEXT("J_Bip_C_Chest"), true },
        { TEXT("upperChest"), TEXT("J_Bip_C_UpperChest"), false },
        { TEXT("neck"), TEXT("J_Bip_C_Neck"), true },
        { TEXT("head"), TEXT("J_Bip_C_Head"), true },
        { TEXT("leftEye"), TEXT("J_Adj_L_FaceEye"), false },
        { TEXT("rightEye"), TEXT("J_Adj_R_FaceEye"), false },

        // Left arm - critical bones
        { TEXT("leftShoulder"), TEXT("J_Bip_L_Shoulder"), true },
        { TEXT("leftUpperArm"), TEXT("J_Bip_L_UpperArm"), true },
        { TEXT("leftLowerArm"), TEXT("J_Bip_L_LowerArm"), true },
        { TEXT("leftHand"), TEXT("J_Bip_L_Hand"), true },

        // Right arm - critical bones
        { TEXT("rightShoulder"), TEXT("J_Bip_R_Shoulder"), true },
        { TEXT("rightUpperArm"), TEXT("J_Bip_R_UpperArm"), true },
        { TEXT("rightLowerArm"), TEXT("J_Bip_R_LowerArm"), true },
        { TEXT("rightHand"), TEXT("J_Bip_R_Hand"), true },

        // Left leg - critical bones
        { TEXT("leftUpperLeg"), TEXT("J_Bip_L_UpperLeg"), true },
        { TEXT("leftLowerLeg"), TEXT("J_Bip_L_LowerLeg"), true },
        { TEXT("leftFoot"), TEXT("J_Bip_L_Foot"), true },
        { TEXT("leftToes"), TEXT("J_Bip_L_ToeBase"), false },

        // Right leg - critical bones
        { TEXT("rightUpperLeg"), TEXT("J_Bip_R_UpperLeg"), true },
        { TEXT("rightLowerLeg"), TEXT("J_Bip_R_LowerLeg"), true },
        { TEXT("rightFoot"), TEXT("J_Bip_R_Foot"), true },
        { TEXT("rightToes"), TEXT("J_Bip_R_ToeBase"), false },

        // Left fingers - non-critical bones
        { TEXT("leftThumbProximal"), TEXT("J_Bip_L_Thumb1"), false },
        { TEXT("leftThumbIntermediate"), TEXT("J_Bip_L_Thumb2"), false },
        { TEXT("leftThumbDistal"), TEXT("J_Bip_L_Thumb3"), false },
        { TEXT("leftIndexProximal"), TEXT("J_Bip_L_Index1"), false },
        { TEXT("leftIndexIntermediate"), TEXT("J_Bip_L_Index2"), false },
        { TEXT("leftIndexDistal"), TEXT("J_Bip_L_Index3"), false },
        { TEXT("leftMiddleProximal"), TEXT("J_Bip_L_Middle1"), false },
        { TEXT("leftMiddleIntermediate"), TEXT("J_Bip_L_Middle2"), false },
        { TEXT("leftMiddleDistal"), TEXT("J_Bip_L_Middle3"), false },
        { TEXT("leftRingProximal"), TEXT("J_Bip_L_Ring1"), false },
        { TEXT("leftRingIntermediate"), TEXT("J_Bip_L_Ring2"), false },
        { TEXT("leftRingDistal"), TEXT("J_Bip_L_Ring3"), false },
        { TEXT("leftLittleProximal"), TEXT("J_Bip_L_Little1"), false },
        { TEXT("leftLittleIntermediate"), TEXT("J_Bip_L_Little2"), false },
        { TEXT("leftLittleDistal"), TEXT("J_Bip_L_Little3"), false },

        // Right fingers - non-critical bones
        { TEXT("rightThumbProximal"), TEXT("J_Bip_R_Thumb1"), false },
        { TEXT("rightThumbIntermediate"), TEXT("J_Bip_R_Thumb2"), false },
        { TEXT("rightThumbDistal"), TEXT("J_Bip_R_Thumb3"), false },
        { TEXT("rightIndexProximal"), TEXT("J_Bip_R_Index1"), false },
        { TEXT("rightIndexIntermediate"), TEXT("J_Bip_R_Index2"), false },
        { TEXT("rightIndexDistal"), TEXT("J_Bip_R_Index3"), false },
        { TEXT("rightMiddleProximal"), TEXT("J_Bip_R_Middle1"), false },
        { TEXT("rightMiddleIntermediate"), TEXT("J_Bip_R_Middle2"), false },
        { TEXT("rightMiddleDistal"), TEXT("J_Bip_R_Middle3"), false },
        { TEXT("rightRingProximal"), TEXT("J_Bip_R_Ring1"), false },
        { TEXT("rightRingIntermediate"), TEXT("J_Bip_R_Ring2"), false },
        { TEXT("rightRingDistal"), TEXT("J_Bip_R_Ring3"), false },
        { TEXT("rightLittleProximal"), TEXT("J_Bip_R_Little1"), false },
        { TEXT("rightLittleIntermediate"), TEXT("J_Bip_R_Little2"), false },
        { TEXT("rightLittleDistal"), TEXT("J_Bip_R_Little3"), false }
    };

    // Track mapping statistics
    int32 TotalMapped = 0;
    int32 TotalCritical = 0;
    int32 MappedCritical = 0;
    TArray<FString> MissingCriticalBones;
    bool bUsedPrefixedNames = false;
    bool bUsedBasicNames = false;
    bool bUsedVrmPrefixedNames = false;

    // First try with VRoid's prefixed naming convention
    for (const FBoneMapEntry& Entry : VRMBoneMap)
    {
        if (Entry.bIsCritical)
        {
            TotalCritical++;
        }

        if (AvailableBones.Contains(*Entry.SkeletonName))
        {
            InMetaObject->humanoidBoneTable.Add(Entry.HumanoidName, Entry.SkeletonName);
            TotalMapped++;
            bUsedPrefixedNames = true;
            
            if (Entry.bIsCritical)
            {
                MappedCritical++;
            }
        }
        else if (Entry.bIsCritical)
        {
            MissingCriticalBones.Add(Entry.HumanoidName + TEXT(" (") + Entry.SkeletonName + TEXT(")"));
        }
    }

    // If no or few mappings were found, try standard VRM bone names as fallback
    if (TotalMapped < TotalCritical / 2)
    {
        UE_LOG(LogTemp, Log, TEXT("VRM mapping: Prefixed naming convention failed, trying standard VRM names"));
        
        // Reset mapping statistics
        InMetaObject->humanoidBoneTable.Empty();
        TotalMapped = 0;
        MappedCritical = 0;
        MissingCriticalBones.Empty();
        
        // Try with base names
        for (const FBoneMapEntry& Entry : VRMBoneMap)
        {
            if (AvailableBones.Contains(*Entry.HumanoidName))
            {
                InMetaObject->humanoidBoneTable.Add(Entry.HumanoidName, Entry.HumanoidName);
                TotalMapped++;
                bUsedBasicNames = true;
                
                if (Entry.bIsCritical)
                {
                    MappedCritical++;
                }
            }
            else 
            {
                // Try with "vrm_" prefix
                FString VrmPrefixedName = FString::Printf(TEXT("vrm_%s"), *Entry.HumanoidName);
                if (AvailableBones.Contains(*VrmPrefixedName))
                {
                    InMetaObject->humanoidBoneTable.Add(Entry.HumanoidName, VrmPrefixedName);
                    TotalMapped++;
                    bUsedVrmPrefixedNames = true;
                    
                    if (Entry.bIsCritical)
                    {
                        MappedCritical++;
                    }
                }
                else if (Entry.bIsCritical)
                {
                    MissingCriticalBones.Add(Entry.HumanoidName);
                }
            }
        }
    }

    // Log mapping results
    if (bUsedPrefixedNames)
    {
        UE_LOG(LogTemp, Log, TEXT("VRM mapping: Used VRoid prefixed naming convention"));
    }
    else if (bUsedBasicNames)
    {
        UE_LOG(LogTemp, Log, TEXT("VRM mapping: Used basic VRM bone names"));
    }
    else if (bUsedVrmPrefixedNames)
    {
        UE_LOG(LogTemp, Log, TEXT("VRM mapping: Used 'vrm_' prefixed bone names"));
    }
    
    UE_LOG(LogTemp, Log, TEXT("VRM mapping: Successfully mapped %d of %d bones (%d of %d critical bones)"), 
           TotalMapped, VRMBoneMap.Num(), MappedCritical, TotalCritical);
    
    if (MissingCriticalBones.Num() > 0)
    {
        FString MissingBonesStr = FString::Join(MissingCriticalBones, TEXT(", "));
        UE_LOG(LogTemp, Warning, TEXT("Missing critical bones: %s"), *MissingBonesStr);
    }

    // Success if we mapped at least one bone, with better results if we mapped all critical bones
    if (TotalMapped > 0)
    {
        if (MappedCritical == TotalCritical)
        {
            UE_LOG(LogTemp, Log, TEXT("VRM mapping: All critical bones mapped successfully"));
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("VRM mapping: Some critical bones could not be mapped (%d/%d)"), 
                  MappedCritical, TotalCritical);
        }
        return true;
    }
    
    UE_LOG(LogTemp, Error, TEXT("VRM mapping: Failed to map any bones"));
    return false;
}

bool UAutoPopulateVrmMeta::PopulateForMixamo(UVrmMetaObject* InMetaObject, USkeletalMesh* InSkeletalMesh)
{
	if (!InMetaObject)
	{
		UE_LOG(LogTemp, Error, TEXT("PopulateForMixamo: Null meta object"));
		return false;
	}

	if (!InSkeletalMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("PopulateForMixamo: Null skeletal mesh"));
		return false;
	}

	// Clear existing mappings
	InMetaObject->humanoidBoneTable.Empty();

	// Get skeleton to find available bones
	USkeleton* Skeleton = VRMGetSkeleton(InSkeletalMesh);
	if (!Skeleton)
	{
		UE_LOG(LogTemp, Error, TEXT("PopulateForMixamo: Could not get skeleton"));
		return false;
	}

	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
	TSet<FName> AvailableBones;
	for (int32 i = 0; i < RefSkeleton.GetNum(); ++i)
	{
		AvailableBones.Add(RefSkeleton.GetBoneName(i));
	}

	// Define all expected Mixamo bone mappings
	struct FBoneMapEntry
	{
		FString HumanoidName;
		FString MixamoName;
		bool bIsCritical; // Is this bone critical for the animation to work properly?
	};

	TArray<FBoneMapEntry> BoneMap = {
		// Main body - critical bones
		{TEXT("hips"), TEXT("Hips"), true},
		{TEXT("spine"), TEXT("Spine"), true},
		{TEXT("chest"), TEXT("Spine2"), true},
		{TEXT("neck"), TEXT("Neck"), true},
		{TEXT("head"), TEXT("Head"), true},

		// Left arm - critical bones
		{TEXT("leftShoulder"), TEXT("LeftShoulder"), true},
		{TEXT("leftUpperArm"), TEXT("LeftArm"), true},
		{TEXT("leftLowerArm"), TEXT("LeftForeArm"), true},
		{TEXT("leftHand"), TEXT("LeftHand"), true},

		// Right arm - critical bones
		{TEXT("rightShoulder"), TEXT("RightShoulder"), true},
		{TEXT("rightUpperArm"), TEXT("RightArm"), true},
		{TEXT("rightLowerArm"), TEXT("RightForeArm"), true},
		{TEXT("rightHand"), TEXT("RightHand"), true},

		// Left leg - critical bones
		{TEXT("leftUpperLeg"), TEXT("LeftUpLeg"), true},
		{TEXT("leftLowerLeg"), TEXT("LeftLeg"), true},
		{TEXT("leftFoot"), TEXT("LeftFoot"), true},
		{TEXT("leftToes"), TEXT("LeftToeBase"), false},

		// Right leg - critical bones
		{TEXT("rightUpperLeg"), TEXT("RightUpLeg"), true},
		{TEXT("rightLowerLeg"), TEXT("RightLeg"), true},
		{TEXT("rightFoot"), TEXT("RightFoot"), true},
		{TEXT("rightToes"), TEXT("RightToeBase"), false},

		// Left fingers - non-critical bones
		{TEXT("leftThumbProximal"), TEXT("LeftHandThumb1"), false},
		{TEXT("leftThumbIntermediate"), TEXT("LeftHandThumb2"), false},
		{TEXT("leftThumbDistal"), TEXT("LeftHandThumb3"), false},
		{TEXT("leftIndexProximal"), TEXT("LeftHandIndex1"), false},
		{TEXT("leftIndexIntermediate"), TEXT("LeftHandIndex2"), false},
		{TEXT("leftIndexDistal"), TEXT("LeftHandIndex3"), false},
		{TEXT("leftMiddleProximal"), TEXT("LeftHandMiddle1"), false},
		{TEXT("leftMiddleIntermediate"), TEXT("LeftHandMiddle2"), false},
		{TEXT("leftMiddleDistal"), TEXT("LeftHandMiddle3"), false},
		{TEXT("leftRingProximal"), TEXT("LeftHandRing1"), false},
		{TEXT("leftRingIntermediate"), TEXT("LeftHandRing2"), false},
		{TEXT("leftRingDistal"), TEXT("LeftHandRing3"), false},
		{TEXT("leftLittleProximal"), TEXT("LeftHandPinky1"), false},
		{TEXT("leftLittleIntermediate"), TEXT("LeftHandPinky2"), false},
		{TEXT("leftLittleDistal"), TEXT("LeftHandPinky3"), false},

		// Right fingers - non-critical bones
		{TEXT("rightThumbProximal"), TEXT("RightHandThumb1"), false},
		{TEXT("rightThumbIntermediate"), TEXT("RightHandThumb2"), false},
		{TEXT("rightThumbDistal"), TEXT("RightHandThumb3"), false},
		{TEXT("rightIndexProximal"), TEXT("RightHandIndex1"), false},
		{TEXT("rightIndexIntermediate"), TEXT("RightHandIndex2"), false},
		{TEXT("rightIndexDistal"), TEXT("RightHandIndex3"), false},
		{TEXT("rightMiddleProximal"), TEXT("RightHandMiddle1"), false},
		{TEXT("rightMiddleIntermediate"), TEXT("RightHandMiddle2"), false},
		{TEXT("rightMiddleDistal"), TEXT("RightHandMiddle3"), false},
		{TEXT("rightRingProximal"), TEXT("RightHandRing1"), false},
		{TEXT("rightRingIntermediate"), TEXT("RightHandRing2"), false},
		{TEXT("rightRingDistal"), TEXT("RightHandRing3"), false},
		{TEXT("rightLittleProximal"), TEXT("RightHandPinky1"), false},
		{TEXT("rightLittleIntermediate"), TEXT("RightHandPinky2"), false},
		{TEXT("rightLittleDistal"), TEXT("RightHandPinky3"), false},
	};

	// Track mapping statistics
	int32 TotalMapped = 0;
	int32 TotalCritical = 0;
	int32 MappedCritical = 0;
	TArray<FString> MissingCriticalBones;

	// Process each bone mapping
	for (const FBoneMapEntry& Entry : BoneMap)
	{
		if (Entry.bIsCritical)
		{
			TotalCritical++;
		}

		if (AvailableBones.Contains(*Entry.MixamoName))
		{
			InMetaObject->humanoidBoneTable.Add(Entry.HumanoidName, Entry.MixamoName);
			TotalMapped++;

			if (Entry.bIsCritical)
			{
				MappedCritical++;
			}
		}
		else if (Entry.bIsCritical)
		{
			MissingCriticalBones.Add(Entry.HumanoidName + TEXT(" (") + Entry.MixamoName + TEXT(")"));
		}
	}

	// Log mapping results
	UE_LOG(LogTemp, Log, TEXT("Mixamo mapping: Successfully mapped %d of %d bones (%d of %d critical bones)"),
			TotalMapped, BoneMap.Num(), MappedCritical, TotalCritical);

	if (MissingCriticalBones.Num() > 0)
	{
		FString MissingBonesStr = FString::Join(MissingCriticalBones, TEXT(", "));
		UE_LOG(LogTemp, Warning, TEXT("Missing critical bones: %s"), *MissingBonesStr);
	}

	// Return success if all critical bones were mapped, or at least some bones were mapped
	return (MappedCritical == TotalCritical) || (TotalMapped > 0);
}

bool UAutoPopulateVrmMeta::PopulateForMetaHuman(UVrmMetaObject* InMetaObject, USkeletalMesh* InSkeletalMesh)
{
	if (!InMetaObject)
	{
		UE_LOG(LogTemp, Error, TEXT("PopulateForMetaHuman: Null meta object"));
		return false;
	}

	if (!InSkeletalMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("PopulateForMetaHuman: Null skeletal mesh"));
		return false;
	}

	// Clear existing mappings
	InMetaObject->humanoidBoneTable.Empty();

	// Get skeleton to determine which naming convention it uses
	USkeleton* Skeleton = VRMGetSkeleton(InSkeletalMesh);
	if (!Skeleton)
	{
		UE_LOG(LogTemp, Error, TEXT("PopulateForMetaHuman: Could not get skeleton"));
		return false;
	}

	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
	TSet<FName> AvailableBones;
	for (int32 i = 0; i < RefSkeleton.GetNum(); ++i)
	{
		AvailableBones.Add(RefSkeleton.GetBoneName(i));
	}

	UE_LOG(LogTemp, Log, TEXT("MetaHuman mapping: Found %d bones in skeleton"), AvailableBones.Num());

	// Check if this is a standard MetaHuman (like the one in the provided hierarchy)
	bool bIsStandardMetaHuman = AvailableBones.Contains(FName("Hips")) &&
		AvailableBones.Contains(FName("Spine1")) &&
		AvailableBones.Contains(FName("LeftArm"));

	// Track mapping statistics
	int32 TotalMapped = 0;
	int32 TotalCritical = 0;
	int32 MappedCritical = 0;
	TArray<FString> MissingCriticalBones;

	// Create mapping data structure like we did for Mixamo
	struct FBoneMapEntry
	{
		FString HumanoidName;
		FString SkeletonName;
		bool bIsCritical;
	};

	TArray<FBoneMapEntry> BoneMap;

	if (bIsStandardMetaHuman)
	{
		UE_LOG(LogTemp, Log, TEXT("Using standard MetaHuman (Mixamo-like) naming convention"));

		// Define mappings for standard MetaHuman
		BoneMap = {
			// Main body - critical bones
			{TEXT("hips"), TEXT("Hips"), true},
			{TEXT("spine"), TEXT("Spine"), true},
			{TEXT("chest"), TEXT("Spine2"), true},
			{TEXT("neck"), TEXT("Neck"), true},
			{TEXT("head"), TEXT("Head"), true},

			// Left arm - critical bones
			{TEXT("leftShoulder"), TEXT("LeftShoulder"), true},
			{TEXT("leftUpperArm"), TEXT("LeftArm"), true},
			{TEXT("leftLowerArm"), TEXT("LeftForeArm"), true},
			{TEXT("leftHand"), TEXT("LeftHand"), true},

			// Right arm - critical bones
			{TEXT("rightShoulder"), TEXT("RightShoulder"), true},
			{TEXT("rightUpperArm"), TEXT("RightArm"), true},
			{TEXT("rightLowerArm"), TEXT("RightForeArm"), true},
			{TEXT("rightHand"), TEXT("RightHand"), true},

			// Left leg - critical bones
			{TEXT("leftUpperLeg"), TEXT("LeftUpLeg"), true},
			{TEXT("leftLowerLeg"), TEXT("LeftLeg"), true},
			{TEXT("leftFoot"), TEXT("LeftFoot"), true},
			{TEXT("leftToes"), TEXT("LeftToeBase"), false},

			// Right leg - critical bones
			{TEXT("rightUpperLeg"), TEXT("RightUpLeg"), true},
			{TEXT("rightLowerLeg"), TEXT("RightLeg"), true},
			{TEXT("rightFoot"), TEXT("RightFoot"), true},
			{TEXT("rightToes"), TEXT("RightToeBase"), false},

			// Left fingers - non-critical bones
			{TEXT("leftThumbProximal"), TEXT("LeftHandThumb1"), false},
			{TEXT("leftThumbIntermediate"), TEXT("LeftHandThumb2"), false},
			{TEXT("leftThumbDistal"), TEXT("LeftHandThumb3"), false},
			{TEXT("leftIndexProximal"), TEXT("LeftHandIndex1"), false},
			{TEXT("leftIndexIntermediate"), TEXT("LeftHandIndex2"), false},
			{TEXT("leftIndexDistal"), TEXT("LeftHandIndex3"), false},
			{TEXT("leftMiddleProximal"), TEXT("LeftHandMiddle1"), false},
			{TEXT("leftMiddleIntermediate"), TEXT("LeftHandMiddle2"), false},
			{TEXT("leftMiddleDistal"), TEXT("LeftHandMiddle3"), false},
			{TEXT("leftRingProximal"), TEXT("LeftHandRing1"), false},
			{TEXT("leftRingIntermediate"), TEXT("LeftHandRing2"), false},
			{TEXT("leftRingDistal"), TEXT("LeftHandRing3"), false},
			{TEXT("leftLittleProximal"), TEXT("LeftHandPinky1"), false},
			{TEXT("leftLittleIntermediate"), TEXT("LeftHandPinky2"), false},
			{TEXT("leftLittleDistal"), TEXT("LeftHandPinky3"), false},

			// Right fingers - non-critical bones
			{TEXT("rightThumbProximal"), TEXT("RightHandThumb1"), false},
			{TEXT("rightThumbIntermediate"), TEXT("RightHandThumb2"), false},
			{TEXT("rightThumbDistal"), TEXT("RightHandThumb3"), false},
			{TEXT("rightIndexProximal"), TEXT("RightHandIndex1"), false},
			{TEXT("rightIndexIntermediate"), TEXT("RightHandIndex2"), false},
			{TEXT("rightIndexDistal"), TEXT("RightHandIndex3"), false},
			{TEXT("rightMiddleProximal"), TEXT("RightHandMiddle1"), false},
			{TEXT("rightMiddleIntermediate"), TEXT("RightHandMiddle2"), false},
			{TEXT("rightMiddleDistal"), TEXT("RightHandMiddle3"), false},
			{TEXT("rightRingProximal"), TEXT("RightHandRing1"), false},
			{TEXT("rightRingIntermediate"), TEXT("RightHandRing2"), false},
			{TEXT("rightRingDistal"), TEXT("RightHandRing3"), false},
			{TEXT("rightLittleProximal"), TEXT("RightHandPinky1"), false},
			{TEXT("rightLittleIntermediate"), TEXT("RightHandPinky2"), false},
			{TEXT("rightLittleDistal"), TEXT("RightHandPinky3"), false},

			// Eyes - non-critical bones
			{TEXT("leftEye"), TEXT("LeftEye"), false},
			{TEXT("rightEye"), TEXT("RightEye"), false},
		};
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("Using Epic skeleton naming convention for MetaHuman"));

		// Define mappings for Epic-style MetaHuman
		BoneMap = {
			// Main body - critical bones
			{TEXT("hips"), TEXT("pelvis"), true},
			{TEXT("spine"), TEXT("spine_01"), true},
			{TEXT("chest"), TEXT("spine_03"), true},
			{TEXT("neck"), TEXT("neck_01"), true},
			{TEXT("head"), TEXT("head"), true},

			// Left arm - critical bones
			{TEXT("leftShoulder"), TEXT("clavicle_l"), true},
			{TEXT("leftUpperArm"), TEXT("upperarm_l"), true},
			{TEXT("leftLowerArm"), TEXT("lowerarm_l"), true},
			{TEXT("leftHand"), TEXT("hand_l"), true},

			// Right arm - critical bones
			{TEXT("rightShoulder"), TEXT("clavicle_r"), true},
			{TEXT("rightUpperArm"), TEXT("upperarm_r"), true},
			{TEXT("rightLowerArm"), TEXT("lowerarm_r"), true},
			{TEXT("rightHand"), TEXT("hand_r"), true},

			// Left leg - critical bones
			{TEXT("leftUpperLeg"), TEXT("thigh_l"), true},
			{TEXT("leftLowerLeg"), TEXT("calf_l"), true},
			{TEXT("leftFoot"), TEXT("foot_l"), true},
			{TEXT("leftToes"), TEXT("ball_l"), false},

			// Right leg - critical bones
			{TEXT("rightUpperLeg"), TEXT("thigh_r"), true},
			{TEXT("rightLowerLeg"), TEXT("calf_r"), true},
			{TEXT("rightFoot"), TEXT("foot_r"), true},
			{TEXT("rightToes"), TEXT("ball_r"), false},

			// Left fingers - non-critical bones
			{TEXT("leftThumbProximal"), TEXT("thumb_01_l"), false},
			{TEXT("leftThumbIntermediate"), TEXT("thumb_02_l"), false},
			{TEXT("leftThumbDistal"), TEXT("thumb_03_l"), false},
			{TEXT("leftIndexProximal"), TEXT("index_01_l"), false},
			{TEXT("leftIndexIntermediate"), TEXT("index_02_l"), false},
			{TEXT("leftIndexDistal"), TEXT("index_03_l"), false},
			{TEXT("leftMiddleProximal"), TEXT("middle_01_l"), false},
			{TEXT("leftMiddleIntermediate"), TEXT("middle_02_l"), false},
			{TEXT("leftMiddleDistal"), TEXT("middle_03_l"), false},
			{TEXT("leftRingProximal"), TEXT("ring_01_l"), false},
			{TEXT("leftRingIntermediate"), TEXT("ring_02_l"), false},
			{TEXT("leftRingDistal"), TEXT("ring_03_l"), false},
			{TEXT("leftLittleProximal"), TEXT("pinky_01_l"), false},
			{TEXT("leftLittleIntermediate"), TEXT("pinky_02_l"), false},
			{TEXT("leftLittleDistal"), TEXT("pinky_03_l"), false},

			// Right fingers - non-critical bones
			{TEXT("rightThumbProximal"), TEXT("thumb_01_r"), false},
			{TEXT("rightThumbIntermediate"), TEXT("thumb_02_r"), false},
			{TEXT("rightThumbDistal"), TEXT("thumb_03_r"), false},
			{TEXT("rightIndexProximal"), TEXT("index_01_r"), false},
			{TEXT("rightIndexIntermediate"), TEXT("index_02_r"), false},
			{TEXT("rightIndexDistal"), TEXT("index_03_r"), false},
			{TEXT("rightMiddleProximal"), TEXT("middle_01_r"), false},
			{TEXT("rightMiddleIntermediate"), TEXT("middle_02_r"), false},
			{TEXT("rightMiddleDistal"), TEXT("middle_03_r"), false},
			{TEXT("rightRingProximal"), TEXT("ring_01_r"), false},
			{TEXT("rightRingIntermediate"), TEXT("ring_02_r"), false},
			{TEXT("rightRingDistal"), TEXT("ring_03_r"), false},
			{TEXT("rightLittleProximal"), TEXT("pinky_01_r"), false},
			{TEXT("rightLittleIntermediate"), TEXT("pinky_02_r"), false},
			{TEXT("rightLittleDistal"), TEXT("pinky_03_r"), false},

			// Check for MetaHuman eye bones with different naming conventions
			{TEXT("leftEye"), TEXT("eye_l"), false},
			{TEXT("rightEye"), TEXT("eye_r"), false},
		};
	}

	// Process each bone mapping
	for (const FBoneMapEntry& Entry : BoneMap)
	{
		if (Entry.bIsCritical)
		{
			TotalCritical++;
		}

		if (AvailableBones.Contains(*Entry.SkeletonName))
		{
			InMetaObject->humanoidBoneTable.Add(Entry.HumanoidName, Entry.SkeletonName);
			TotalMapped++;

			if (Entry.bIsCritical)
			{
				MappedCritical++;
			}
		}
		else if (Entry.bIsCritical)
		{
			MissingCriticalBones.Add(Entry.HumanoidName + TEXT(" (") + Entry.SkeletonName + TEXT(")"));
		}
	}

	// Log mapping results
	UE_LOG(LogTemp, Log, TEXT("MetaHuman mapping: Successfully mapped %d of %d bones (%d of %d critical bones)"),
			TotalMapped, BoneMap.Num(), MappedCritical, TotalCritical);

	if (MissingCriticalBones.Num() > 0)
	{
		FString MissingBonesStr = FString::Join(MissingCriticalBones, TEXT(", "));
		UE_LOG(LogTemp, Warning, TEXT("Missing critical bones: %s"), *MissingBonesStr);
	}

	// Return success if all critical bones were mapped, or at least some bones were mapped
	return (MappedCritical == TotalCritical) || (TotalMapped > 0);
}

bool UAutoPopulateVrmMeta::PopulateForDAZ(UVrmMetaObject* InMetaObject, USkeletalMesh* InSkeletalMesh)
{
	if (!InMetaObject)
	{
		UE_LOG(LogTemp, Error, TEXT("PopulateForDAZ: Null meta object"));
		return false;
	}

	if (!InSkeletalMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("PopulateForDAZ: Null skeletal mesh"));
		return false;
	}

	// Clear existing mappings
	InMetaObject->humanoidBoneTable.Empty();

	// Get skeleton to find available bones
	USkeleton* Skeleton = VRMGetSkeleton(InSkeletalMesh);
	if (!Skeleton)
	{
		UE_LOG(LogTemp, Error, TEXT("PopulateForDAZ: Could not get skeleton"));
		return false;
	}

	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
	TSet<FName> AvailableBones;
	for (int32 i = 0; i < RefSkeleton.GetNum(); ++i)
	{
		AvailableBones.Add(RefSkeleton.GetBoneName(i));
	}
	
	UE_LOG(LogTemp, Log, TEXT("DAZ mapping: Found %d bones in skeleton"), AvailableBones.Num());

	// Define all expected DAZ bone mappings with critical flags
	struct FBoneMapEntry
	{
		FString HumanoidName;
		FString DAZName;
		bool bIsCritical; // Is this bone critical for the animation to work properly?
	};

	TArray<FBoneMapEntry> BoneMap = {
		// Main body - critical bones
		{TEXT("hips"), TEXT("hip"), true},
		{TEXT("spine"), TEXT("abdomen"), true},
		{TEXT("chest"), TEXT("chest"), true},
		{TEXT("neck"), TEXT("neck"), true},
		{TEXT("head"), TEXT("head"), true},

		// Left arm - critical bones
		{TEXT("leftShoulder"), TEXT("lCollar"), true},
		{TEXT("leftUpperArm"), TEXT("lShldr"), true},
		{TEXT("leftLowerArm"), TEXT("lForeArm"), true},
		{TEXT("leftHand"), TEXT("lHand"), true},

		// Right arm - critical bones
		{TEXT("rightShoulder"), TEXT("rCollar"), true},
		{TEXT("rightUpperArm"), TEXT("rShldr"), true},
		{TEXT("rightLowerArm"), TEXT("rForeArm"), true},
		{TEXT("rightHand"), TEXT("rHand"), true},

		// Left leg - critical bones
		{TEXT("leftUpperLeg"), TEXT("lThigh"), true},
		{TEXT("leftLowerLeg"), TEXT("lShin"), true},
		{TEXT("leftFoot"), TEXT("lFoot"), true},
		{TEXT("leftToes"), TEXT("lToe"), false},

		// Right leg - critical bones
		{TEXT("rightUpperLeg"), TEXT("rThigh"), true},
		{TEXT("rightLowerLeg"), TEXT("rShin"), true},
		{TEXT("rightFoot"), TEXT("rFoot"), true},
		{TEXT("rightToes"), TEXT("rToe"), false},

		// Left fingers - non-critical bones
		{TEXT("leftThumbProximal"), TEXT("lThumb1"), false},
		{TEXT("leftThumbIntermediate"), TEXT("lThumb2"), false},
		{TEXT("leftThumbDistal"), TEXT("lThumb3"), false},
		{TEXT("leftIndexProximal"), TEXT("lIndex1"), false},
		{TEXT("leftIndexIntermediate"), TEXT("lIndex2"), false},
		{TEXT("leftIndexDistal"), TEXT("lIndex3"), false},
		{TEXT("leftMiddleProximal"), TEXT("lMid1"), false},
		{TEXT("leftMiddleIntermediate"), TEXT("lMid2"), false},
		{TEXT("leftMiddleDistal"), TEXT("lMid3"), false},
		{TEXT("leftRingProximal"), TEXT("lRing1"), false},
		{TEXT("leftRingIntermediate"), TEXT("lRing2"), false},
		{TEXT("leftRingDistal"), TEXT("lRing3"), false},
		{TEXT("leftLittleProximal"), TEXT("lPinky1"), false},
		{TEXT("leftLittleIntermediate"), TEXT("lPinky2"), false},
		{TEXT("leftLittleDistal"), TEXT("lPinky3"), false},

		// Right fingers - non-critical bones
		{TEXT("rightThumbProximal"), TEXT("rThumb1"), false},
		{TEXT("rightThumbIntermediate"), TEXT("rThumb2"), false},
		{TEXT("rightThumbDistal"), TEXT("rThumb3"), false},
		{TEXT("rightIndexProximal"), TEXT("rIndex1"), false},
		{TEXT("rightIndexIntermediate"), TEXT("rIndex2"), false},
		{TEXT("rightIndexDistal"), TEXT("rIndex3"), false},
		{TEXT("rightMiddleProximal"), TEXT("rMid1"), false},
		{TEXT("rightMiddleIntermediate"), TEXT("rMid2"), false},
		{TEXT("rightMiddleDistal"), TEXT("rMid3"), false},
		{TEXT("rightRingProximal"), TEXT("rRing1"), false},
		{TEXT("rightRingIntermediate"), TEXT("rRing2"), false},
		{TEXT("rightRingDistal"), TEXT("rRing3"), false},
		{TEXT("rightLittleProximal"), TEXT("rPinky1"), false},
		{TEXT("rightLittleIntermediate"), TEXT("rPinky2"), false},
		{TEXT("rightLittleDistal"), TEXT("rPinky3"), false},
		
		// Eyes - non-critical bones
		{TEXT("leftEye"), TEXT("lEye"), false},
		{TEXT("rightEye"), TEXT("rEye"), false},
	};

	// Track mapping statistics
	int32 TotalMapped = 0;
	int32 TotalCritical = 0;
	int32 MappedCritical = 0;
	TArray<FString> MissingCriticalBones;

	// Process each bone mapping
	for (const FBoneMapEntry& Entry : BoneMap)
	{
		if (Entry.bIsCritical)
		{
			TotalCritical++;
		}

		if (AvailableBones.Contains(*Entry.DAZName))
		{
			InMetaObject->humanoidBoneTable.Add(Entry.HumanoidName, Entry.DAZName);
			TotalMapped++;

			if (Entry.bIsCritical)
			{
				MappedCritical++;
			}
		}
		else if (Entry.bIsCritical)
		{
			MissingCriticalBones.Add(Entry.HumanoidName + TEXT(" (") + Entry.DAZName + TEXT(")"));
		}
	}

	// Check for alternative bone naming that some DAZ exports might use
	if (TotalMapped < TotalCritical / 2)
	{
		UE_LOG(LogTemp, Log, TEXT("DAZ mapping: Standard DAZ naming convention failed, trying alternative naming"));
		
		// Define alternative naming mappings for DAZ exports that might use different conventions
		TArray<TPair<FString, FString>> AlternativeNamings = {
			// Alternative naming for main body
			{TEXT("hip"), TEXT("pelvis")},
			{TEXT("abdomen"), TEXT("spine")},
			{TEXT("chest"), TEXT("chest1")},
			
			// Alternative naming for arms
			{TEXT("lCollar"), TEXT("l_clavicle")},
			{TEXT("rCollar"), TEXT("r_clavicle")},
			{TEXT("lShldr"), TEXT("l_upperarm")},
			{TEXT("rShldr"), TEXT("r_upperarm")},
			{TEXT("lForeArm"), TEXT("l_forearm")},
			{TEXT("rForeArm"), TEXT("r_forearm")},
			
			// Alternative naming for legs
			{TEXT("lThigh"), TEXT("l_thigh")},
			{TEXT("rThigh"), TEXT("r_thigh")},
			{TEXT("lShin"), TEXT("l_calf")},
			{TEXT("rShin"), TEXT("r_calf")},
		};
		
		// Try each alternative naming
		for (const TPair<FString, FString>& Alternative : AlternativeNamings)
		{
			for (const FBoneMapEntry& Entry : BoneMap)
			{
				// Skip if already mapped
				if (InMetaObject->humanoidBoneTable.Contains(Entry.HumanoidName))
				{
					continue;
				}
				
				// Check if this entry matches the current alternative
				if (Entry.DAZName == Alternative.Key && AvailableBones.Contains(*Alternative.Value))
				{
					InMetaObject->humanoidBoneTable.Add(Entry.HumanoidName, Alternative.Value);
					TotalMapped++;
					
					if (Entry.bIsCritical)
					{
						// Update tracking for critical bones
						MappedCritical++;
						// Remove from missing list if it was there
						MissingCriticalBones.Remove(Entry.HumanoidName + TEXT(" (") + Entry.DAZName + TEXT(")"));
					}
					
					UE_LOG(LogTemp, Log, TEXT("Applied alternative DAZ mapping: %s -> %s (instead of %s)"), 
						*Entry.HumanoidName, *Alternative.Value, *Entry.DAZName);
				}
			}
		}
	}

	// Log mapping results
	UE_LOG(LogTemp, Log, TEXT("DAZ mapping: Successfully mapped %d of %d bones (%d of %d critical bones)"),
			TotalMapped, BoneMap.Num(), MappedCritical, TotalCritical);

	if (MissingCriticalBones.Num() > 0)
	{
		FString MissingBonesStr = FString::Join(MissingCriticalBones, TEXT(", "));
		UE_LOG(LogTemp, Warning, TEXT("Missing critical bones: %s"), *MissingBonesStr);
	}

	// Return success if all critical bones were mapped, or at least some bones were mapped
	if (MappedCritical == TotalCritical)
	{
		UE_LOG(LogTemp, Log, TEXT("DAZ mapping: All critical bones mapped successfully"));
		return true;
	}
	else if (TotalMapped > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("DAZ mapping: Some critical bones could not be mapped (%d/%d)"), 
			  MappedCritical, TotalCritical);
		return true;
	}
	
	UE_LOG(LogTemp, Error, TEXT("DAZ mapping: Failed to map any bones"));
	return false;
}

bool UAutoPopulateVrmMeta::ApplyCustomBoneOverrides(UVrmMetaObject* InMetaObject, USkeletalMesh* InSkeletalMesh)
{
	if (!InMetaObject || !InSkeletalMesh || InMetaObject->CustomBoneOverrides.Num() == 0)
	{
		// No overrides to apply
		return false;
	}

	USkeleton* Skeleton = VRMGetSkeleton(InSkeletalMesh);
	if (!Skeleton)
	{
		UE_LOG(LogTemp, Error, TEXT("ApplyCustomBoneOverrides: Could not get skeleton"));
		return false;
	}

	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
	int32 OverridesApplied = 0;

	for (const FVrmBoneOverride& Override : InMetaObject->CustomBoneOverrides)
	{
		if (Override.HumanoidBoneName.IsEmpty() || Override.ModelBoneName.IsEmpty())
		{
			continue;
		}

		// Check if the target bone exists in the skeleton
		int32 BoneIndex = RefSkeleton.FindBoneIndex(*Override.ModelBoneName);
		if (BoneIndex != INDEX_NONE)
		{
			// Add or update the bone mapping
			InMetaObject->humanoidBoneTable.Add(Override.HumanoidBoneName, Override.ModelBoneName);
			UE_LOG(LogTemp, Log, TEXT("Applied custom bone override: %s -> %s"),
					*Override.HumanoidBoneName, *Override.ModelBoneName);
			OverridesApplied++;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Custom bone override failed: Bone '%s' not found in skeleton"),
					*Override.ModelBoneName);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("Applied %d custom bone overrides"), OverridesApplied);
	return OverridesApplied > 0;
}
