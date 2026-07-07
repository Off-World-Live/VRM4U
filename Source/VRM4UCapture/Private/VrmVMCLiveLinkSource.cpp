// VRM4U Copyright (c) 2021-2026 Haruyoshi Yamamoto. This software is released under the MIT License.

#include "VrmVMCLiveLinkSource.h"
#include "VrmVMCFaceLiveLink.h"
#include "VRM4U_VMCSubsystem.h"
#include "VRM4UCaptureLog.h"

#include "Engine/Engine.h"
#include "Features/IModularFeatures.h"
#include "ILiveLinkClient.h"
#include "LiveLinkTypes.h"
#include "Roles/LiveLinkBasicRole.h"
#include "Roles/LiveLinkBasicTypes.h"

#define LOCTEXT_NAMESPACE "VrmVMCLiveLink"

namespace VrmVMCFaceLiveLink
{
	const TArray<FName>& GetARKitPropertyNames()
	{
		// EARFaceBlendShape order (ARTrackable.h): the exact names + order Epic's Live Link
		// Face source publishes, so the MetaHuman ARKit mapping assets match without remap.
		static const TArray<FName> Names = {
			// Left eye
			TEXT("EyeBlinkLeft"), TEXT("EyeLookDownLeft"), TEXT("EyeLookInLeft"), TEXT("EyeLookOutLeft"),
			TEXT("EyeLookUpLeft"), TEXT("EyeSquintLeft"), TEXT("EyeWideLeft"),
			// Right eye
			TEXT("EyeBlinkRight"), TEXT("EyeLookDownRight"), TEXT("EyeLookInRight"), TEXT("EyeLookOutRight"),
			TEXT("EyeLookUpRight"), TEXT("EyeSquintRight"), TEXT("EyeWideRight"),
			// Jaw
			TEXT("JawForward"), TEXT("JawLeft"), TEXT("JawRight"), TEXT("JawOpen"),
			// Mouth
			TEXT("MouthClose"), TEXT("MouthFunnel"), TEXT("MouthPucker"), TEXT("MouthLeft"), TEXT("MouthRight"),
			TEXT("MouthSmileLeft"), TEXT("MouthSmileRight"), TEXT("MouthFrownLeft"), TEXT("MouthFrownRight"),
			TEXT("MouthDimpleLeft"), TEXT("MouthDimpleRight"), TEXT("MouthStretchLeft"), TEXT("MouthStretchRight"),
			TEXT("MouthRollLower"), TEXT("MouthRollUpper"), TEXT("MouthShrugLower"), TEXT("MouthShrugUpper"),
			TEXT("MouthPressLeft"), TEXT("MouthPressRight"), TEXT("MouthLowerDownLeft"), TEXT("MouthLowerDownRight"),
			TEXT("MouthUpperUpLeft"), TEXT("MouthUpperUpRight"),
			// Brow
			TEXT("BrowDownLeft"), TEXT("BrowDownRight"), TEXT("BrowInnerUp"),
			TEXT("BrowOuterUpLeft"), TEXT("BrowOuterUpRight"),
			// Cheek
			TEXT("CheekPuff"), TEXT("CheekSquintLeft"), TEXT("CheekSquintRight"),
			// Nose / tongue
			TEXT("NoseSneerLeft"), TEXT("NoseSneerRight"), TEXT("TongueOut"),
			// Head / eye rotation curves (VMC senders do not emit these; published as 0)
			TEXT("HeadYaw"), TEXT("HeadPitch"), TEXT("HeadRoll"),
			TEXT("LeftEyeYaw"), TEXT("LeftEyePitch"), TEXT("LeftEyeRoll"),
			TEXT("RightEyeYaw"), TEXT("RightEyePitch"), TEXT("RightEyeRoll"),
		};
		return Names;
	}

	namespace
	{
		const TMap<FName, int32>& GetARKitNameToIndex()
		{
			static const TMap<FName, int32> Map = []()
			{
				TMap<FName, int32> M;
				const TArray<FName>& Names = GetARKitPropertyNames();
				M.Reserve(Names.Num());
				for (int32 i = 0; i < Names.Num(); ++i)
				{
					// FName lookups are case-insensitive, so this also matches the
					// camelCase ARKit names PerfectSync senders emit ("jawOpen").
					M.Add(Names[i], i);
				}
				return M;
			}();
			return Map;
		}

		struct FFallbackTarget
		{
			int32 Index;
			float Weight;
		};

		// Classic VRM expression keys -> weighted ARKit fan-out. Lossy by design: it keeps
		// non-PerfectSync senders serviceable (mouth opens, eyes blink, emotions read),
		// not studio-grade. VRM 0.x and 1.0 spellings are separate entries; casing is not
		// (FName). Weights are hand-picked against the MetaHuman ARKit mapping's response.
		const TMap<FName, TArray<FFallbackTarget>>& GetVrmToARKitFallback()
		{
			static const TMap<FName, TArray<FFallbackTarget>> Table = []()
			{
				const TMap<FName, int32>& Idx = GetARKitNameToIndex();
				auto I = [&Idx](const TCHAR* Name) { return Idx.FindChecked(FName(Name)); };

				TMap<FName, TArray<FFallbackTarget>> M;
				auto Add = [&M](const TCHAR* Key, std::initializer_list<FFallbackTarget> Targets)
				{
					M.Add(FName(Key), TArray<FFallbackTarget>(Targets));
				};

				// Visemes (VRM 0.x A/I/U/E/O, VRM 1.0 aa/ih/ou/ee/oh)
				const TArray<FFallbackTarget> VisA = { { I(TEXT("JawOpen")), 0.7f } };
				const TArray<FFallbackTarget> VisI = { { I(TEXT("MouthStretchLeft")), 0.6f }, { I(TEXT("MouthStretchRight")), 0.6f }, { I(TEXT("JawOpen")), 0.15f } };
				const TArray<FFallbackTarget> VisU = { { I(TEXT("MouthPucker")), 0.8f }, { I(TEXT("JawOpen")), 0.1f } };
				const TArray<FFallbackTarget> VisE = { { I(TEXT("MouthStretchLeft")), 0.4f }, { I(TEXT("MouthStretchRight")), 0.4f }, { I(TEXT("JawOpen")), 0.3f } };
				const TArray<FFallbackTarget> VisO = { { I(TEXT("MouthFunnel")), 0.8f }, { I(TEXT("JawOpen")), 0.35f } };
				M.Add(TEXT("A"), VisA);  M.Add(TEXT("aa"), VisA);
				M.Add(TEXT("I"), VisI);  M.Add(TEXT("ih"), VisI);
				M.Add(TEXT("U"), VisU);  M.Add(TEXT("ou"), VisU);
				M.Add(TEXT("E"), VisE);  M.Add(TEXT("ee"), VisE);
				M.Add(TEXT("O"), VisO);  M.Add(TEXT("oh"), VisO);

				// Blinks ("Blink" also covers VRM 1.0 "blink" via FName case folding)
				Add(TEXT("Blink"), { { I(TEXT("EyeBlinkLeft")), 1.0f }, { I(TEXT("EyeBlinkRight")), 1.0f } });
				const TArray<FFallbackTarget> BlinkL = { { I(TEXT("EyeBlinkLeft")), 1.0f } };
				const TArray<FFallbackTarget> BlinkR = { { I(TEXT("EyeBlinkRight")), 1.0f } };
				M.Add(TEXT("Blink_L"), BlinkL); M.Add(TEXT("blinkLeft"), BlinkL);
				M.Add(TEXT("Blink_R"), BlinkR); M.Add(TEXT("blinkRight"), BlinkR);

				// Emotions (VRM 0.x Joy/Angry/Sorrow/Fun, VRM 1.0 happy/angry/sad/relaxed/surprised)
				const TArray<FFallbackTarget> Joy = { { I(TEXT("MouthSmileLeft")), 0.8f }, { I(TEXT("MouthSmileRight")), 0.8f }, { I(TEXT("CheekSquintLeft")), 0.3f }, { I(TEXT("CheekSquintRight")), 0.3f } };
				M.Add(TEXT("Joy"), Joy); M.Add(TEXT("happy"), Joy);
				Add(TEXT("Angry"), { { I(TEXT("BrowDownLeft")), 1.0f }, { I(TEXT("BrowDownRight")), 1.0f }, { I(TEXT("NoseSneerLeft")), 0.3f }, { I(TEXT("NoseSneerRight")), 0.3f } });
				const TArray<FFallbackTarget> Sorrow = { { I(TEXT("BrowInnerUp")), 0.8f }, { I(TEXT("MouthFrownLeft")), 0.6f }, { I(TEXT("MouthFrownRight")), 0.6f } };
				M.Add(TEXT("Sorrow"), Sorrow); M.Add(TEXT("sad"), Sorrow);
				const TArray<FFallbackTarget> Fun = { { I(TEXT("MouthSmileLeft")), 0.5f }, { I(TEXT("MouthSmileRight")), 0.5f } };
				M.Add(TEXT("Fun"), Fun); M.Add(TEXT("relaxed"), Fun);
				Add(TEXT("surprised"), { { I(TEXT("EyeWideLeft")), 0.8f }, { I(TEXT("EyeWideRight")), 0.8f }, { I(TEXT("BrowInnerUp")), 0.6f }, { I(TEXT("BrowOuterUpLeft")), 0.6f }, { I(TEXT("BrowOuterUpRight")), 0.6f }, { I(TEXT("JawOpen")), 0.3f } });

				// Eye look (sender-side gaze curves; "LookLeft" = character looks screen-left)
				Add(TEXT("LookUp"), { { I(TEXT("EyeLookUpLeft")), 1.0f }, { I(TEXT("EyeLookUpRight")), 1.0f } });
				Add(TEXT("LookDown"), { { I(TEXT("EyeLookDownLeft")), 1.0f }, { I(TEXT("EyeLookDownRight")), 1.0f } });
				Add(TEXT("LookLeft"), { { I(TEXT("EyeLookOutLeft")), 1.0f }, { I(TEXT("EyeLookInRight")), 1.0f } });
				Add(TEXT("LookRight"), { { I(TEXT("EyeLookInLeft")), 1.0f }, { I(TEXT("EyeLookOutRight")), 1.0f } });
				return M;
			}();
			return Table;
		}
	}

	void MapVMCCurvesToARKit(const TMap<FName, float>& InCurves, TArray<float>& OutValues)
	{
		const int32 NumProperties = GetARKitPropertyNames().Num();
		OutValues.Reset(NumProperties);
		OutValues.AddZeroed(NumProperties);

		const TMap<FName, int32>& NameToIndex = GetARKitNameToIndex();
		const TMap<FName, TArray<FFallbackTarget>>& Fallback = GetVrmToARKitFallback();

		for (const TPair<FName, float>& Curve : InCurves)
		{
			FName Key = Curve.Key;
			FString KeyString = Key.ToString();
			if (KeyString.RemoveFromStart(TEXT("BlendShape.")))
			{
				Key = FName(*KeyString);
			}

			if (const int32* Index = NameToIndex.Find(Key))
			{
				OutValues[*Index] = FMath::Max(OutValues[*Index], Curve.Value);
				continue;
			}
			if (const TArray<FFallbackTarget>* Targets = Fallback.Find(Key))
			{
				for (const FFallbackTarget& Target : *Targets)
				{
					OutValues[Target.Index] = FMath::Max(OutValues[Target.Index], Curve.Value * Target.Weight);
				}
			}
			// Unknown keys are ignored: they are either morph names for VRM meshes (the
			// anim node's job) or sender extensions with no ARKit meaning.
		}
	}

	bool IsLiveLinkClientAvailable()
	{
		return IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName);
	}
}

FVrmVMCLiveLinkSource::FVrmVMCLiveLinkSource(FName InSubjectName, const FString& InServerAddress, int32 InPort)
	: SubjectName(InSubjectName)
	, ServerAddress(InServerAddress)
	, Port(InPort)
{
}

void FVrmVMCLiveLinkSource::ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid)
{
	Client = InClient;
	SourceGuid = InSourceGuid;

	FLiveLinkStaticDataStruct StaticDataStruct(FLiveLinkBaseStaticData::StaticStruct());
	FLiveLinkBaseStaticData* StaticData = StaticDataStruct.Cast<FLiveLinkBaseStaticData>();
	StaticData->PropertyNames = VrmVMCFaceLiveLink::GetARKitPropertyNames();
	Client->PushSubjectStaticData_AnyThread(FLiveLinkSubjectKey(SourceGuid, SubjectName),
		ULiveLinkBasicRole::StaticClass(), MoveTemp(StaticDataStruct));
	bStaticDataPushed = true;

	UE_LOG(LogVRM4UCapture, Display, TEXT("[FaceLiveLink] Subject '%s' publishing %d ARKit curves from VMC %s:%d."),
		*SubjectName.ToString(), VrmVMCFaceLiveLink::GetARKitPropertyNames().Num(), *ServerAddress, Port);
}

void FVrmVMCLiveLinkSource::Update()
{
	if (Client == nullptr || bShutdown || !bStaticDataPushed)
	{
		return;
	}
	UVRM4U_VMCSubsystem* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UVRM4U_VMCSubsystem>() : nullptr;
	if (Subsystem == nullptr)
	{
		return;
	}

	// Hold the subject at neutral until the endpoint has actually received a packet:
	// pushing before that would just stream zeros (and mark a dead sender "Active").
	float SecondsSinceLastPacket = 0.0f;
	if (!Subsystem->GetServerSecondsSinceLastPacket(ServerAddress, Port, SecondsSinceLastPacket) ||
		SecondsSinceLastPacket < 0.0f)
	{
		return;
	}
	bHasReceivedPackets = true;

	const TMap<FName, float> Curves = Subsystem->GetVMCAllCurveValues(ServerAddress, Port);
	VrmVMCFaceLiveLink::MapVMCCurvesToARKit(Curves, ValueScratch);

	FLiveLinkFrameDataStruct FrameDataStruct(FLiveLinkBaseFrameData::StaticStruct());
	FLiveLinkBaseFrameData* FrameData = FrameDataStruct.Cast<FLiveLinkBaseFrameData>();
	FrameData->WorldTime = FPlatformTime::Seconds();
	FrameData->PropertyValues = ValueScratch;
	Client->PushSubjectFrameData_AnyThread(FLiveLinkSubjectKey(SourceGuid, SubjectName), MoveTemp(FrameDataStruct));
}

bool FVrmVMCLiveLinkSource::RequestSourceShutdown()
{
	if (!bShutdown && Client != nullptr)
	{
		Client->RemoveSubject_AnyThread(FLiveLinkSubjectKey(SourceGuid, SubjectName));
	}
	bShutdown = true;
	return true;
}

FText FVrmVMCLiveLinkSource::GetSourceType() const
{
	return LOCTEXT("SourceType", "VRM4U VMC Face");
}

FText FVrmVMCLiveLinkSource::GetSourceMachineName() const
{
	return FText::FromString(FString::Printf(TEXT("%s:%d"), *ServerAddress, Port));
}

FText FVrmVMCLiveLinkSource::GetSourceStatus() const
{
	if (bShutdown)
	{
		return LOCTEXT("StatusShutdown", "Shut down");
	}
	return bHasReceivedPackets
		? LOCTEXT("StatusActive", "Active")
		: LOCTEXT("StatusWaiting", "Waiting for VMC packets");
}

#undef LOCTEXT_NAMESPACE
