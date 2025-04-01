// VRM4U Copyright (c) 2021-2024 Haruyoshi Yamamoto. This software is released under the MIT License.

#include "AutoPopulateVrmMeta.h"
#include "Animation/Skeleton.h"
#include "Misc/EngineVersionComparison.h"
#include "VrmMetaObject.h"
#include "VrmUtil.h"

ESkeletonType UAutoPopulateVrmMeta::DetectSkeletonType(USkeletalMesh* InSkeletalMesh)
{
    if (!InSkeletalMesh)
    {
        return ESkeletonType::Unknown;
    }

    USkeleton* Skeleton = VRMGetSkeleton(InSkeletalMesh);
    if (!Skeleton)
    {
        return ESkeletonType::Unknown;
    }

    // Get bone names to detect skeleton type
    const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
    TArray<FName> BoneNames;
    for (int32 i = 0; i < RefSkeleton.GetNum(); ++i)
    {
        BoneNames.Add(RefSkeleton.GetBoneName(i));
    }

    // Check for MetaHuman specific bones
    if (BoneNames.Contains(FName("spine_01")) && 
        BoneNames.Contains(FName("neck_01")) && 
        BoneNames.Contains(FName("clavicle_l")))
    {
        return ESkeletonType::MetaHuman;
    }

    // Check for Mixamo specific bones
    if (BoneNames.Contains(FName("Hips")) && 
        BoneNames.Contains(FName("Spine")) && 
        BoneNames.Contains(FName("LeftArm")))
    {
        return ESkeletonType::Mixamo;
    }

    // Check for DAZ specific bones (add your detection logic here)
    // ...

    // Check for VRM specific bones
    if (BoneNames.Contains(FName("J_Bip_C_Hips")) || 
        BoneNames.Contains(FName("vrm_hips")))
    {
        return ESkeletonType::VRM;
    }

    return ESkeletonType::Unknown;
}

bool UAutoPopulateVrmMeta::AutoPopulateMetaObject(UVrmMetaObject* InMetaObject, USkeletalMesh* InSkeletalMesh)
{
    if (!InMetaObject || !InSkeletalMesh)
    {
        return false;
    }

    // Set the skeletal mesh first
    InMetaObject->SkeletalMesh = InSkeletalMesh;

    // Detect skeleton type and call appropriate population method
    ESkeletonType SkeletonType = DetectSkeletonType(InSkeletalMesh);
    
    switch (SkeletonType)
    {
    case ESkeletonType::Mixamo:
        return PopulateForMixamo(InMetaObject, InSkeletalMesh);
    case ESkeletonType::MetaHuman:
        return PopulateForMetaHuman(InMetaObject, InSkeletalMesh);
    case ESkeletonType::DAZ:
        return PopulateForDAZ(InMetaObject, InSkeletalMesh);
    case ESkeletonType::VRM:
        // VRM already works natively, no need to populate
        return true;
    default:
        return false;
    }
}

bool UAutoPopulateVrmMeta::PopulateForMixamo(UVrmMetaObject* InMetaObject, USkeletalMesh* InSkeletalMesh)
{
    if (!InMetaObject)
    {
        return false;
    }

    // Clear existing mappings
    InMetaObject->humanoidBoneTable.Empty();

    // Add Mixamo bone mappings
    InMetaObject->humanoidBoneTable.Add(TEXT("hips"), TEXT("Hips"));
    InMetaObject->humanoidBoneTable.Add(TEXT("spine"), TEXT("Spine"));
    InMetaObject->humanoidBoneTable.Add(TEXT("chest"), TEXT("Spine2"));
    InMetaObject->humanoidBoneTable.Add(TEXT("neck"), TEXT("Neck"));
    InMetaObject->humanoidBoneTable.Add(TEXT("head"), TEXT("Head"));
    
    // Left arm
    InMetaObject->humanoidBoneTable.Add(TEXT("leftShoulder"), TEXT("LeftShoulder"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftUpperArm"), TEXT("LeftArm"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftLowerArm"), TEXT("LeftForeArm"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftHand"), TEXT("LeftHand"));
    
    // Right arm
    InMetaObject->humanoidBoneTable.Add(TEXT("rightShoulder"), TEXT("RightShoulder"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightUpperArm"), TEXT("RightArm"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightLowerArm"), TEXT("RightForeArm"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightHand"), TEXT("RightHand"));
    
    // Left leg
    InMetaObject->humanoidBoneTable.Add(TEXT("leftUpperLeg"), TEXT("LeftUpLeg"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftLowerLeg"), TEXT("LeftLeg"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftFoot"), TEXT("LeftFoot"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftToes"), TEXT("LeftToeBase"));
    
    // Right leg
    InMetaObject->humanoidBoneTable.Add(TEXT("rightUpperLeg"), TEXT("RightUpLeg"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightLowerLeg"), TEXT("RightLeg"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightFoot"), TEXT("RightFoot"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightToes"), TEXT("RightToeBase"));
    
    // Left fingers
    InMetaObject->humanoidBoneTable.Add(TEXT("leftThumbProximal"), TEXT("LeftHandThumb1"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftThumbIntermediate"), TEXT("LeftHandThumb2"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftThumbDistal"), TEXT("LeftHandThumb3"));
    
    InMetaObject->humanoidBoneTable.Add(TEXT("leftIndexProximal"), TEXT("LeftHandIndex1"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftIndexIntermediate"), TEXT("LeftHandIndex2"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftIndexDistal"), TEXT("LeftHandIndex3"));
    
    InMetaObject->humanoidBoneTable.Add(TEXT("leftMiddleProximal"), TEXT("LeftHandMiddle1"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftMiddleIntermediate"), TEXT("LeftHandMiddle2"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftMiddleDistal"), TEXT("LeftHandMiddle3"));
    
    InMetaObject->humanoidBoneTable.Add(TEXT("leftRingProximal"), TEXT("LeftHandRing1"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftRingIntermediate"), TEXT("LeftHandRing2"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftRingDistal"), TEXT("LeftHandRing3"));
    
    InMetaObject->humanoidBoneTable.Add(TEXT("leftLittleProximal"), TEXT("LeftHandPinky1"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftLittleIntermediate"), TEXT("LeftHandPinky2"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftLittleDistal"), TEXT("LeftHandPinky3"));
    
    // Right fingers
    InMetaObject->humanoidBoneTable.Add(TEXT("rightThumbProximal"), TEXT("RightHandThumb1"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightThumbIntermediate"), TEXT("RightHandThumb2"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightThumbDistal"), TEXT("RightHandThumb3"));
    
    InMetaObject->humanoidBoneTable.Add(TEXT("rightIndexProximal"), TEXT("RightHandIndex1"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightIndexIntermediate"), TEXT("RightHandIndex2"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightIndexDistal"), TEXT("RightHandIndex3"));
    
    InMetaObject->humanoidBoneTable.Add(TEXT("rightMiddleProximal"), TEXT("RightHandMiddle1"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightMiddleIntermediate"), TEXT("RightHandMiddle2"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightMiddleDistal"), TEXT("RightHandMiddle3"));
    
    InMetaObject->humanoidBoneTable.Add(TEXT("rightRingProximal"), TEXT("RightHandRing1"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightRingIntermediate"), TEXT("RightHandRing2"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightRingDistal"), TEXT("RightHandRing3"));
    
    InMetaObject->humanoidBoneTable.Add(TEXT("rightLittleProximal"), TEXT("RightHandPinky1"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightLittleIntermediate"), TEXT("RightHandPinky2"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightLittleDistal"), TEXT("RightHandPinky3"));

    return true;
}

bool UAutoPopulateVrmMeta::PopulateForMetaHuman(UVrmMetaObject* InMetaObject, USkeletalMesh* InSkeletalMesh)
{
    if (!InMetaObject)
    {
        return false;
    }

    // Clear existing mappings
    InMetaObject->humanoidBoneTable.Empty();

    // Add MetaHuman bone mappings
    InMetaObject->humanoidBoneTable.Add(TEXT("hips"), TEXT("pelvis"));
    InMetaObject->humanoidBoneTable.Add(TEXT("spine"), TEXT("spine_01"));
    InMetaObject->humanoidBoneTable.Add(TEXT("chest"), TEXT("spine_03"));
    InMetaObject->humanoidBoneTable.Add(TEXT("neck"), TEXT("neck_01"));
    InMetaObject->humanoidBoneTable.Add(TEXT("head"), TEXT("head"));
    
    // Left arm
    InMetaObject->humanoidBoneTable.Add(TEXT("leftShoulder"), TEXT("clavicle_l"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftUpperArm"), TEXT("upperarm_l"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftLowerArm"), TEXT("lowerarm_l"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftHand"), TEXT("hand_l"));
    
    // Right arm
    InMetaObject->humanoidBoneTable.Add(TEXT("rightShoulder"), TEXT("clavicle_r"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightUpperArm"), TEXT("upperarm_r"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightLowerArm"), TEXT("lowerarm_r"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightHand"), TEXT("hand_r"));
    
    // Left leg
    InMetaObject->humanoidBoneTable.Add(TEXT("leftUpperLeg"), TEXT("thigh_l"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftLowerLeg"), TEXT("calf_l"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftFoot"), TEXT("foot_l"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftToes"), TEXT("ball_l"));
    
    // Right leg
    InMetaObject->humanoidBoneTable.Add(TEXT("rightUpperLeg"), TEXT("thigh_r"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightLowerLeg"), TEXT("calf_r"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightFoot"), TEXT("foot_r"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightToes"), TEXT("ball_r"));
    
    // Left fingers (MetaHuman has a slightly different naming convention)
    InMetaObject->humanoidBoneTable.Add(TEXT("leftThumbProximal"), TEXT("thumb_01_l"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftThumbIntermediate"), TEXT("thumb_02_l"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftThumbDistal"), TEXT("thumb_03_l"));
    
    InMetaObject->humanoidBoneTable.Add(TEXT("leftIndexProximal"), TEXT("index_01_l"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftIndexIntermediate"), TEXT("index_02_l"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftIndexDistal"), TEXT("index_03_l"));
    
    InMetaObject->humanoidBoneTable.Add(TEXT("leftMiddleProximal"), TEXT("middle_01_l"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftMiddleIntermediate"), TEXT("middle_02_l"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftMiddleDistal"), TEXT("middle_03_l"));
    
    InMetaObject->humanoidBoneTable.Add(TEXT("leftRingProximal"), TEXT("ring_01_l"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftRingIntermediate"), TEXT("ring_02_l"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftRingDistal"), TEXT("ring_03_l"));
    
    InMetaObject->humanoidBoneTable.Add(TEXT("leftLittleProximal"), TEXT("pinky_01_l"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftLittleIntermediate"), TEXT("pinky_02_l"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftLittleDistal"), TEXT("pinky_03_l"));
    
    // Right fingers
    InMetaObject->humanoidBoneTable.Add(TEXT("rightThumbProximal"), TEXT("thumb_01_r"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightThumbIntermediate"), TEXT("thumb_02_r"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightThumbDistal"), TEXT("thumb_03_r"));
    
    InMetaObject->humanoidBoneTable.Add(TEXT("rightIndexProximal"), TEXT("index_01_r"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightIndexIntermediate"), TEXT("index_02_r"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightIndexDistal"), TEXT("index_03_r"));
    
    InMetaObject->humanoidBoneTable.Add(TEXT("rightMiddleProximal"), TEXT("middle_01_r"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightMiddleIntermediate"), TEXT("middle_02_r"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightMiddleDistal"), TEXT("middle_03_r"));
    
    InMetaObject->humanoidBoneTable.Add(TEXT("rightRingProximal"), TEXT("ring_01_r"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightRingIntermediate"), TEXT("ring_02_r"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightRingDistal"), TEXT("ring_03_r"));
    
    InMetaObject->humanoidBoneTable.Add(TEXT("rightLittleProximal"), TEXT("pinky_01_r"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightLittleIntermediate"), TEXT("pinky_02_r"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightLittleDistal"), TEXT("pinky_03_r"));

    return true;
}

bool UAutoPopulateVrmMeta::PopulateForDAZ(UVrmMetaObject* InMetaObject, USkeletalMesh* InSkeletalMesh)
{
    if (!InMetaObject)
    {
        return false;
    }

    // Clear existing mappings
    InMetaObject->humanoidBoneTable.Empty();

    // Add DAZ bone mappings - these names may need to be adjusted based on your specific DAZ export settings
    // Main body
    InMetaObject->humanoidBoneTable.Add(TEXT("hips"), TEXT("hip"));
    InMetaObject->humanoidBoneTable.Add(TEXT("spine"), TEXT("abdomen"));
    InMetaObject->humanoidBoneTable.Add(TEXT("chest"), TEXT("chest"));
    InMetaObject->humanoidBoneTable.Add(TEXT("neck"), TEXT("neck"));
    InMetaObject->humanoidBoneTable.Add(TEXT("head"), TEXT("head"));
    
    // Left arm
    InMetaObject->humanoidBoneTable.Add(TEXT("leftShoulder"), TEXT("lCollar"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftUpperArm"), TEXT("lShldr"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftLowerArm"), TEXT("lForeArm"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftHand"), TEXT("lHand"));
    
    // Right arm
    InMetaObject->humanoidBoneTable.Add(TEXT("rightShoulder"), TEXT("rCollar"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightUpperArm"), TEXT("rShldr"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightLowerArm"), TEXT("rForeArm"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightHand"), TEXT("rHand"));
    
    // Left leg
    InMetaObject->humanoidBoneTable.Add(TEXT("leftUpperLeg"), TEXT("lThigh"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftLowerLeg"), TEXT("lShin"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftFoot"), TEXT("lFoot"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftToes"), TEXT("lToe"));
    
    // Right leg
    InMetaObject->humanoidBoneTable.Add(TEXT("rightUpperLeg"), TEXT("rThigh"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightLowerLeg"), TEXT("rShin"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightFoot"), TEXT("rFoot"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightToes"), TEXT("rToe"));
    
    // Left fingers
    InMetaObject->humanoidBoneTable.Add(TEXT("leftThumbProximal"), TEXT("lThumb1"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftThumbIntermediate"), TEXT("lThumb2"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftThumbDistal"), TEXT("lThumb3"));
    
    InMetaObject->humanoidBoneTable.Add(TEXT("leftIndexProximal"), TEXT("lIndex1"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftIndexIntermediate"), TEXT("lIndex2"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftIndexDistal"), TEXT("lIndex3"));
    
    InMetaObject->humanoidBoneTable.Add(TEXT("leftMiddleProximal"), TEXT("lMid1"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftMiddleIntermediate"), TEXT("lMid2"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftMiddleDistal"), TEXT("lMid3"));
    
    InMetaObject->humanoidBoneTable.Add(TEXT("leftRingProximal"), TEXT("lRing1"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftRingIntermediate"), TEXT("lRing2"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftRingDistal"), TEXT("lRing3"));
    
    InMetaObject->humanoidBoneTable.Add(TEXT("leftLittleProximal"), TEXT("lPinky1"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftLittleIntermediate"), TEXT("lPinky2"));
    InMetaObject->humanoidBoneTable.Add(TEXT("leftLittleDistal"), TEXT("lPinky3"));
    
    // Right fingers
    InMetaObject->humanoidBoneTable.Add(TEXT("rightThumbProximal"), TEXT("rThumb1"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightThumbIntermediate"), TEXT("rThumb2"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightThumbDistal"), TEXT("rThumb3"));
    
    InMetaObject->humanoidBoneTable.Add(TEXT("rightIndexProximal"), TEXT("rIndex1"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightIndexIntermediate"), TEXT("rIndex2"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightIndexDistal"), TEXT("rIndex3"));
    
    InMetaObject->humanoidBoneTable.Add(TEXT("rightMiddleProximal"), TEXT("rMid1"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightMiddleIntermediate"), TEXT("rMid2"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightMiddleDistal"), TEXT("rMid3"));
    
    InMetaObject->humanoidBoneTable.Add(TEXT("rightRingProximal"), TEXT("rRing1"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightRingIntermediate"), TEXT("rRing2"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightRingDistal"), TEXT("rRing3"));
    
    InMetaObject->humanoidBoneTable.Add(TEXT("rightLittleProximal"), TEXT("rPinky1"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightLittleIntermediate"), TEXT("rPinky2"));
    InMetaObject->humanoidBoneTable.Add(TEXT("rightLittleDistal"), TEXT("rPinky3"));

    return true;
}