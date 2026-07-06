// VRM4U Copyright (c) 2021-2026 Haruyoshi Yamamoto. This software is released under the MIT License.

#include "Misc/EngineVersionComparison.h"

// FIKRetargetProcessor as a standalone value type is UE 5.6+ (earlier versions had the
// deprecated UIKRetargetProcessor UObject instead).
#if WITH_DEV_AUTOMATION_TESTS && !UE_VERSION_OLDER_THAN(5,6,0)

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"
#include "AnimationRuntime.h"
#include "Retargeter/IKRetargeter.h"
#include "Retargeter/IKRetargetProcessor.h"
#include "Retargeter/IKRetargetProfile.h"
#include "Retargeter/RetargetOps/FKChainsOp.h"

namespace
{
	// UE 5.7 removed FIKRetargetProcessor::ScaleSourcePose; source scaling now happens inside
	// RunRetargeter. Only UE 5.6 needs the explicit pre-scale before RunRetargeter.
	FORCEINLINE void ScaleSourcePoseCompat(FIKRetargetProcessor& Processor, TArray<FTransform>& InOutSourcePose)
	{
#if UE_VERSION_OLDER_THAN(5,7,0)
		Processor.ScaleSourcePose(InOutSourcePose);
#else
		(void)Processor;
		(void)InOutSourcePose;
#endif
	}
}

// Pose-replay regressions for the VMC -> IK Retargeter pipeline (docs/ik-retargeter-pipeline.md).
// Feeds synthetic poses on the SOURCE rig through the project's RTG asset via Epic's
// FIKRetargetProcessor (the same engine the RetargetPoseFromMesh anim node runs per frame) and
// asserts the TARGET arm behaves:
//   - ElbowBend: the target forearm/hand articulate at all (regression for the "limp forearm").
//   - ElbowFidelity: the target elbow's interior angle matches the source's, at rest and bent —
//     catches wrong-plane retarget-pose offsets and rotation-mode distortion that leave the arm
//     moving but bent differently than the source.
//
// The asset paths are project-specific (OWLVRM4U-Template). When they are absent the tests skip
// with a warning instead of failing, so the suite stays green in other host projects.

namespace VrmRetargetPoseReplayTest
{
	static const TCHAR* SourceMeshPath = TEXT("/Game/VTuber/Characters/VRM/SK_OWLVRM.SK_OWLVRM");
	static const TCHAR* TargetMeshPath = TEXT("/Game/MetaHumans/MetaHumanCharacter/Body/SKM_MetaHumanCharacter_BodyMesh.SKM_MetaHumanCharacter_BodyMesh");
	static const TCHAR* RetargeterPath = TEXT("/Game/VTuber/Characters/VRM/RTG_OWLVRM_to_MetaHuman.RTG_OWLVRM_to_MetaHuman");

	static const FName SourceShoulder(TEXT("J_Bip_L_UpperArm"));
	static const FName SourceElbow(TEXT("J_Bip_L_LowerArm"));
	static const FName SourceHand(TEXT("J_Bip_L_Hand"));
	static const FName SourceShoulderR(TEXT("J_Bip_R_UpperArm"));
	static const FName SourcePelvis(TEXT("J_Bip_C_Hips"));
	static const FName SourceHead(TEXT("J_Bip_C_Head"));
	static const FName TargetShoulder(TEXT("upperarm_l"));
	static const FName TargetElbow(TEXT("lowerarm_l"));
	static const FName TargetHand(TEXT("hand_l"));
	static const FName TargetShoulderR(TEXT("upperarm_r"));
	static const FName TargetPelvis(TEXT("pelvis"));
	static const FName TargetHead(TEXT("head"));

	// Component-space pose from a local pose, scale stripped as RunRetargeter requires.
	static void ToComponentSpaceUnitScale(const FReferenceSkeleton& RefSkel,
		const TArray<FTransform>& LocalPose, TArray<FTransform>& OutComponentPose)
	{
		FAnimationRuntime::FillUpComponentSpaceTransforms(RefSkel, LocalPose, OutComponentPose);
		for (FTransform& T : OutComponentPose)
		{
			T.SetScale3D(FVector::OneVector);
		}
	}

	// The elbow's local bend axis differs per rig, so try each cardinal axis and keep the one
	// that displaces the hand the most — callers only need "a visibly bent elbow", not a specific
	// anatomical direction. Returns the hand displacement (cm) achieved.
	static double BuildBentElbowLocalPose(const FReferenceSkeleton& RefSkel, int32 ElbowIdx,
		int32 HandIdx, float AngleDeg, TArray<FTransform>& OutLocalPose)
	{
		const TArray<FTransform>& RefLocalPose = RefSkel.GetRefBonePose();
		TArray<FTransform> RefComponent;
		ToComponentSpaceUnitScale(RefSkel, RefLocalPose, RefComponent);
		const FVector RefHandPos = RefComponent[HandIdx].GetLocation();

		double BestHandDelta = 0.0;
		for (const FVector& Axis : { FVector::XAxisVector, FVector::YAxisVector, FVector::ZAxisVector })
		{
			TArray<FTransform> Candidate = RefLocalPose;
			Candidate[ElbowIdx].SetRotation(
				FQuat(Axis, FMath::DegreesToRadians(AngleDeg)) * Candidate[ElbowIdx].GetRotation());
			TArray<FTransform> CandidateComponent;
			ToComponentSpaceUnitScale(RefSkel, Candidate, CandidateComponent);
			const double HandDelta = FVector::Dist(CandidateComponent[HandIdx].GetLocation(), RefHandPos);
			if (HandDelta > BestHandDelta)
			{
				BestHandDelta = HandDelta;
				OutLocalPose = MoveTemp(Candidate);
			}
		}
		return BestHandDelta;
	}

	// Interior angle at the elbow (degrees): 180 = straight arm, 90 = right-angle bend.
	static double InteriorElbowAngleDeg(const TArray<FTransform>& ComponentPose,
		int32 ShoulderIdx, int32 ElbowIdx, int32 HandIdx)
	{
		const FVector ToShoulder = (ComponentPose[ShoulderIdx].GetLocation() - ComponentPose[ElbowIdx].GetLocation()).GetSafeNormal();
		const FVector ToHand = (ComponentPose[HandIdx].GetLocation() - ComponentPose[ElbowIdx].GetLocation()).GetSafeNormal();
		return FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(ToShoulder, ToHand)));
	}

	// Express a component-space vector in the character's own body frame (right / forward / up
	// built from shoulders + pelvis->head), so directions are comparable across rigs regardless
	// of each mesh's component-space facing convention.
	static FVector ToBodyFrame(const TArray<FTransform>& Pose, int32 PelvisIdx, int32 HeadIdx,
		int32 ShoulderLIdx, int32 ShoulderRIdx, const FVector& V)
	{
		const FVector Right = (Pose[ShoulderRIdx].GetLocation() - Pose[ShoulderLIdx].GetLocation()).GetSafeNormal();
		FVector Up = (Pose[HeadIdx].GetLocation() - Pose[PelvisIdx].GetLocation()).GetSafeNormal();
		const FVector Fwd = FVector::CrossProduct(Right, Up).GetSafeNormal();
		Up = FVector::CrossProduct(Fwd, Right).GetSafeNormal();
		return FVector(V | Right, V | Fwd, V | Up);
	}

	struct FLoadedRetargetFixture
	{
		USkeletalMesh* SrcMesh = nullptr;
		USkeletalMesh* TgtMesh = nullptr;
		UIKRetargeter* Retargeter = nullptr;

		bool LoadOrSkip(FAutomationTestBase& Test)
		{
			SrcMesh = LoadObject<USkeletalMesh>(nullptr, SourceMeshPath);
			TgtMesh = LoadObject<USkeletalMesh>(nullptr, TargetMeshPath);
			Retargeter = LoadObject<UIKRetargeter>(nullptr, RetargeterPath);
			if (SrcMesh == nullptr || TgtMesh == nullptr || Retargeter == nullptr)
			{
				Test.AddWarning(FString::Printf(TEXT("Skipping: project assets not found (src=%d tgt=%d rtg=%d). ")
					TEXT("This test needs the OWLVRM4U-Template content."),
					SrcMesh != nullptr, TgtMesh != nullptr, Retargeter != nullptr));
				return false;
			}
			return true;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVrmVMCRetargetPoseReplayElbowTest,
	"VRM4U.VMC.RetargetPoseReplay.ElbowBend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVrmVMCRetargetPoseReplayElbowTest::RunTest(const FString& Parameters)
{
	using namespace VrmRetargetPoseReplayTest;

	FLoadedRetargetFixture Fx;
	if (!Fx.LoadOrSkip(*this))
	{
		return true;
	}

	FRetargetProfile Profile;
	Profile.FillProfileWithAssetSettings(Fx.Retargeter);

	FIKRetargetProcessor Processor;
	Processor.Initialize(Fx.SrcMesh, Fx.TgtMesh, Fx.Retargeter, Profile);
	TestTrue(TEXT("processor initialized from RTG + meshes"), Processor.IsInitialized());
	if (!Processor.IsInitialized())
	{
		return false;
	}

	const FRetargetSkeleton& Src = Processor.GetSkeleton(ERetargetSourceOrTarget::Source);
	const FRetargetSkeleton& Tgt = Processor.GetSkeleton(ERetargetSourceOrTarget::Target);
	const int32 SrcElbowIdx = Src.FindBoneIndexByName(SourceElbow);
	const int32 SrcHandIdx = Src.FindBoneIndexByName(SourceHand);
	const int32 TgtElbowIdx = Tgt.FindBoneIndexByName(TargetElbow);
	const int32 TgtHandIdx = Tgt.FindBoneIndexByName(TargetHand);
	TestNotEqual(TEXT("source elbow bone found"), SrcElbowIdx, (int32)INDEX_NONE);
	TestNotEqual(TEXT("source hand bone found"), SrcHandIdx, (int32)INDEX_NONE);
	TestNotEqual(TEXT("target elbow bone found"), TgtElbowIdx, (int32)INDEX_NONE);
	TestNotEqual(TEXT("target hand bone found"), TgtHandIdx, (int32)INDEX_NONE);
	if (SrcElbowIdx == INDEX_NONE || SrcHandIdx == INDEX_NONE ||
		TgtElbowIdx == INDEX_NONE || TgtHandIdx == INDEX_NONE)
	{
		return false;
	}

	const FReferenceSkeleton& SrcRef = Fx.SrcMesh->GetRefSkeleton();
	const float DeltaTime = 1.f / 30.f;

	// --- Frame 1: source reference pose -> baseline target pose. RunRetargeter returns a
	// reference to the processor's internal array, so copy the result before the next run.
	Processor.OnPlaybackReset();
	TArray<FTransform> SourcePose;
	ToComponentSpaceUnitScale(SrcRef, SrcRef.GetRefBonePose(), SourcePose);
	ScaleSourcePoseCompat(Processor,SourcePose);
	const TArray<FTransform> BaselineTargetPose = Processor.RunRetargeter(SourcePose, Profile, DeltaTime);

	// --- Frame 2: bent-elbow pose -> target pose.
	TArray<FTransform> BentLocalPose;
	const double BestSourceHandDelta = BuildBentElbowLocalPose(SrcRef, SrcElbowIdx, SrcHandIdx, 60.f, BentLocalPose);
	TestTrue(TEXT("synthetic elbow bend moves the source hand (sanity)"), BestSourceHandDelta > 5.0);
	AddInfo(FString::Printf(TEXT("source hand moved %.1f cm from a 60 deg elbow bend"), BestSourceHandDelta));

	TArray<FTransform> BentSourcePose;
	ToComponentSpaceUnitScale(SrcRef, BentLocalPose, BentSourcePose);
	ScaleSourcePoseCompat(Processor,BentSourcePose);
	const TArray<FTransform> BentTargetPose = Processor.RunRetargeter(BentSourcePose, Profile, DeltaTime);

	// --- The regression: the target forearm must articulate, not ride rigidly.
	const double TargetHandDelta = FVector::Dist(
		BentTargetPose[TgtHandIdx].GetLocation(), BaselineTargetPose[TgtHandIdx].GetLocation());
	const double TargetElbowAngleDeg = FMath::RadiansToDegrees(
		BentTargetPose[TgtElbowIdx].GetRotation().AngularDistance(BaselineTargetPose[TgtElbowIdx].GetRotation()));
	AddInfo(FString::Printf(TEXT("target hand moved %.1f cm, target elbow rotated %.1f deg"),
		TargetHandDelta, TargetElbowAngleDeg));

	TestTrue(FString::Printf(TEXT("target hand articulates (moved %.1f cm, want > 5)"), TargetHandDelta),
		TargetHandDelta > 5.0);
	TestTrue(FString::Printf(TEXT("target elbow rotates (%.1f deg, want > 15)"), TargetElbowAngleDeg),
		TargetElbowAngleDeg > 15.0);

	// Proportionality guard: the target hand should move on the same order as the source hand
	// (chain-aware retarget preserves the motion, proportions merely rescale it).
	TestTrue(FString::Printf(TEXT("target hand motion is proportionate (%.1f cm vs source %.1f cm)"),
		TargetHandDelta, BestSourceHandDelta),
		TargetHandDelta > 0.25 * BestSourceHandDelta);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVrmVMCRetargetPoseReplayFidelityTest,
	"VRM4U.VMC.RetargetPoseReplay.ElbowFidelity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVrmVMCRetargetPoseReplayFidelityTest::RunTest(const FString& Parameters)
{
	using namespace VrmRetargetPoseReplayTest;

	FLoadedRetargetFixture Fx;
	if (!Fx.LoadOrSkip(*this))
	{
		return true;
	}

	const FReferenceSkeleton& SrcRef = Fx.SrcMesh->GetRefSkeleton();
	const float DeltaTime = 1.f / 30.f;

	// --- Source-side ground truth, measured once: rest pose + 60 deg synthetic elbow bend.
	TArray<FTransform> RestSourcePose;
	ToComponentSpaceUnitScale(SrcRef, SrcRef.GetRefBonePose(), RestSourcePose);
	TArray<FTransform> BentLocalPose;
	BuildBentElbowLocalPose(SrcRef, SrcRef.FindBoneIndex(SourceElbow), SrcRef.FindBoneIndex(SourceHand), 60.f, BentLocalPose);
	TArray<FTransform> BentSourcePose;
	ToComponentSpaceUnitScale(SrcRef, BentLocalPose, BentSourcePose);

	// The sweep: as-configured asset settings first (this is what runs live and what the
	// assertions gate), then each FK rotation-mode override on the arm chains — measured via a
	// per-run FRetargetProfile override, so the asset is never modified.
	struct FMode { const TCHAR* Label; bool bOverride; EFKChainRotationMode Mode; };
	const FMode Modes[] = {
		{ TEXT("AssetConfigured"), false, EFKChainRotationMode::Interpolated },
		{ TEXT("Interpolated"), true, EFKChainRotationMode::Interpolated },
		{ TEXT("OneToOne"), true, EFKChainRotationMode::OneToOne },
		{ TEXT("MatchChain"), true, EFKChainRotationMode::MatchChain },
	};

	for (const FMode& M : Modes)
	{
		FRetargetProfile Profile;
		Profile.FillProfileWithAssetSettings(Fx.Retargeter);
		if (M.bOverride)
		{
			TArray<FIKRetargetFKChainsOpSettings*> FKSettings;
			Profile.GetOpSettingsByTypeInProfile<FIKRetargetFKChainsOpSettings>(FKSettings);
			for (FIKRetargetFKChainsOpSettings* S : FKSettings)
			{
				for (FRetargetFKChainSettings& Chain : S->ChainsToRetarget)
				{
					if (Chain.TargetChainName == TEXT("LeftArm") || Chain.TargetChainName == TEXT("RightArm"))
					{
						Chain.RotationMode = M.Mode;
					}
				}
			}
		}

		FIKRetargetProcessor Processor;
		Processor.Initialize(Fx.SrcMesh, Fx.TgtMesh, Fx.Retargeter, Profile);
		if (!Processor.IsInitialized())
		{
			AddError(FString::Printf(TEXT("[%s] processor failed to initialize"), M.Label));
			continue;
		}

		const FRetargetSkeleton& Src = Processor.GetSkeleton(ERetargetSourceOrTarget::Source);
		const FRetargetSkeleton& Tgt = Processor.GetSkeleton(ERetargetSourceOrTarget::Target);
		const int32 SrcShoulderIdx = Src.FindBoneIndexByName(SourceShoulder);
		const int32 SrcElbowIdx = Src.FindBoneIndexByName(SourceElbow);
		const int32 SrcHandIdx = Src.FindBoneIndexByName(SourceHand);
		const int32 SrcShoulderRIdx = Src.FindBoneIndexByName(SourceShoulderR);
		const int32 SrcPelvisIdx = Src.FindBoneIndexByName(SourcePelvis);
		const int32 SrcHeadIdx = Src.FindBoneIndexByName(SourceHead);
		const int32 TgtShoulderIdx = Tgt.FindBoneIndexByName(TargetShoulder);
		const int32 TgtElbowIdx = Tgt.FindBoneIndexByName(TargetElbow);
		const int32 TgtHandIdx = Tgt.FindBoneIndexByName(TargetHand);
		const int32 TgtShoulderRIdx = Tgt.FindBoneIndexByName(TargetShoulderR);
		const int32 TgtPelvisIdx = Tgt.FindBoneIndexByName(TargetPelvis);
		const int32 TgtHeadIdx = Tgt.FindBoneIndexByName(TargetHead);
		if (SrcShoulderIdx == INDEX_NONE || SrcElbowIdx == INDEX_NONE || SrcHandIdx == INDEX_NONE ||
			SrcShoulderRIdx == INDEX_NONE || SrcPelvisIdx == INDEX_NONE || SrcHeadIdx == INDEX_NONE ||
			TgtShoulderIdx == INDEX_NONE || TgtElbowIdx == INDEX_NONE || TgtHandIdx == INDEX_NONE ||
			TgtShoulderRIdx == INDEX_NONE || TgtPelvisIdx == INDEX_NONE || TgtHeadIdx == INDEX_NONE)
		{
			AddError(TEXT("arm/body bones not found on one of the rigs"));
			return false;
		}

		// Source ground truth (identical each iteration; cheap to recompute with the indices).
		const double SrcRestAngle = InteriorElbowAngleDeg(RestSourcePose, SrcShoulderIdx, SrcElbowIdx, SrcHandIdx);
		const double SrcBentAngle = InteriorElbowAngleDeg(BentSourcePose, SrcShoulderIdx, SrcElbowIdx, SrcHandIdx);
		const FVector SrcDispBody = ToBodyFrame(RestSourcePose, SrcPelvisIdx, SrcHeadIdx, SrcShoulderIdx, SrcShoulderRIdx,
			BentSourcePose[SrcHandIdx].GetLocation() - RestSourcePose[SrcHandIdx].GetLocation()).GetSafeNormal();

		Processor.OnPlaybackReset();
		TArray<FTransform> RestIn = RestSourcePose;
		ScaleSourcePoseCompat(Processor,RestIn);
		const TArray<FTransform> RestOut = Processor.RunRetargeter(RestIn, Profile, DeltaTime);
		TArray<FTransform> BentIn = BentSourcePose;
		ScaleSourcePoseCompat(Processor,BentIn);
		const TArray<FTransform> BentOut = Processor.RunRetargeter(BentIn, Profile, DeltaTime);

		const double TgtRestAngle = InteriorElbowAngleDeg(RestOut, TgtShoulderIdx, TgtElbowIdx, TgtHandIdx);
		const double TgtBentAngle = InteriorElbowAngleDeg(BentOut, TgtShoulderIdx, TgtElbowIdx, TgtHandIdx);
		const FVector TgtDispBody = ToBodyFrame(RestOut, TgtPelvisIdx, TgtHeadIdx, TgtShoulderIdx, TgtShoulderRIdx,
			BentOut[TgtHandIdx].GetLocation() - RestOut[TgtHandIdx].GetLocation()).GetSafeNormal();

		const double RestError = TgtRestAngle - SrcRestAngle;
		const double BendTransferError = (TgtBentAngle - TgtRestAngle) - (SrcBentAngle - SrcRestAngle);
		const double DirErrorDeg = FMath::RadiansToDegrees(FMath::Acos(
			FMath::Clamp(FVector::DotProduct(SrcDispBody, TgtDispBody), -1.0, 1.0)));

		AddInfo(FString::Printf(TEXT("[%s] rest elbow: src %.1f tgt %.1f (err %+.1f) | bent: src %.1f tgt %.1f (xfer err %+.1f) | hand-motion direction err %.1f deg"),
			M.Label, SrcRestAngle, TgtRestAngle, RestError, SrcBentAngle, TgtBentAngle, BendTransferError, DirErrorDeg));

		// --- Shoulder-raise poses: rotate the SOURCE upper arm (straight elbow) about each
		// cardinal axis and check that (a) the target elbow STAYS straight — a "phantom" elbow
		// bend during pure shoulder motion is the wrong-plane failure seen live — and (b) the
		// target upper-arm direction (in body frame) tracks the source's.
		double WorstPhantomBend = 0.0, WorstUpperArmDirErr = 0.0;
		for (int32 AxisIdx = 0; AxisIdx < 3; ++AxisIdx)
		{
			const FVector Axis = (AxisIdx == 0) ? FVector::XAxisVector : (AxisIdx == 1) ? FVector::YAxisVector : FVector::ZAxisVector;
			TArray<FTransform> RaisedLocal = SrcRef.GetRefBonePose();
			RaisedLocal[SrcShoulderIdx].SetRotation(
				FQuat(Axis, FMath::DegreesToRadians(75.f)) * RaisedLocal[SrcShoulderIdx].GetRotation());
			TArray<FTransform> RaisedSourcePose;
			ToComponentSpaceUnitScale(SrcRef, RaisedLocal, RaisedSourcePose);
			const double SrcRaisedElbow = InteriorElbowAngleDeg(RaisedSourcePose, SrcShoulderIdx, SrcElbowIdx, SrcHandIdx);
			const FVector SrcArmDirBody = ToBodyFrame(RestSourcePose, SrcPelvisIdx, SrcHeadIdx, SrcShoulderIdx, SrcShoulderRIdx,
				RaisedSourcePose[SrcElbowIdx].GetLocation() - RaisedSourcePose[SrcShoulderIdx].GetLocation()).GetSafeNormal();

			TArray<FTransform> RaisedIn = RaisedSourcePose;
			ScaleSourcePoseCompat(Processor,RaisedIn);
			const TArray<FTransform> RaisedOut = Processor.RunRetargeter(RaisedIn, Profile, DeltaTime);
			const double TgtRaisedElbow = InteriorElbowAngleDeg(RaisedOut, TgtShoulderIdx, TgtElbowIdx, TgtHandIdx);
			const FVector TgtArmDirBody = ToBodyFrame(RestOut, TgtPelvisIdx, TgtHeadIdx, TgtShoulderIdx, TgtShoulderRIdx,
				RaisedOut[TgtElbowIdx].GetLocation() - RaisedOut[TgtShoulderIdx].GetLocation()).GetSafeNormal();

			const double PhantomBend = FMath::Abs(TgtRaisedElbow - SrcRaisedElbow);
			const double ArmDirErr = FMath::RadiansToDegrees(FMath::Acos(
				FMath::Clamp(FVector::DotProduct(SrcArmDirBody, TgtArmDirBody), -1.0, 1.0)));
			WorstPhantomBend = FMath::Max(WorstPhantomBend, PhantomBend);
			WorstUpperArmDirErr = FMath::Max(WorstUpperArmDirErr, ArmDirErr);
			AddInfo(FString::Printf(TEXT("[%s] shoulder-raise axis %d: src elbow %.1f tgt elbow %.1f (phantom bend %.1f) | upper-arm dir err %.1f deg"),
				M.Label, AxisIdx, SrcRaisedElbow, TgtRaisedElbow, PhantomBend, ArmDirErr));
		}

		// --- Twist-leak probe: rotate the SOURCE upper arm 60 deg about its own bone axis
		// (the direction to its child elbow, so the elbow/hand positions stay put — a pure
		// wrist-orientation twist like a live tracker produces on an extended arm). A faithful
		// retarget keeps the target elbow straight and the target hand planted; twist leaking
		// into swing (phantom bend / hand travel) is the classic cross-convention failure.
		{
			TArray<FTransform> TwistLocal = SrcRef.GetRefBonePose();
			const FVector TwistAxis = TwistLocal[SrcElbowIdx].GetTranslation().GetSafeNormal();
			TwistLocal[SrcShoulderIdx].SetRotation(
				TwistLocal[SrcShoulderIdx].GetRotation() * FQuat(TwistAxis, FMath::DegreesToRadians(60.f)));
			TArray<FTransform> TwistSourcePose;
			ToComponentSpaceUnitScale(SrcRef, TwistLocal, TwistSourcePose);
			const double SrcTwistElbow = InteriorElbowAngleDeg(TwistSourcePose, SrcShoulderIdx, SrcElbowIdx, SrcHandIdx);
			const double SrcTwistHandTravel = FVector::Dist(
				TwistSourcePose[SrcHandIdx].GetLocation(), RestSourcePose[SrcHandIdx].GetLocation());

			TArray<FTransform> TwistIn = TwistSourcePose;
			ScaleSourcePoseCompat(Processor,TwistIn);
			const TArray<FTransform> TwistOut = Processor.RunRetargeter(TwistIn, Profile, DeltaTime);
			const double TgtTwistElbow = InteriorElbowAngleDeg(TwistOut, TgtShoulderIdx, TgtElbowIdx, TgtHandIdx);
			const double TgtTwistHandTravel = FVector::Dist(
				TwistOut[TgtHandIdx].GetLocation(), RestOut[TgtHandIdx].GetLocation());

			const double TwistPhantomBend = FMath::Abs(TgtTwistElbow - SrcTwistElbow);
			AddInfo(FString::Printf(TEXT("[%s] upper-arm twist 60: src elbow %.1f hand-travel %.1f cm -> tgt elbow %.1f (phantom bend %.1f) hand-travel %.1f cm"),
				M.Label, SrcTwistElbow, SrcTwistHandTravel, TgtTwistElbow, TwistPhantomBend, TgtTwistHandTravel));
			if (!M.bOverride)
			{
				TestTrue(FString::Printf(TEXT("twist does not leak into elbow bend (phantom %.1f deg, want within 25)"), TwistPhantomBend),
					TwistPhantomBend <= 25.0);
				TestTrue(FString::Printf(TEXT("twist does not move the hand (travel %.1f cm, want < 15)"), TgtTwistHandTravel),
					TgtTwistHandTravel < 15.0);
			}
		}

		// Only the as-configured asset settings gate the suite; the overrides are diagnostics.
		if (!M.bOverride)
		{
			// 20/30 deg tolerances: proportion differences legitimately shift these a little, but
			// a wrong-plane retarget-pose offset or shape-matching distortion shows up as 30-90+.
			TestTrue(FString::Printf(TEXT("rest-pose elbow fidelity (error %+.1f deg, want within 20)"), RestError),
				FMath::Abs(RestError) <= 20.0);
			TestTrue(FString::Printf(TEXT("bend-transfer fidelity (error %+.1f deg, want within 20)"), BendTransferError),
				FMath::Abs(BendTransferError) <= 20.0);
			TestTrue(FString::Printf(TEXT("bend-direction fidelity (error %.1f deg, want within 30)"), DirErrorDeg),
				DirErrorDeg <= 30.0);
			TestTrue(FString::Printf(TEXT("no phantom elbow bend on shoulder motion (worst %.1f deg, want within 25)"), WorstPhantomBend),
				WorstPhantomBend <= 25.0);
			TestTrue(FString::Printf(TEXT("upper-arm direction fidelity (worst %.1f deg, want within 30)"), WorstUpperArmDirErr),
				WorstUpperArmDirErr <= 30.0);
		}
	}

	return true;
}

// Replays the newest live PIE capture (written by UVrmVMCRetargetAnimInstance::CapturePosesForDebug
// into Saved/VRM4U/PoseCapture_*.txt) through the offline processor and diffs the replayed target
// pose against the captured LIVE target pose. Pure diagnostic — reports, never fails:
//   - replayed == captured live  -> the live pipeline is exactly the (proven-good) offline retarget,
//     so any visual mismatch is already present in the captured SOURCE pose.
//   - replayed != captured live  -> something between the retarget node and the final pose
//     (post-process AnimBP, node wiring, profile) is modifying the result; the worst bones say what.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVrmVMCRetargetCapturedPoseTest,
	"VRM4U.VMC.RetargetPoseReplay.CapturedPose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVrmVMCRetargetCapturedPoseTest::RunTest(const FString& Parameters)
{
	using namespace VrmRetargetPoseReplayTest;

	const FString CaptureDir = FPaths::ProjectSavedDir() / TEXT("VRM4U");
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(CaptureDir / TEXT("PoseCapture_*.txt")), true, false);
	if (Files.Num() == 0)
	{
		AddWarning(FString::Printf(TEXT("Skipping: no PoseCapture_*.txt in %s — run PIE with the ")
			TEXT("retarget anim instance's bDebugCapturePoses on, strike a pose, stop PIE."), *CaptureDir));
		return true;
	}
	Files.Sort(); // timestamped names sort chronologically
	const FString File = CaptureDir / Files.Last();
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *File))
	{
		AddWarning(FString::Printf(TEXT("Skipping: cannot read %s"), *File));
		return true;
	}

	FString SrcMeshName, TgtMeshName, SrcPoseLine, TgtPoseLine;
	for (const FString& L : Lines)
	{
		if (L.StartsWith(TEXT("VRMCAP HDR ")))
		{
			TArray<FString> Tok;
			L.ParseIntoArrayWS(Tok);
			if (Tok.Num() >= 4)
			{
				(Tok[2] == TEXT("source") ? SrcMeshName : TgtMeshName) = Tok[3];
			}
		}
		else if (L.StartsWith(TEXT("VRMCAP POSE ")))
		{
			// keep the LAST sample of each rig — the pose held when PIE stopped
			(L.Contains(TEXT(" source ")) ? SrcPoseLine : TgtPoseLine) = L;
		}
	}
	if (SrcPoseLine.IsEmpty() || TgtPoseLine.IsEmpty())
	{
		AddWarning(FString::Printf(TEXT("Skipping: %s has no complete pose sample"), *File));
		return true;
	}
	AddInfo(FString::Printf(TEXT("replaying %s (src mesh %s, tgt mesh %s)"), *Files.Last(), *SrcMeshName, *TgtMeshName));

	const auto ParsePose = [](const FString& Line, TArray<FTransform>& Out) -> bool
	{
		TArray<FString> Tok;
		Line.ParseIntoArrayWS(Tok);
		// VRMCAP POSE <t> <rig> <N> then 7 floats per bone
		if (Tok.Num() < 5)
		{
			return false;
		}
		const int32 N = FCString::Atoi(*Tok[4]);
		if (N <= 0 || Tok.Num() != 5 + N * 7)
		{
			return false;
		}
		Out.SetNum(N);
		for (int32 i = 0; i < N; ++i)
		{
			const int32 B = 5 + i * 7;
			const FVector P(FCString::Atod(*Tok[B]), FCString::Atod(*Tok[B + 1]), FCString::Atod(*Tok[B + 2]));
			FQuat Q(FCString::Atod(*Tok[B + 3]), FCString::Atod(*Tok[B + 4]), FCString::Atod(*Tok[B + 5]), FCString::Atod(*Tok[B + 6]));
			Q.Normalize();
			Out[i] = FTransform(Q, P, FVector::OneVector);
		}
		return true;
	};

	TArray<FTransform> CapturedSource, CapturedTarget;
	if (!ParsePose(SrcPoseLine, CapturedSource) || !ParsePose(TgtPoseLine, CapturedTarget))
	{
		AddWarning(TEXT("Skipping: malformed pose line in capture"));
		return true;
	}

	FLoadedRetargetFixture Fx;
	if (!Fx.LoadOrSkip(*this))
	{
		return true;
	}
	if (SrcMeshName != Fx.SrcMesh->GetName() || TgtMeshName != Fx.TgtMesh->GetName() ||
		CapturedSource.Num() != Fx.SrcMesh->GetRefSkeleton().GetNum() ||
		CapturedTarget.Num() != Fx.TgtMesh->GetRefSkeleton().GetNum())
	{
		AddWarning(FString::Printf(TEXT("Skipping: capture is for %s->%s (%d/%d bones), fixture is %s->%s (%d/%d)"),
			*SrcMeshName, *TgtMeshName, CapturedSource.Num(), CapturedTarget.Num(),
			*Fx.SrcMesh->GetName(), *Fx.TgtMesh->GetName(),
			Fx.SrcMesh->GetRefSkeleton().GetNum(), Fx.TgtMesh->GetRefSkeleton().GetNum()));
		return true;
	}

	FRetargetProfile Profile;
	Profile.FillProfileWithAssetSettings(Fx.Retargeter);
	FIKRetargetProcessor Processor;
	Processor.Initialize(Fx.SrcMesh, Fx.TgtMesh, Fx.Retargeter, Profile);
	if (!Processor.IsInitialized())
	{
		AddError(TEXT("processor failed to initialize"));
		return false;
	}
	const FRetargetSkeleton& Src = Processor.GetSkeleton(ERetargetSourceOrTarget::Source);
	const FRetargetSkeleton& Tgt = Processor.GetSkeleton(ERetargetSourceOrTarget::Target);

	Processor.OnPlaybackReset();
	TArray<FTransform> SourceIn = CapturedSource;
	ScaleSourcePoseCompat(Processor,SourceIn);
	const TArray<FTransform> Replayed = Processor.RunRetargeter(SourceIn, Profile, 1.f / 30.f);

	// Per-bone divergence: replayed offline output vs the captured LIVE final pose.
	struct FBoneErr { FName Bone; double Deg; double Cm; };
	TArray<FBoneErr> Errs;
	for (int32 i = 0; i < Tgt.BoneNames.Num(); ++i)
	{
		const double Deg = FMath::RadiansToDegrees(Replayed[i].GetRotation().AngularDistance(CapturedTarget[i].GetRotation()));
		const double Cm = FVector::Dist(Replayed[i].GetLocation(), CapturedTarget[i].GetLocation());
		Errs.Add({ Tgt.BoneNames[i], Deg, Cm });
	}
	TArray<FBoneErr> Sorted = Errs;
	Sorted.Sort([](const FBoneErr& A, const FBoneErr& B) { return A.Deg > B.Deg; });
	for (int32 i = 0; i < FMath::Min(8, Sorted.Num()); ++i)
	{
		AddInfo(FString::Printf(TEXT("live-vs-replay worst %d: %s rot %.1f deg, pos %.1f cm"),
			i + 1, *Sorted[i].Bone.ToString(), Sorted[i].Deg, Sorted[i].Cm));
	}
	for (const TCHAR* BoneName : { TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("hand_l"),
								   TEXT("upperarm_r"), TEXT("lowerarm_r"), TEXT("hand_r") })
	{
		const int32 i = Tgt.FindBoneIndexByName(BoneName);
		if (i != INDEX_NONE)
		{
			AddInfo(FString::Printf(TEXT("live-vs-replay %s: rot %.1f deg, pos %.1f cm"),
				BoneName, Errs[i].Deg, Errs[i].Cm));
		}
	}

	// Interior elbow angles: does the captured LIVE source even match what the golds showed, and
	// where does the elbow angle change — in the retarget (replayed) or after it (live final)?
	const auto ReportArm = [&](const TCHAR* Side, const FName& SShoulder, const FName& SElbow, const FName& SHand,
		const TCHAR* TShoulder, const TCHAR* TElbow, const TCHAR* THand)
	{
		const int32 SS = Src.FindBoneIndexByName(SShoulder), SE = Src.FindBoneIndexByName(SElbow), SH = Src.FindBoneIndexByName(SHand);
		const int32 TS = Tgt.FindBoneIndexByName(TShoulder), TE = Tgt.FindBoneIndexByName(TElbow), TH = Tgt.FindBoneIndexByName(THand);
		if (SS == INDEX_NONE || SE == INDEX_NONE || SH == INDEX_NONE ||
			TS == INDEX_NONE || TE == INDEX_NONE || TH == INDEX_NONE)
		{
			return;
		}
		AddInfo(FString::Printf(TEXT("%s elbow interior: SOURCE live %.1f | TARGET replayed %.1f | TARGET live %.1f deg"),
			Side,
			InteriorElbowAngleDeg(CapturedSource, SS, SE, SH),
			InteriorElbowAngleDeg(Replayed, TS, TE, TH),
			InteriorElbowAngleDeg(CapturedTarget, TS, TE, TH)));
	};
	ReportArm(TEXT("LEFT"), SourceShoulder, SourceElbow, SourceHand, TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("hand_l"));
	ReportArm(TEXT("RIGHT"), TEXT("J_Bip_R_UpperArm"), TEXT("J_Bip_R_LowerArm"), TEXT("J_Bip_R_Hand"),
		TEXT("upperarm_r"), TEXT("lowerarm_r"), TEXT("hand_r"));

	// Upper-arm elevation (deg from straight up; 0 = arms overhead, 90 = horizontal): shows how
	// much of the source's arm raise reaches the target — the shoulder-chain transfer metric.
	const auto ElevFromUp = [](const TArray<FTransform>& Pose, int32 ShoulderIdx, int32 ElbowIdx)
	{
		const FVector Dir = (Pose[ElbowIdx].GetLocation() - Pose[ShoulderIdx].GetLocation()).GetSafeNormal();
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dir.Z, -1.0, 1.0)));
	};
	const auto ReportElevation = [&](const TCHAR* Side, const FName& SUpper, const FName& SLower,
		const TCHAR* TUpper, const TCHAR* TLower)
	{
		const int32 SU = Src.FindBoneIndexByName(SUpper), SL = Src.FindBoneIndexByName(SLower);
		const int32 TU = Tgt.FindBoneIndexByName(TUpper), TL = Tgt.FindBoneIndexByName(TLower);
		if (SU == INDEX_NONE || SL == INDEX_NONE || TU == INDEX_NONE || TL == INDEX_NONE)
		{
			return;
		}
		AddInfo(FString::Printf(TEXT("%s upper-arm elevation-from-up: SOURCE live %.1f | TARGET replayed %.1f | TARGET live %.1f deg"),
			Side,
			ElevFromUp(CapturedSource, SU, SL),
			ElevFromUp(Replayed, TU, TL),
			ElevFromUp(CapturedTarget, TU, TL)));
	};
	ReportElevation(TEXT("LEFT"), SourceShoulder, SourceElbow, TEXT("upperarm_l"), TEXT("lowerarm_l"));
	ReportElevation(TEXT("RIGHT"), TEXT("J_Bip_R_UpperArm"), TEXT("J_Bip_R_LowerArm"), TEXT("upperarm_r"), TEXT("lowerarm_r"));

	// Index-finger curl (angle between proximal and middle segment; 0 = straight, ~100 = fist):
	// the finger-chain transfer metric.
	const auto Curl = [](const TArray<FTransform>& Pose, int32 B1, int32 B2, int32 B3)
	{
		const FVector A = (Pose[B2].GetLocation() - Pose[B1].GetLocation()).GetSafeNormal();
		const FVector B = (Pose[B3].GetLocation() - Pose[B2].GetLocation()).GetSafeNormal();
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(A, B), -1.0, 1.0)));
	};
	const auto ReportCurl = [&](const TCHAR* Side, const TCHAR* S1, const TCHAR* S2, const TCHAR* S3,
		const TCHAR* T1, const TCHAR* T2, const TCHAR* T3)
	{
		const int32 SB1 = Src.FindBoneIndexByName(S1), SB2 = Src.FindBoneIndexByName(S2), SB3 = Src.FindBoneIndexByName(S3);
		const int32 TB1 = Tgt.FindBoneIndexByName(T1), TB2 = Tgt.FindBoneIndexByName(T2), TB3 = Tgt.FindBoneIndexByName(T3);
		if (SB1 == INDEX_NONE || SB2 == INDEX_NONE || SB3 == INDEX_NONE ||
			TB1 == INDEX_NONE || TB2 == INDEX_NONE || TB3 == INDEX_NONE)
		{
			return;
		}
		AddInfo(FString::Printf(TEXT("%s index-finger curl: SOURCE live %.1f | TARGET replayed %.1f | TARGET live %.1f deg"),
			Side,
			Curl(CapturedSource, SB1, SB2, SB3),
			Curl(Replayed, TB1, TB2, TB3),
			Curl(CapturedTarget, TB1, TB2, TB3)));
	};
	ReportCurl(TEXT("LEFT"), TEXT("J_Bip_L_Index1"), TEXT("J_Bip_L_Index2"), TEXT("J_Bip_L_Index3"),
		TEXT("index_01_l"), TEXT("index_02_l"), TEXT("index_03_l"));
	ReportCurl(TEXT("RIGHT"), TEXT("J_Bip_R_Index1"), TEXT("J_Bip_R_Index2"), TEXT("J_Bip_R_Index3"),
		TEXT("index_01_r"), TEXT("index_02_r"), TEXT("index_03_r"));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && UE 5.6+
