#include "VrmVMCDebugStyle.h"

const TArray<FVrmVMCDebugStyle::FBoneGroup>& FVrmVMCDebugStyle::GetBoneGroups()
{
	static const TArray<FBoneGroup> Groups = {
		{
			TEXT("Spine & Head"),
			{
				TEXT("hips"), TEXT("spine"), TEXT("chest"), TEXT("upperChest"),
				TEXT("neck"), TEXT("head"), TEXT("jaw"),
				TEXT("leftEye"), TEXT("rightEye")
			}
		},
		{
			TEXT("Left Arm"),
			{
				TEXT("leftShoulder"), TEXT("leftUpperArm"),
				TEXT("leftLowerArm"), TEXT("leftHand")
			}
		},
		{
			TEXT("Right Arm"),
			{
				TEXT("rightShoulder"), TEXT("rightUpperArm"),
				TEXT("rightLowerArm"), TEXT("rightHand")
			}
		},
		{
			TEXT("Left Leg"),
			{
				TEXT("leftUpperLeg"), TEXT("leftLowerLeg"),
				TEXT("leftFoot"), TEXT("leftToes")
			}
		},
		{
			TEXT("Right Leg"),
			{
				TEXT("rightUpperLeg"), TEXT("rightLowerLeg"),
				TEXT("rightFoot"), TEXT("rightToes")
			}
		},
		{
			TEXT("Left Fingers"),
			{
				TEXT("leftThumbProximal"), TEXT("leftThumbIntermediate"), TEXT("leftThumbDistal"),
				TEXT("leftIndexProximal"), TEXT("leftIndexIntermediate"), TEXT("leftIndexDistal"),
				TEXT("leftMiddleProximal"), TEXT("leftMiddleIntermediate"), TEXT("leftMiddleDistal"),
				TEXT("leftRingProximal"), TEXT("leftRingIntermediate"), TEXT("leftRingDistal"),
				TEXT("leftLittleProximal"), TEXT("leftLittleIntermediate"), TEXT("leftLittleDistal")
			}
		},
		{
			TEXT("Right Fingers"),
			{
				TEXT("rightThumbProximal"), TEXT("rightThumbIntermediate"), TEXT("rightThumbDistal"),
				TEXT("rightIndexProximal"), TEXT("rightIndexIntermediate"), TEXT("rightIndexDistal"),
				TEXT("rightMiddleProximal"), TEXT("rightMiddleIntermediate"), TEXT("rightMiddleDistal"),
				TEXT("rightRingProximal"), TEXT("rightRingIntermediate"), TEXT("rightRingDistal"),
				TEXT("rightLittleProximal"), TEXT("rightLittleIntermediate"), TEXT("rightLittleDistal")
			}
		}
	};
	return Groups;
}

const TSet<FString>& FVrmVMCDebugStyle::GetPerfectSyncCurveSet()
{
	// ARKit-derived blendshape set used by VMC senders that support Perfect Sync.
	// Includes the 52 standard ARKit shapes plus the common VRoid extension shapes.
	static const TSet<FString> Set = {
		TEXT("BrowDownLeft"), TEXT("BrowDownRight"),
		TEXT("BrowInnerUp"), TEXT("BrowOuterUpLeft"), TEXT("BrowOuterUpRight"),
		TEXT("CheekPuff"), TEXT("CheekSquintLeft"), TEXT("CheekSquintRight"),
		TEXT("EyeBlinkLeft"), TEXT("EyeBlinkRight"),
		TEXT("EyeLookDownLeft"), TEXT("EyeLookDownRight"),
		TEXT("EyeLookInLeft"), TEXT("EyeLookInRight"),
		TEXT("EyeLookOutLeft"), TEXT("EyeLookOutRight"),
		TEXT("EyeLookUpLeft"), TEXT("EyeLookUpRight"),
		TEXT("EyeSquintLeft"), TEXT("EyeSquintRight"),
		TEXT("EyeWideLeft"), TEXT("EyeWideRight"),
		TEXT("JawForward"), TEXT("JawLeft"), TEXT("JawOpen"), TEXT("JawRight"),
		TEXT("MouthClose"), TEXT("MouthDimpleLeft"), TEXT("MouthDimpleRight"),
		TEXT("MouthFrownLeft"), TEXT("MouthFrownRight"),
		TEXT("MouthFunnel"), TEXT("MouthLeft"),
		TEXT("MouthLowerDownLeft"), TEXT("MouthLowerDownRight"),
		TEXT("MouthPressLeft"), TEXT("MouthPressRight"),
		TEXT("MouthPucker"), TEXT("MouthRight"),
		TEXT("MouthRollLower"), TEXT("MouthRollUpper"),
		TEXT("MouthShrugLower"), TEXT("MouthShrugUpper"),
		TEXT("MouthSmileLeft"), TEXT("MouthSmileRight"),
		TEXT("MouthStretchLeft"), TEXT("MouthStretchRight"),
		TEXT("MouthUpperUpLeft"), TEXT("MouthUpperUpRight"),
		TEXT("NoseSneerLeft"), TEXT("NoseSneerRight"),
		TEXT("TongueOut")
	};
	return Set;
}

FString FVrmVMCDebugStyle::GetMirrorName(const FString& HumanoidName)
{
	if (HumanoidName.StartsWith(TEXT("left")))
	{
		return TEXT("right") + HumanoidName.RightChop(4);
	}
	if (HumanoidName.StartsWith(TEXT("right")))
	{
		return TEXT("left") + HumanoidName.RightChop(5);
	}
	return FString();
}

// Color choices below match the FAppStyle warning/error/success palette so
// they read correctly across light and dark editor themes without us baking
// in absolute hex values.

FSlateColor FVrmVMCDebugStyle::GetLiveTextColor()
{
	return FSlateColor::UseForeground();
}

FSlateColor FVrmVMCDebugStyle::GetMaskedTextColor()
{
	return FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f, 1.0f));
}

FSlateColor FVrmVMCDebugStyle::GetPreOverrideBadgeColor()
{
	return FSlateColor(FLinearColor(0.95f, 0.75f, 0.20f, 1.0f));
}

FSlateColor FVrmVMCDebugStyle::GetPostOverrideBadgeColor()
{
	return FSlateColor(FLinearColor(0.95f, 0.50f, 0.10f, 1.0f));
}

FSlateColor FVrmVMCDebugStyle::GetMaskedBadgeColor()
{
	return FSlateColor(FLinearColor(0.90f, 0.30f, 0.30f, 1.0f));
}

FSlateColor FVrmVMCDebugStyle::GetInactiveStreamColor()
{
	return FSlateColor(FLinearColor(0.40f, 0.40f, 0.40f, 1.0f));
}

FSlateColor FVrmVMCDebugStyle::GetConnectionGoodColor()
{
	return FSlateColor(FLinearColor(0.30f, 0.85f, 0.40f, 1.0f));
}

FSlateColor FVrmVMCDebugStyle::GetConnectionStaleColor()
{
	return FSlateColor(FLinearColor(0.95f, 0.75f, 0.20f, 1.0f));
}

FSlateColor FVrmVMCDebugStyle::GetConnectionDeadColor()
{
	return FSlateColor(FLinearColor(0.85f, 0.30f, 0.30f, 1.0f));
}
