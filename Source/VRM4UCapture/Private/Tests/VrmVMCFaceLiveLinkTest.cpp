// VRM4U Copyright (c) 2021-2026 Haruyoshi Yamamoto. This software is released under the MIT License.

#include "Misc/EngineVersionComparison.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "VrmVMCFaceLiveLink.h"

// Pure-logic tests for the VMC -> LiveLink ARKit curve mapping (VrmVMCFaceLiveLink). These run
// headless (no LiveLink client / no PIE): the schema and translation rules the MetaHuman face
// path depends on are covered automatically; only the visual result needs a manual check.

namespace VrmVMCFaceLiveLinkTestHelpers
{
	static int32 IndexOf(const TCHAR* PropertyName)
	{
		return VrmVMCFaceLiveLink::GetARKitPropertyNames().IndexOfByKey(FName(PropertyName));
	}

	static TArray<float> Map(std::initializer_list<TPair<FName, float>> Curves)
	{
		TMap<FName, float> In;
		for (const TPair<FName, float>& Pair : Curves)
		{
			In.Add(Pair.Key, Pair.Value);
		}
		TArray<float> Out;
		VrmVMCFaceLiveLink::MapVMCCurvesToARKit(In, Out);
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVrmVMCFaceLiveLinkSchemaTest,
	"VRM4U.VMC.FaceLiveLink.Schema",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVrmVMCFaceLiveLinkSchemaTest::RunTest(const FString& Parameters)
{
	using namespace VrmVMCFaceLiveLinkTestHelpers;
	const TArray<FName>& Names = VrmVMCFaceLiveLink::GetARKitPropertyNames();

	// Epic's Live Link Face publishes EARFaceBlendShape::MAX = 61 properties (52 ARKit
	// blendshapes + 9 head/eye rotation curves); the MetaHuman mapping assets expect them.
	TestEqual(TEXT("property count matches EARFaceBlendShape::MAX"), Names.Num(), 61);
	TestEqual(TEXT("first property is EyeBlinkLeft (enum order)"), Names[0], FName(TEXT("EyeBlinkLeft")));
	TestEqual(TEXT("last property is RightEyeRoll (enum order)"), Names[60], FName(TEXT("RightEyeRoll")));
	TestNotEqual(TEXT("JawOpen present"), IndexOf(TEXT("JawOpen")), (int32)INDEX_NONE);
	TestNotEqual(TEXT("TongueOut present"), IndexOf(TEXT("TongueOut")), (int32)INDEX_NONE);

	// No duplicates: a repeated name would silently shift every later curve.
	TSet<FName> Unique(Names);
	TestEqual(TEXT("property names are unique"), Unique.Num(), Names.Num());

	// Output array always matches the static-data schema, even with no input.
	TArray<float> Out;
	VrmVMCFaceLiveLink::MapVMCCurvesToARKit(TMap<FName, float>(), Out);
	TestEqual(TEXT("empty input yields full-size zeroed frame"), Out.Num(), Names.Num());
	for (float Value : Out)
	{
		if (Value != 0.0f)
		{
			AddError(TEXT("empty input must map to all-zero values"));
			break;
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVrmVMCFaceLiveLinkPassThroughTest,
	"VRM4U.VMC.FaceLiveLink.PassThrough",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVrmVMCFaceLiveLinkPassThroughTest::RunTest(const FString& Parameters)
{
	using namespace VrmVMCFaceLiveLinkTestHelpers;

	// PerfectSync senders emit camelCase ARKit names; FName matching is case-insensitive.
	{
		const TArray<float> Out = Map({ { FName(TEXT("jawOpen")), 0.5f }, { FName(TEXT("eyeBlinkLeft")), 0.9f } });
		TestEqual(TEXT("camelCase jawOpen passes through"), Out[IndexOf(TEXT("JawOpen"))], 0.5f);
		TestEqual(TEXT("camelCase eyeBlinkLeft passes through"), Out[IndexOf(TEXT("EyeBlinkLeft"))], 0.9f);
	}
	// VSeeFace-style senders wrap keys in a "BlendShape." prefix; it must be stripped.
	{
		const TArray<float> Out = Map({ { FName(TEXT("BlendShape.jawOpen")), 0.25f } });
		TestEqual(TEXT("BlendShape. prefix is stripped"), Out[IndexOf(TEXT("JawOpen"))], 0.25f);
	}
	// Unknown keys (VRM morph names for meshes, sender extensions) must be ignored.
	{
		const TArray<float> Out = Map({ { FName(TEXT("Fcl_MTH_A")), 1.0f }, { FName(TEXT("SomeCustomKey")), 1.0f } });
		for (float Value : Out)
		{
			if (Value != 0.0f)
			{
				AddError(TEXT("unknown keys must not drive any ARKit curve"));
				break;
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVrmVMCFaceLiveLinkFallbackTest,
	"VRM4U.VMC.FaceLiveLink.ClassicVrmFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVrmVMCFaceLiveLinkFallbackTest::RunTest(const FString& Parameters)
{
	using namespace VrmVMCFaceLiveLinkTestHelpers;

	// Classic viseme A opens the jaw (scaled).
	{
		const TArray<float> Out = Map({ { FName(TEXT("A")), 1.0f } });
		TestTrue(TEXT("viseme A drives JawOpen"), Out[IndexOf(TEXT("JawOpen"))] > 0.5f);
	}
	// Blink fans out to both eyes; single-side variants stay single-side.
	{
		const TArray<float> Out = Map({ { FName(TEXT("Blink")), 1.0f } });
		TestEqual(TEXT("Blink drives left eye"), Out[IndexOf(TEXT("EyeBlinkLeft"))], 1.0f);
		TestEqual(TEXT("Blink drives right eye"), Out[IndexOf(TEXT("EyeBlinkRight"))], 1.0f);
	}
	{
		const TArray<float> Out = Map({ { FName(TEXT("Blink_L")), 1.0f } });
		TestEqual(TEXT("Blink_L drives left eye"), Out[IndexOf(TEXT("EyeBlinkLeft"))], 1.0f);
		TestEqual(TEXT("Blink_L leaves right eye alone"), Out[IndexOf(TEXT("EyeBlinkRight"))], 0.0f);
	}
	// VRM 1.0 spellings map like their 0.x counterparts (aa == A, happy == Joy).
	{
		const TArray<float> A = Map({ { FName(TEXT("aa")), 1.0f } });
		TestTrue(TEXT("VRM 1.0 'aa' drives JawOpen"), A[IndexOf(TEXT("JawOpen"))] > 0.5f);
		const TArray<float> Happy = Map({ { FName(TEXT("happy")), 1.0f } });
		TestTrue(TEXT("VRM 1.0 'happy' drives MouthSmileLeft"), Happy[IndexOf(TEXT("MouthSmileLeft"))] > 0.0f);
	}
	// Gaze: LookLeft = out on the left eye, in on the right.
	{
		const TArray<float> Out = Map({ { FName(TEXT("LookLeft")), 1.0f } });
		TestEqual(TEXT("LookLeft -> EyeLookOutLeft"), Out[IndexOf(TEXT("EyeLookOutLeft"))], 1.0f);
		TestEqual(TEXT("LookLeft -> EyeLookInRight"), Out[IndexOf(TEXT("EyeLookInRight"))], 1.0f);
		TestEqual(TEXT("LookLeft leaves EyeLookInLeft alone"), Out[IndexOf(TEXT("EyeLookInLeft"))], 0.0f);
	}
	// Collision rule: direct ARKit value and a fallback writing the same target keep the max.
	{
		const TArray<float> Out = Map({ { FName(TEXT("Blink")), 0.4f }, { FName(TEXT("eyeBlinkLeft")), 0.9f } });
		TestEqual(TEXT("max wins on collision (left)"), Out[IndexOf(TEXT("EyeBlinkLeft"))], 0.9f);
		TestEqual(TEXT("fallback still applies to right"), Out[IndexOf(TEXT("EyeBlinkRight"))], 0.4f);
	}
	// PerfectSync stream must NOT be distorted by the fallback: ARKit keys never fan out.
	{
		const TArray<float> Out = Map({ { FName(TEXT("mouthSmileLeft")), 1.0f } });
		TestEqual(TEXT("ARKit key does not fan out"), Out[IndexOf(TEXT("MouthSmileRight"))], 0.0f);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
