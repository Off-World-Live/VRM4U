#include "AutoPopulateVrmMeta.h"
#include "Animation/Skeleton.h"
#include "Misc/EngineVersionComparison.h"
#include "VrmMetaObject.h"
#include "VrmUtil.h"
#include "ScopedTransaction.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

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

	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
	TArray<FName> BoneNames;
	for (int32 i = 0; i < RefSkeleton.GetNum(); ++i)
	{
		BoneNames.Add(RefSkeleton.GetBoneName(i));
	}

	if (BoneNames.Contains(FName("J_Bip_C_Hips")) ||
		BoneNames.Contains(FName("vrm_hips")) ||
		BoneNames.Contains(FName("J_Bip_L_UpperArm")) ||
		BoneNames.Contains(FName("J_Adj_L_FaceEye")))
	{
		UE_LOG(LogTemp, Log, TEXT("Detected VRM skeleton type"));
		return ESkeletonType::VRM;
	}

	if (BoneNames.Contains(FName("Hips")) &&
		BoneNames.Contains(FName("Spine1")) &&
		BoneNames.Contains(FName("LeftArm")) &&
		(BoneNames.Contains(FName("LeftEye")) || BoneNames.Contains(FName("RightEye"))))
	{
		UE_LOG(LogTemp, Log, TEXT("Detected MetaHuman skeleton type (standard naming)"));
		return ESkeletonType::MetaHuman;
	}

	if (BoneNames.Contains(FName("pelvis")) &&
		BoneNames.Contains(FName("spine_01")) &&
		BoneNames.Contains(FName("clavicle_l")))
	{
		UE_LOG(LogTemp, Log, TEXT("Detected MetaHuman skeleton type (Epic naming)"));
		return ESkeletonType::MetaHuman;
	}

	if (BoneNames.Contains(FName("Hips")) &&
		BoneNames.Contains(FName("Spine")) &&
		BoneNames.Contains(FName("LeftArm")))
	{
		UE_LOG(LogTemp, Log, TEXT("Detected Mixamo skeleton type"));
		return ESkeletonType::Mixamo;
	}

	if (BoneNames.Contains(FName("pelvis")) &&
		BoneNames.Contains(FName("abdomenLower")) &&
		BoneNames.Contains(FName("lShldrBend")))
	{
		UE_LOG(LogTemp, Log, TEXT("Detected DAZ Genesis 8 skeleton type"));
		return ESkeletonType::DAZ;
	}

	// Pre-Genesis-8 DAZ exports use generic naming (hip/abdomen/lShldr).
	if (BoneNames.Contains(FName("hip")) &&
		BoneNames.Contains(FName("abdomen")) &&
		BoneNames.Contains(FName("lShldr")))
	{
		UE_LOG(LogTemp, Log, TEXT("Detected DAZ skeleton type (pre-Genesis 8 naming)"));
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

	InMetaObject->SkeletalMesh = InSkeletalMesh;

	ESkeletonType SkeletonType = ESkeletonType::Unknown;

	if (InMetaObject->SkeletonType == EVrmSkeletonType::Auto)
	{
		SkeletonType = DetectSkeletonType(InSkeletalMesh);
		if (SkeletonType == ESkeletonType::Unknown)
		{
			UE_LOG(LogTemp, Warning, TEXT("Failed to auto-detect skeleton type, no mapping will be applied"));
		}
		else
		{
			FString TypeName = UEnum::GetValueAsString(SkeletonType);
			UE_LOG(LogTemp, Log, TEXT("Auto-detected skeleton type: %s"), *TypeName);
		}
	}
	else
	{
		switch (InMetaObject->SkeletonType)
		{
		case EVrmSkeletonType::VRM:
			SkeletonType = ESkeletonType::VRM;
			break;
		case EVrmSkeletonType::Mixamo:
			SkeletonType = ESkeletonType::Mixamo;
			break;
		case EVrmSkeletonType::MetaHuman:
			SkeletonType = ESkeletonType::MetaHuman;
			break;
		case EVrmSkeletonType::DAZ:
			SkeletonType = ESkeletonType::DAZ;
			break;
		default:
			SkeletonType = ESkeletonType::Unknown;
			break;
		}
		
		FString TypeName = UEnum::GetValueAsString(SkeletonType);
		UE_LOG(LogTemp, Log, TEXT("Using user-specified skeleton type: %s"), *TypeName);
	}

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

	ApplyCustomBoneOverrides(InMetaObject, InSkeletalMesh);

	if (bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("Successfully mapped %d bones for skeleton"), InMetaObject->humanoidBoneTable.Num());
	}
	else
	{
		FString TypeName = UEnum::GetValueAsString(SkeletonType);
		UE_LOG(LogTemp, Warning, TEXT("Failed to map bones for %s skeleton type"), *TypeName);
	}

	return bSuccess;
}

namespace
{
	struct FVrmBoneMapEntry
	{
		FString HumanoidName;
		TArray<FString> Candidates; // tried in order; first present in the skeleton wins
		bool bIsCritical;
	};

	bool BuildAvailableBoneSet(USkeletalMesh* InSkeletalMesh, const TCHAR* Context, TSet<FName>& OutBones)
	{
		USkeleton* Skeleton = VRMGetSkeleton(InSkeletalMesh);
		if (!Skeleton)
		{
			UE_LOG(LogTemp, Error, TEXT("%s: Could not get skeleton"), Context);
			return false;
		}

		const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
		OutBones.Reserve(RefSkeleton.GetNum());
		for (int32 i = 0; i < RefSkeleton.GetNum(); ++i)
		{
			OutBones.Add(RefSkeleton.GetBoneName(i));
		}
		return true;
	}

	bool ApplyBoneMap(UVrmMetaObject* InMetaObject, const TSet<FName>& AvailableBones,
		const TArray<FVrmBoneMapEntry>& BoneMap, const TCHAR* Context)
	{
		int32 TotalMapped = 0;
		int32 TotalCritical = 0;
		int32 MappedCritical = 0;
		TArray<FString> MissingCriticalBones;

		for (const FVrmBoneMapEntry& Entry : BoneMap)
		{
			if (Entry.bIsCritical)
			{
				++TotalCritical;
			}

			const FString* MatchedBone = nullptr;
			for (const FString& Candidate : Entry.Candidates)
			{
				if (AvailableBones.Contains(*Candidate))
				{
					MatchedBone = &Candidate;
					break;
				}
			}

			if (MatchedBone)
			{
				InMetaObject->humanoidBoneTable.Add(Entry.HumanoidName, *MatchedBone);
				++TotalMapped;
				if (Entry.bIsCritical)
				{
					++MappedCritical;
				}
			}
			else if (Entry.bIsCritical)
			{
				MissingCriticalBones.Add(Entry.HumanoidName + TEXT(" (") + FString::Join(Entry.Candidates, TEXT("/")) + TEXT(")"));
			}
		}

		UE_LOG(LogTemp, Log, TEXT("%s mapping: Successfully mapped %d of %d bones (%d of %d critical bones)"),
			Context, TotalMapped, BoneMap.Num(), MappedCritical, TotalCritical);

		if (MissingCriticalBones.Num() > 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("Missing critical bones: %s"), *FString::Join(MissingCriticalBones, TEXT(", ")));
		}

		return TotalMapped > 0;
	}
}

bool UAutoPopulateVrmMeta::PopulateForVRM(UVrmMetaObject* InMetaObject, USkeletalMesh* InSkeletalMesh)
{
	if (!InMetaObject || !InSkeletalMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("PopulateForVRM: Null meta object or skeletal mesh"));
		return false;
	}

	InMetaObject->humanoidBoneTable.Empty();

	TSet<FName> AvailableBones;
	if (!BuildAvailableBoneSet(InSkeletalMesh, TEXT("PopulateForVRM"), AvailableBones))
	{
		return false;
	}

	// VRoid uses J_Bip prefixes; other exporters use the plain name or a "vrm_" prefix.
	struct FSeed { const TCHAR* Humanoid; const TCHAR* Prefixed; bool bCritical; };
	static const FSeed Seeds[] = {
		{TEXT("hips"), TEXT("J_Bip_C_Hips"), true},
		{TEXT("spine"), TEXT("J_Bip_C_Spine"), true},
		{TEXT("chest"), TEXT("J_Bip_C_Chest"), true},
		{TEXT("upperChest"), TEXT("J_Bip_C_UpperChest"), false},
		{TEXT("neck"), TEXT("J_Bip_C_Neck"), true},
		{TEXT("head"), TEXT("J_Bip_C_Head"), true},
		{TEXT("leftEye"), TEXT("J_Adj_L_FaceEye"), false},
		{TEXT("rightEye"), TEXT("J_Adj_R_FaceEye"), false},

		{TEXT("leftShoulder"), TEXT("J_Bip_L_Shoulder"), true},
		{TEXT("leftUpperArm"), TEXT("J_Bip_L_UpperArm"), true},
		{TEXT("leftLowerArm"), TEXT("J_Bip_L_LowerArm"), true},
		{TEXT("leftHand"), TEXT("J_Bip_L_Hand"), true},

		{TEXT("rightShoulder"), TEXT("J_Bip_R_Shoulder"), true},
		{TEXT("rightUpperArm"), TEXT("J_Bip_R_UpperArm"), true},
		{TEXT("rightLowerArm"), TEXT("J_Bip_R_LowerArm"), true},
		{TEXT("rightHand"), TEXT("J_Bip_R_Hand"), true},

		{TEXT("leftUpperLeg"), TEXT("J_Bip_L_UpperLeg"), true},
		{TEXT("leftLowerLeg"), TEXT("J_Bip_L_LowerLeg"), true},
		{TEXT("leftFoot"), TEXT("J_Bip_L_Foot"), true},
		{TEXT("leftToes"), TEXT("J_Bip_L_ToeBase"), false},

		{TEXT("rightUpperLeg"), TEXT("J_Bip_R_UpperLeg"), true},
		{TEXT("rightLowerLeg"), TEXT("J_Bip_R_LowerLeg"), true},
		{TEXT("rightFoot"), TEXT("J_Bip_R_Foot"), true},
		{TEXT("rightToes"), TEXT("J_Bip_R_ToeBase"), false},

		{TEXT("leftThumbProximal"), TEXT("J_Bip_L_Thumb1"), false},
		{TEXT("leftThumbIntermediate"), TEXT("J_Bip_L_Thumb2"), false},
		{TEXT("leftThumbDistal"), TEXT("J_Bip_L_Thumb3"), false},
		{TEXT("leftIndexProximal"), TEXT("J_Bip_L_Index1"), false},
		{TEXT("leftIndexIntermediate"), TEXT("J_Bip_L_Index2"), false},
		{TEXT("leftIndexDistal"), TEXT("J_Bip_L_Index3"), false},
		{TEXT("leftMiddleProximal"), TEXT("J_Bip_L_Middle1"), false},
		{TEXT("leftMiddleIntermediate"), TEXT("J_Bip_L_Middle2"), false},
		{TEXT("leftMiddleDistal"), TEXT("J_Bip_L_Middle3"), false},
		{TEXT("leftRingProximal"), TEXT("J_Bip_L_Ring1"), false},
		{TEXT("leftRingIntermediate"), TEXT("J_Bip_L_Ring2"), false},
		{TEXT("leftRingDistal"), TEXT("J_Bip_L_Ring3"), false},
		{TEXT("leftLittleProximal"), TEXT("J_Bip_L_Little1"), false},
		{TEXT("leftLittleIntermediate"), TEXT("J_Bip_L_Little2"), false},
		{TEXT("leftLittleDistal"), TEXT("J_Bip_L_Little3"), false},

		{TEXT("rightThumbProximal"), TEXT("J_Bip_R_Thumb1"), false},
		{TEXT("rightThumbIntermediate"), TEXT("J_Bip_R_Thumb2"), false},
		{TEXT("rightThumbDistal"), TEXT("J_Bip_R_Thumb3"), false},
		{TEXT("rightIndexProximal"), TEXT("J_Bip_R_Index1"), false},
		{TEXT("rightIndexIntermediate"), TEXT("J_Bip_R_Index2"), false},
		{TEXT("rightIndexDistal"), TEXT("J_Bip_R_Index3"), false},
		{TEXT("rightMiddleProximal"), TEXT("J_Bip_R_Middle1"), false},
		{TEXT("rightMiddleIntermediate"), TEXT("J_Bip_R_Middle2"), false},
		{TEXT("rightMiddleDistal"), TEXT("J_Bip_R_Middle3"), false},
		{TEXT("rightRingProximal"), TEXT("J_Bip_R_Ring1"), false},
		{TEXT("rightRingIntermediate"), TEXT("J_Bip_R_Ring2"), false},
		{TEXT("rightRingDistal"), TEXT("J_Bip_R_Ring3"), false},
		{TEXT("rightLittleProximal"), TEXT("J_Bip_R_Little1"), false},
		{TEXT("rightLittleIntermediate"), TEXT("J_Bip_R_Little2"), false},
		{TEXT("rightLittleDistal"), TEXT("J_Bip_R_Little3"), false},
	};

	TArray<FVrmBoneMapEntry> BoneMap;
	BoneMap.Reserve(UE_ARRAY_COUNT(Seeds));
	for (const FSeed& Seed : Seeds)
	{
		FVrmBoneMapEntry Entry;
		Entry.HumanoidName = Seed.Humanoid;
		Entry.bIsCritical = Seed.bCritical;
		Entry.Candidates = { Seed.Prefixed, Seed.Humanoid, FString::Printf(TEXT("vrm_%s"), Seed.Humanoid) };
		BoneMap.Add(MoveTemp(Entry));
	}

	return ApplyBoneMap(InMetaObject, AvailableBones, BoneMap, TEXT("VRM"));
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

	InMetaObject->humanoidBoneTable.Empty();

	TSet<FName> AvailableBones;
	if (!BuildAvailableBoneSet(InSkeletalMesh, TEXT("PopulateForMixamo"), AvailableBones))
	{
		return false;
	}

	const TArray<FVrmBoneMapEntry> BoneMap = {
		{TEXT("hips"), {TEXT("Hips")}, true},
		{TEXT("spine"), {TEXT("Spine")}, true},
		{TEXT("chest"), {TEXT("Spine2")}, true},
		{TEXT("neck"), {TEXT("Neck")}, true},
		{TEXT("head"), {TEXT("Head")}, true},

		{TEXT("leftShoulder"), {TEXT("LeftShoulder")}, true},
		{TEXT("leftUpperArm"), {TEXT("LeftArm")}, true},
		{TEXT("leftLowerArm"), {TEXT("LeftForeArm")}, true},
		{TEXT("leftHand"), {TEXT("LeftHand")}, true},

		{TEXT("rightShoulder"), {TEXT("RightShoulder")}, true},
		{TEXT("rightUpperArm"), {TEXT("RightArm")}, true},
		{TEXT("rightLowerArm"), {TEXT("RightForeArm")}, true},
		{TEXT("rightHand"), {TEXT("RightHand")}, true},

		{TEXT("leftUpperLeg"), {TEXT("LeftUpLeg")}, true},
		{TEXT("leftLowerLeg"), {TEXT("LeftLeg")}, true},
		{TEXT("leftFoot"), {TEXT("LeftFoot")}, true},
		{TEXT("leftToes"), {TEXT("LeftToeBase")}, false},

		{TEXT("rightUpperLeg"), {TEXT("RightUpLeg")}, true},
		{TEXT("rightLowerLeg"), {TEXT("RightLeg")}, true},
		{TEXT("rightFoot"), {TEXT("RightFoot")}, true},
		{TEXT("rightToes"), {TEXT("RightToeBase")}, false},

		{TEXT("leftThumbProximal"), {TEXT("LeftHandThumb1")}, false},
		{TEXT("leftThumbIntermediate"), {TEXT("LeftHandThumb2")}, false},
		{TEXT("leftThumbDistal"), {TEXT("LeftHandThumb3")}, false},
		{TEXT("leftIndexProximal"), {TEXT("LeftHandIndex1")}, false},
		{TEXT("leftIndexIntermediate"), {TEXT("LeftHandIndex2")}, false},
		{TEXT("leftIndexDistal"), {TEXT("LeftHandIndex3")}, false},
		{TEXT("leftMiddleProximal"), {TEXT("LeftHandMiddle1")}, false},
		{TEXT("leftMiddleIntermediate"), {TEXT("LeftHandMiddle2")}, false},
		{TEXT("leftMiddleDistal"), {TEXT("LeftHandMiddle3")}, false},
		{TEXT("leftRingProximal"), {TEXT("LeftHandRing1")}, false},
		{TEXT("leftRingIntermediate"), {TEXT("LeftHandRing2")}, false},
		{TEXT("leftRingDistal"), {TEXT("LeftHandRing3")}, false},
		{TEXT("leftLittleProximal"), {TEXT("LeftHandPinky1")}, false},
		{TEXT("leftLittleIntermediate"), {TEXT("LeftHandPinky2")}, false},
		{TEXT("leftLittleDistal"), {TEXT("LeftHandPinky3")}, false},

		{TEXT("rightThumbProximal"), {TEXT("RightHandThumb1")}, false},
		{TEXT("rightThumbIntermediate"), {TEXT("RightHandThumb2")}, false},
		{TEXT("rightThumbDistal"), {TEXT("RightHandThumb3")}, false},
		{TEXT("rightIndexProximal"), {TEXT("RightHandIndex1")}, false},
		{TEXT("rightIndexIntermediate"), {TEXT("RightHandIndex2")}, false},
		{TEXT("rightIndexDistal"), {TEXT("RightHandIndex3")}, false},
		{TEXT("rightMiddleProximal"), {TEXT("RightHandMiddle1")}, false},
		{TEXT("rightMiddleIntermediate"), {TEXT("RightHandMiddle2")}, false},
		{TEXT("rightMiddleDistal"), {TEXT("RightHandMiddle3")}, false},
		{TEXT("rightRingProximal"), {TEXT("RightHandRing1")}, false},
		{TEXT("rightRingIntermediate"), {TEXT("RightHandRing2")}, false},
		{TEXT("rightRingDistal"), {TEXT("RightHandRing3")}, false},
		{TEXT("rightLittleProximal"), {TEXT("RightHandPinky1")}, false},
		{TEXT("rightLittleIntermediate"), {TEXT("RightHandPinky2")}, false},
		{TEXT("rightLittleDistal"), {TEXT("RightHandPinky3")}, false},
	};

	return ApplyBoneMap(InMetaObject, AvailableBones, BoneMap, TEXT("Mixamo"));
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

	InMetaObject->humanoidBoneTable.Empty();

	TSet<FName> AvailableBones;
	if (!BuildAvailableBoneSet(InSkeletalMesh, TEXT("PopulateForMetaHuman"), AvailableBones))
	{
		return false;
	}

	// Two disjoint skeletons (standard/Mixamo-like vs Epic); kept as separate
	// tables rather than merged candidates to preserve Head vs head casing.
	const bool bIsStandardMetaHuman = AvailableBones.Contains(FName("Hips")) &&
		AvailableBones.Contains(FName("Spine1")) &&
		AvailableBones.Contains(FName("LeftArm"));

	TArray<FVrmBoneMapEntry> BoneMap;
	if (bIsStandardMetaHuman)
	{
		UE_LOG(LogTemp, Log, TEXT("Using standard MetaHuman (Mixamo-like) naming convention"));
		BoneMap = {
			{TEXT("hips"), {TEXT("Hips")}, true},
			{TEXT("spine"), {TEXT("Spine")}, true},
			{TEXT("chest"), {TEXT("Spine2")}, true},
			{TEXT("neck"), {TEXT("Neck")}, true},
			{TEXT("head"), {TEXT("Head")}, true},

			{TEXT("leftShoulder"), {TEXT("LeftShoulder")}, true},
			{TEXT("leftUpperArm"), {TEXT("LeftArm")}, true},
			{TEXT("leftLowerArm"), {TEXT("LeftForeArm")}, true},
			{TEXT("leftHand"), {TEXT("LeftHand")}, true},

			{TEXT("rightShoulder"), {TEXT("RightShoulder")}, true},
			{TEXT("rightUpperArm"), {TEXT("RightArm")}, true},
			{TEXT("rightLowerArm"), {TEXT("RightForeArm")}, true},
			{TEXT("rightHand"), {TEXT("RightHand")}, true},

			{TEXT("leftUpperLeg"), {TEXT("LeftUpLeg")}, true},
			{TEXT("leftLowerLeg"), {TEXT("LeftLeg")}, true},
			{TEXT("leftFoot"), {TEXT("LeftFoot")}, true},
			{TEXT("leftToes"), {TEXT("LeftToeBase")}, false},

			{TEXT("rightUpperLeg"), {TEXT("RightUpLeg")}, true},
			{TEXT("rightLowerLeg"), {TEXT("RightLeg")}, true},
			{TEXT("rightFoot"), {TEXT("RightFoot")}, true},
			{TEXT("rightToes"), {TEXT("RightToeBase")}, false},

			{TEXT("leftThumbProximal"), {TEXT("LeftHandThumb1")}, false},
			{TEXT("leftThumbIntermediate"), {TEXT("LeftHandThumb2")}, false},
			{TEXT("leftThumbDistal"), {TEXT("LeftHandThumb3")}, false},
			{TEXT("leftIndexProximal"), {TEXT("LeftHandIndex1")}, false},
			{TEXT("leftIndexIntermediate"), {TEXT("LeftHandIndex2")}, false},
			{TEXT("leftIndexDistal"), {TEXT("LeftHandIndex3")}, false},
			{TEXT("leftMiddleProximal"), {TEXT("LeftHandMiddle1")}, false},
			{TEXT("leftMiddleIntermediate"), {TEXT("LeftHandMiddle2")}, false},
			{TEXT("leftMiddleDistal"), {TEXT("LeftHandMiddle3")}, false},
			{TEXT("leftRingProximal"), {TEXT("LeftHandRing1")}, false},
			{TEXT("leftRingIntermediate"), {TEXT("LeftHandRing2")}, false},
			{TEXT("leftRingDistal"), {TEXT("LeftHandRing3")}, false},
			{TEXT("leftLittleProximal"), {TEXT("LeftHandPinky1")}, false},
			{TEXT("leftLittleIntermediate"), {TEXT("LeftHandPinky2")}, false},
			{TEXT("leftLittleDistal"), {TEXT("LeftHandPinky3")}, false},

			{TEXT("rightThumbProximal"), {TEXT("RightHandThumb1")}, false},
			{TEXT("rightThumbIntermediate"), {TEXT("RightHandThumb2")}, false},
			{TEXT("rightThumbDistal"), {TEXT("RightHandThumb3")}, false},
			{TEXT("rightIndexProximal"), {TEXT("RightHandIndex1")}, false},
			{TEXT("rightIndexIntermediate"), {TEXT("RightHandIndex2")}, false},
			{TEXT("rightIndexDistal"), {TEXT("RightHandIndex3")}, false},
			{TEXT("rightMiddleProximal"), {TEXT("RightHandMiddle1")}, false},
			{TEXT("rightMiddleIntermediate"), {TEXT("RightHandMiddle2")}, false},
			{TEXT("rightMiddleDistal"), {TEXT("RightHandMiddle3")}, false},
			{TEXT("rightRingProximal"), {TEXT("RightHandRing1")}, false},
			{TEXT("rightRingIntermediate"), {TEXT("RightHandRing2")}, false},
			{TEXT("rightRingDistal"), {TEXT("RightHandRing3")}, false},
			{TEXT("rightLittleProximal"), {TEXT("RightHandPinky1")}, false},
			{TEXT("rightLittleIntermediate"), {TEXT("RightHandPinky2")}, false},
			{TEXT("rightLittleDistal"), {TEXT("RightHandPinky3")}, false},

			{TEXT("leftEye"), {TEXT("LeftEye")}, false},
			{TEXT("rightEye"), {TEXT("RightEye")}, false},
		};
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("Using Epic skeleton naming convention for MetaHuman"));
		BoneMap = {
			{TEXT("hips"), {TEXT("pelvis")}, true},
			{TEXT("spine"), {TEXT("spine_01")}, true},
			{TEXT("chest"), {TEXT("spine_03")}, true},
			{TEXT("neck"), {TEXT("neck_01")}, true},
			{TEXT("head"), {TEXT("head")}, true},

			{TEXT("leftShoulder"), {TEXT("clavicle_l")}, true},
			{TEXT("leftUpperArm"), {TEXT("upperarm_l")}, true},
			{TEXT("leftLowerArm"), {TEXT("lowerarm_l")}, true},
			{TEXT("leftHand"), {TEXT("hand_l")}, true},

			{TEXT("rightShoulder"), {TEXT("clavicle_r")}, true},
			{TEXT("rightUpperArm"), {TEXT("upperarm_r")}, true},
			{TEXT("rightLowerArm"), {TEXT("lowerarm_r")}, true},
			{TEXT("rightHand"), {TEXT("hand_r")}, true},

			{TEXT("leftUpperLeg"), {TEXT("thigh_l")}, true},
			{TEXT("leftLowerLeg"), {TEXT("calf_l")}, true},
			{TEXT("leftFoot"), {TEXT("foot_l")}, true},
			{TEXT("leftToes"), {TEXT("ball_l")}, false},

			{TEXT("rightUpperLeg"), {TEXT("thigh_r")}, true},
			{TEXT("rightLowerLeg"), {TEXT("calf_r")}, true},
			{TEXT("rightFoot"), {TEXT("foot_r")}, true},
			{TEXT("rightToes"), {TEXT("ball_r")}, false},

			{TEXT("leftThumbProximal"), {TEXT("thumb_01_l")}, false},
			{TEXT("leftThumbIntermediate"), {TEXT("thumb_02_l")}, false},
			{TEXT("leftThumbDistal"), {TEXT("thumb_03_l")}, false},
			{TEXT("leftIndexProximal"), {TEXT("index_01_l")}, false},
			{TEXT("leftIndexIntermediate"), {TEXT("index_02_l")}, false},
			{TEXT("leftIndexDistal"), {TEXT("index_03_l")}, false},
			{TEXT("leftMiddleProximal"), {TEXT("middle_01_l")}, false},
			{TEXT("leftMiddleIntermediate"), {TEXT("middle_02_l")}, false},
			{TEXT("leftMiddleDistal"), {TEXT("middle_03_l")}, false},
			{TEXT("leftRingProximal"), {TEXT("ring_01_l")}, false},
			{TEXT("leftRingIntermediate"), {TEXT("ring_02_l")}, false},
			{TEXT("leftRingDistal"), {TEXT("ring_03_l")}, false},
			{TEXT("leftLittleProximal"), {TEXT("pinky_01_l")}, false},
			{TEXT("leftLittleIntermediate"), {TEXT("pinky_02_l")}, false},
			{TEXT("leftLittleDistal"), {TEXT("pinky_03_l")}, false},

			{TEXT("rightThumbProximal"), {TEXT("thumb_01_r")}, false},
			{TEXT("rightThumbIntermediate"), {TEXT("thumb_02_r")}, false},
			{TEXT("rightThumbDistal"), {TEXT("thumb_03_r")}, false},
			{TEXT("rightIndexProximal"), {TEXT("index_01_r")}, false},
			{TEXT("rightIndexIntermediate"), {TEXT("index_02_r")}, false},
			{TEXT("rightIndexDistal"), {TEXT("index_03_r")}, false},
			{TEXT("rightMiddleProximal"), {TEXT("middle_01_r")}, false},
			{TEXT("rightMiddleIntermediate"), {TEXT("middle_02_r")}, false},
			{TEXT("rightMiddleDistal"), {TEXT("middle_03_r")}, false},
			{TEXT("rightRingProximal"), {TEXT("ring_01_r")}, false},
			{TEXT("rightRingIntermediate"), {TEXT("ring_02_r")}, false},
			{TEXT("rightRingDistal"), {TEXT("ring_03_r")}, false},
			{TEXT("rightLittleProximal"), {TEXT("pinky_01_r")}, false},
			{TEXT("rightLittleIntermediate"), {TEXT("pinky_02_r")}, false},
			{TEXT("rightLittleDistal"), {TEXT("pinky_03_r")}, false},

			{TEXT("leftEye"), {TEXT("eye_l")}, false},
			{TEXT("rightEye"), {TEXT("eye_r")}, false},
		};
	}

	return ApplyBoneMap(InMetaObject, AvailableBones, BoneMap, TEXT("MetaHuman"));
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

	InMetaObject->humanoidBoneTable.Empty();

	TSet<FName> AvailableBones;
	if (!BuildAvailableBoneSet(InSkeletalMesh, TEXT("PopulateForDAZ"), AvailableBones))
	{
		return false;
	}

	// Genesis 8 names first, older/underscore exports as fallbacks; first present wins.
	const TArray<FVrmBoneMapEntry> BoneMap = {
		{TEXT("hips"), {TEXT("pelvis"), TEXT("hip")}, true},
		{TEXT("spine"), {TEXT("abdomenLower"), TEXT("abdomen")}, true},
		{TEXT("chest"), {TEXT("chestUpper"), TEXT("chest"), TEXT("chest1")}, true},
		{TEXT("neck"), {TEXT("neckLower"), TEXT("neck")}, true},
		{TEXT("head"), {TEXT("head")}, true},

		{TEXT("leftShoulder"), {TEXT("lCollar"), TEXT("l_clavicle")}, true},
		{TEXT("leftUpperArm"), {TEXT("lShldrBend"), TEXT("lShldr"), TEXT("l_upperarm")}, true},
		{TEXT("leftLowerArm"), {TEXT("lForearmBend"), TEXT("lForeArm"), TEXT("l_forearm")}, true},
		{TEXT("leftHand"), {TEXT("lHand")}, true},

		{TEXT("rightShoulder"), {TEXT("rCollar"), TEXT("r_clavicle")}, true},
		{TEXT("rightUpperArm"), {TEXT("rShldrBend"), TEXT("rShldr"), TEXT("r_upperarm")}, true},
		{TEXT("rightLowerArm"), {TEXT("rForearmBend"), TEXT("rForeArm"), TEXT("r_forearm")}, true},
		{TEXT("rightHand"), {TEXT("rHand")}, true},

		{TEXT("leftUpperLeg"), {TEXT("lThighBend"), TEXT("lThigh"), TEXT("l_thigh")}, true},
		{TEXT("leftLowerLeg"), {TEXT("lShin"), TEXT("l_calf")}, true},
		{TEXT("leftFoot"), {TEXT("lFoot")}, true},
		{TEXT("leftToes"), {TEXT("lToe")}, false},

		{TEXT("rightUpperLeg"), {TEXT("rThighBend"), TEXT("rThigh"), TEXT("r_thigh")}, true},
		{TEXT("rightLowerLeg"), {TEXT("rShin"), TEXT("r_calf")}, true},
		{TEXT("rightFoot"), {TEXT("rFoot")}, true},
		{TEXT("rightToes"), {TEXT("rToe")}, false},

		{TEXT("leftThumbProximal"), {TEXT("lThumb1")}, false},
		{TEXT("leftThumbIntermediate"), {TEXT("lThumb2")}, false},
		{TEXT("leftThumbDistal"), {TEXT("lThumb3")}, false},
		{TEXT("leftIndexProximal"), {TEXT("lIndex1")}, false},
		{TEXT("leftIndexIntermediate"), {TEXT("lIndex2")}, false},
		{TEXT("leftIndexDistal"), {TEXT("lIndex3")}, false},
		{TEXT("leftMiddleProximal"), {TEXT("lMid1")}, false},
		{TEXT("leftMiddleIntermediate"), {TEXT("lMid2")}, false},
		{TEXT("leftMiddleDistal"), {TEXT("lMid3")}, false},
		{TEXT("leftRingProximal"), {TEXT("lRing1")}, false},
		{TEXT("leftRingIntermediate"), {TEXT("lRing2")}, false},
		{TEXT("leftRingDistal"), {TEXT("lRing3")}, false},
		{TEXT("leftLittleProximal"), {TEXT("lPinky1")}, false},
		{TEXT("leftLittleIntermediate"), {TEXT("lPinky2")}, false},
		{TEXT("leftLittleDistal"), {TEXT("lPinky3")}, false},

		{TEXT("rightThumbProximal"), {TEXT("rThumb1")}, false},
		{TEXT("rightThumbIntermediate"), {TEXT("rThumb2")}, false},
		{TEXT("rightThumbDistal"), {TEXT("rThumb3")}, false},
		{TEXT("rightIndexProximal"), {TEXT("rIndex1")}, false},
		{TEXT("rightIndexIntermediate"), {TEXT("rIndex2")}, false},
		{TEXT("rightIndexDistal"), {TEXT("rIndex3")}, false},
		{TEXT("rightMiddleProximal"), {TEXT("rMid1")}, false},
		{TEXT("rightMiddleIntermediate"), {TEXT("rMid2")}, false},
		{TEXT("rightMiddleDistal"), {TEXT("rMid3")}, false},
		{TEXT("rightRingProximal"), {TEXT("rRing1")}, false},
		{TEXT("rightRingIntermediate"), {TEXT("rRing2")}, false},
		{TEXT("rightRingDistal"), {TEXT("rRing3")}, false},
		{TEXT("rightLittleProximal"), {TEXT("rPinky1")}, false},
		{TEXT("rightLittleIntermediate"), {TEXT("rPinky2")}, false},
		{TEXT("rightLittleDistal"), {TEXT("rPinky3")}, false},

		{TEXT("leftEye"), {TEXT("lEye")}, false},
		{TEXT("rightEye"), {TEXT("rEye")}, false},
	};

	return ApplyBoneMap(InMetaObject, AvailableBones, BoneMap, TEXT("DAZ"));
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

namespace
{
	void ShowAutoPopulateToast(const FText& Message, bool bSuccess, float ExpireSeconds)
	{
		FNotificationInfo Info(Message);
		Info.bUseLargeFont = false;
		Info.bUseSuccessFailIcons = true;
		Info.ExpireDuration = ExpireSeconds;
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}
}

FVrmAutoPopulateUiResult UAutoPopulateVrmMeta::AutoPopulateWithUi(UVrmMetaObject* MetaObject, bool bShowAssignReminder)
{
	FVrmAutoPopulateUiResult Result;

	if (MetaObject == nullptr || MetaObject->SkeletalMesh == nullptr)
	{
		ShowAutoPopulateToast(
			NSLOCTEXT("AutoPopulateVrmMeta", "NoSkeletalMesh", "Auto-Populate: the Vrm Meta Object has no SkeletalMesh assigned."),
			false, 5.0f);
		return Result;
	}

	// Resolve the display type name: an explicit SkeletonType on the meta wins;
	// Auto falls back to detection (and fails loudly on Unknown - the populate
	// itself would also fail, but this names the actual problem).
	bool bAutoDetected = false;
	switch (MetaObject->SkeletonType)
	{
	case EVrmSkeletonType::VRM:       Result.TypeName = TEXT("VRM"); break;
	case EVrmSkeletonType::Mixamo:    Result.TypeName = TEXT("Mixamo"); break;
	case EVrmSkeletonType::MetaHuman: Result.TypeName = TEXT("MetaHuman"); break;
	case EVrmSkeletonType::DAZ:       Result.TypeName = TEXT("DAZ"); break;
	case EVrmSkeletonType::Auto:
	default:
	{
		bAutoDetected = true;
		switch (DetectSkeletonType(MetaObject->SkeletalMesh))
		{
		case ESkeletonType::VRM:       Result.TypeName = TEXT("VRM"); break;
		case ESkeletonType::Mixamo:    Result.TypeName = TEXT("Mixamo"); break;
		case ESkeletonType::MetaHuman: Result.TypeName = TEXT("MetaHuman"); break;
		case ESkeletonType::DAZ:       Result.TypeName = TEXT("DAZ"); break;
		default:
			ShowAutoPopulateToast(
				NSLOCTEXT("AutoPopulateVrmMeta", "UnknownSkeleton",
					"Auto-Populate: could not detect the skeleton type. Set SkeletonType explicitly on the Vrm Meta Object."),
				false, 5.0f);
			return Result;
		}
		break;
	}
	}

	// Auto-populate edits an asset, so wrap the mutation in a transaction
	// (Modify() before) and flush change/dirty state after, otherwise the edit
	// is neither undoable nor saved.
	const FScopedTransaction Transaction(
		NSLOCTEXT("AutoPopulateVrmMeta", "AutoPopulateCtx", "Auto-Populate VRM Bone Mappings"));
	MetaObject->Modify();

	Result.bSuccess = AutoPopulateMetaObject(MetaObject, MetaObject->SkeletalMesh);

	MetaObject->PostEditChange();
	MetaObject->MarkPackageDirty();

	Result.MappedBones = MetaObject->humanoidBoneTable.Num();

	if (Result.bSuccess)
	{
		FText Message = FText::Format(
			bAutoDetected
				? NSLOCTEXT("AutoPopulateVrmMeta", "PopulateOkAuto", "Auto-Populate: mapped {0} bones for the auto-detected {1} skeleton.")
				: NSLOCTEXT("AutoPopulateVrmMeta", "PopulateOkExplicit", "Auto-Populate: mapped {0} bones for the {1} skeleton."),
			Result.MappedBones, FText::FromString(Result.TypeName));
		if (bShowAssignReminder)
		{
			// The easy-to-miss next step: a populated asset does nothing until it
			// is assigned to the VRM VMC node (auto-search finds VRM meshes only).
			Message = FText::Format(
				NSLOCTEXT("AutoPopulateVrmMeta", "PopulateAssignReminder",
					"{0}\nNext: assign this asset to your VRM VMC node's 'Vrm Meta Object' (auto-search finds VRM meshes only), then restart PIE."),
				Message);
		}
		else
		{
			Message = FText::Format(
				NSLOCTEXT("AutoPopulateVrmMeta", "PopulateRestartReminder", "{0}\nRestart PIE to apply."),
				Message);
		}
		ShowAutoPopulateToast(Message, true, 8.0f);
	}
	else
	{
		ShowAutoPopulateToast(
			FText::Format(
				NSLOCTEXT("AutoPopulateVrmMeta", "PopulateFail",
					"Auto-Populate: failed to map the {0} skeleton's bones. See the Output Log for details."),
				FText::FromString(Result.TypeName)),
			false, 5.0f);
	}

	return Result;
}
