// VRM4U Copyright (c) 2021-2026 Haruyoshi Yamamoto. This software is released under the MIT License.

#include "Misc/EngineVersionComparison.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "VrmVMCBlueprintLibrary.h"
#include "OWLVrmVMCPoseSnapshot.h"

// Pure-logic tests for the VMC pose-snapshot helpers (UVrmVMCBlueprintLibrary). These run headless
// (no skeletal mesh / no PIE) by hand-building FVrmVMCPoseSnapshot values, so the comparison metric
// that backs the ghost readout / regression checks is covered automatically.

namespace VrmVMCSnapshotTestHelpers
{
	// Build a snapshot with the given humanoid names and component-space rotations.
	// LocalTransforms are filled with identity so IsSnapshotValid()'s count checks pass.
	static FVrmVMCPoseSnapshot MakeSnapshot(const TArray<FName>& Names, const TArray<FQuat>& ComponentRotations)
	{
		FVrmVMCPoseSnapshot Snapshot;
		Snapshot.HumanoidNames = Names;
		for (const FQuat& Rotation : ComponentRotations)
		{
			Snapshot.LocalTransforms.Add(FTransform::Identity);
			Snapshot.ComponentTransforms.Add(FTransform(Rotation));
		}
		return Snapshot;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVrmVMCSnapshotValidityTest,
	"VRM4U.VMC.Snapshot.Validity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVrmVMCSnapshotValidityTest::RunTest(const FString& Parameters)
{
	using namespace VrmVMCSnapshotTestHelpers;

	TestFalse(TEXT("empty snapshot is invalid"),
		UVrmVMCBlueprintLibrary::IsSnapshotValid(FVrmVMCPoseSnapshot()));

	const FVrmVMCPoseSnapshot Good = MakeSnapshot({ TEXT("hips") }, { FQuat::Identity });
	TestTrue(TEXT("matched-count snapshot is valid"),
		UVrmVMCBlueprintLibrary::IsSnapshotValid(Good));

	FVrmVMCPoseSnapshot Mismatched = Good;
	Mismatched.HumanoidNames.Add(TEXT("spine")); // names=2 but transforms=1
	TestFalse(TEXT("count-mismatched snapshot is invalid"),
		UVrmVMCBlueprintLibrary::IsSnapshotValid(Mismatched));

	TestEqual(TEXT("GetSnapshotHumanoidNames returns the names"),
		UVrmVMCBlueprintLibrary::GetSnapshotHumanoidNames(Good).Num(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVrmVMCSnapshotCompareTest,
	"VRM4U.VMC.Snapshot.Compare",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVrmVMCSnapshotCompareTest::RunTest(const FString& Parameters)
{
	using namespace VrmVMCSnapshotTestHelpers;

	const FQuat Zero = FQuat::Identity;
	const FQuat Ninety = FQuat(FVector::UpVector, FMath::DegreesToRadians(90.f));

	// NOTE: CompareSnapshots returns TRUE when differences are found (it is a "do they differ?"
	// query, return OutDifferingBones.Num() > 0), and FALSE when the snapshots match.

	// Identical -> no differences found (returns false), empty diff list.
	{
		const FVrmVMCPoseSnapshot A = MakeSnapshot({ TEXT("leftUpperArm") }, { Zero });
		const FVrmVMCPoseSnapshot B = MakeSnapshot({ TEXT("leftUpperArm") }, { Zero });
		TArray<FName> Diff;
		TestFalse(TEXT("identical snapshots report no differences"),
			UVrmVMCBlueprintLibrary::CompareSnapshots(A, B, Diff, 0.5f));
		TestEqual(TEXT("no differing bones"), Diff.Num(), 0);
	}

	// Rotated past threshold -> difference found (returns true), the bone is reported.
	{
		const FVrmVMCPoseSnapshot A = MakeSnapshot({ TEXT("leftUpperArm") }, { Zero });
		const FVrmVMCPoseSnapshot B = MakeSnapshot({ TEXT("leftUpperArm") }, { Ninety });
		TArray<FName> Diff;
		TestTrue(TEXT("a 90-degree rotation is reported as a difference"),
			UVrmVMCBlueprintLibrary::CompareSnapshots(A, B, Diff, 0.5f));
		TestTrue(TEXT("the rotated bone is named"), Diff.Contains(FName(TEXT("leftUpperArm"))));
	}

	// Name match is case-insensitive -> identical pose -> no difference (returns false).
	{
		const FVrmVMCPoseSnapshot A = MakeSnapshot({ TEXT("leftUpperArm") }, { Zero });
		const FVrmVMCPoseSnapshot B = MakeSnapshot({ TEXT("LEFTUPPERARM") }, { Zero });
		TArray<FName> Diff;
		TestFalse(TEXT("case-insensitive name match yields no difference"),
			UVrmVMCBlueprintLibrary::CompareSnapshots(A, B, Diff, 0.5f));
	}

	// A bone present in A but missing in B is reported as differing (returns true).
	{
		const FVrmVMCPoseSnapshot A = MakeSnapshot({ TEXT("hips"), TEXT("spine") }, { Zero, Zero });
		const FVrmVMCPoseSnapshot B = MakeSnapshot({ TEXT("hips") }, { Zero });
		TArray<FName> Diff;
		TestTrue(TEXT("a bone missing from B counts as a difference"),
			UVrmVMCBlueprintLibrary::CompareSnapshots(A, B, Diff, 0.5f));
		TestTrue(TEXT("the missing bone is named"), Diff.Contains(FName(TEXT("spine"))));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
