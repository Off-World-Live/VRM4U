// VRM4U Copyright (c) 2021-2026 Haruyoshi Yamamoto. This software is released under the MIT License.

#include "Misc/EngineVersionComparison.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_VERSION_OLDER_THAN(5,2,0)

#include "Misc/AutomationTest.h"
#include "VrmVMCRetargetAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Retargeter/IKRetargeter.h"
#include "UObject/Package.h"

// These are pure object-wiring tests for UVrmVMCRetargetAnimInstance — no world, no PIE.
// They cover defaults and SetRetargetSource(). The runtime retarget itself (proxy evaluation,
// tick ordering vs the live source) is still validated in PIE; see docs/ik-retargeter-pipeline.md.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVrmVMCRetargetAnimInstanceDefaultsTest,
	"VRM4U.VMC.RetargetAnimInstance.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVrmVMCRetargetAnimInstanceDefaultsTest::RunTest(const FString& Parameters)
{
	// A UAnimInstance has ClassWithin = USkeletalMeshComponent, so its Outer MUST be a skeletal
	// mesh component, not a package. Construct the owning component first.
	USkeletalMeshComponent* Owner = NewObject<USkeletalMeshComponent>(GetTransientPackage());
	UVrmVMCRetargetAnimInstance* AI = NewObject<UVrmVMCRetargetAnimInstance>(Owner);
	TestNotNull(TEXT("anim instance constructs"), AI);
	if (AI == nullptr)
	{
		return false;
	}

	TestNull(TEXT("Retargeter defaults to null"), AI->Retargeter.Get());
	TestNull(TEXT("SourceMeshComponent defaults to null"), AI->SourceMeshComponent.Get());
	TestTrue(TEXT("source tick prerequisite is on by default"), AI->bAddSourceTickPrerequisite);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVrmVMCRetargetAnimInstanceSetSourceTest,
	"VRM4U.VMC.RetargetAnimInstance.SetRetargetSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVrmVMCRetargetAnimInstanceSetSourceTest::RunTest(const FString& Parameters)
{
	// The anim instance's Outer must be a skeletal mesh component (ClassWithin). The *source* and
	// retargeter are independent objects and can live in the transient package.
	USkeletalMeshComponent* Owner = NewObject<USkeletalMeshComponent>(GetTransientPackage());
	UVrmVMCRetargetAnimInstance* AI = NewObject<UVrmVMCRetargetAnimInstance>(Owner);
	USkeletalMeshComponent* Src = NewObject<USkeletalMeshComponent>(GetTransientPackage());
	UIKRetargeter* Rtg = NewObject<UIKRetargeter>(GetTransientPackage());

	if (AI == nullptr || Src == nullptr || Rtg == nullptr)
	{
		AddError(TEXT("failed to construct test objects"));
		return false;
	}

	AI->SetRetargetSource(Src, Rtg);
	TestTrue(TEXT("SetRetargetSource sets the source component"), AI->SourceMeshComponent.Get() == Src);
	TestTrue(TEXT("SetRetargetSource sets the retargeter"), AI->Retargeter.Get() == Rtg);

	// Clearing back to null is honored too.
	AI->SetRetargetSource(nullptr, nullptr);
	TestNull(TEXT("source cleared"), AI->SourceMeshComponent.Get());
	TestNull(TEXT("retargeter cleared"), AI->Retargeter.Get());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
