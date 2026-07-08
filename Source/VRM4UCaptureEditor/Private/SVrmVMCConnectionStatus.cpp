#include "SVrmVMCConnectionStatus.h"
#include "VrmVMCDebugStyle.h"

#include "VRM4U_VMCSubsystem.h"

#include "Engine/Engine.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "VrmVMCDebug"

namespace
{
	// Thresholds in seconds. Tuned for VMC's typical 30-60Hz packet cadence:
	// 250ms gives 5+ packet windows at 30Hz before nagging the user, 2s is the
	// "something is definitely wrong" line.
	constexpr float StaleThresholdSeconds = 0.25f;
	constexpr float DeadThresholdSeconds = 2.0f;
}

void SVrmVMCConnectionStatus::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 6.0f, 0.0f)
		[
			SNew(SImage)
			.Image(FAppStyle::GetBrush("Icons.BulletPoint"))
			.ColorAndOpacity(this, &SVrmVMCConnectionStatus::GetDotColor)
			.DesiredSizeOverride(FVector2D(12.0f, 12.0f))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(this, &SVrmVMCConnectionStatus::GetStatusText)
		]
	];
}

void SVrmVMCConnectionStatus::SetServerEndpoint(const FString& InAddress, int32 InPort)
{
	CurrentAddress = InAddress;
	CurrentPort = InPort;
	RefreshStatus();
}

void SVrmVMCConnectionStatus::RefreshStatus()
{
	if (CurrentAddress.IsEmpty() || CurrentPort <= 0)
	{
		Status = EStatus::NoServer;
		SecondsSinceLast = 0.0f;
		return;
	}

	UVRM4U_VMCSubsystem* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UVRM4U_VMCSubsystem>() : nullptr;
	if (Subsystem == nullptr)
	{
		Status = EStatus::NoServer;
		return;
	}

	float Seconds = 0.0f;
	const bool bFound = Subsystem->GetServerSecondsSinceLastPacket(CurrentAddress, CurrentPort, Seconds);
	if (!bFound)
	{
		Status = EStatus::NoServer;
		SecondsSinceLast = 0.0f;
		return;
	}

	if (Seconds < 0.0f)
	{
		Status = EStatus::NeverReceived;
		SecondsSinceLast = 0.0f;
		return;
	}

	SecondsSinceLast = Seconds;
	if (Seconds < StaleThresholdSeconds)
	{
		Status = EStatus::Good;
	}
	else if (Seconds < DeadThresholdSeconds)
	{
		Status = EStatus::Stale;
	}
	else
	{
		Status = EStatus::Dead;
	}
}

FSlateColor SVrmVMCConnectionStatus::GetDotColor() const
{
	switch (Status)
	{
	case EStatus::Good: return FVrmVMCDebugStyle::GetConnectionGoodColor();
	case EStatus::Stale: return FVrmVMCDebugStyle::GetConnectionStaleColor();
	case EStatus::Dead: return FVrmVMCDebugStyle::GetConnectionDeadColor();
	case EStatus::NeverReceived: return FVrmVMCDebugStyle::GetConnectionStaleColor();
	case EStatus::NoServer:
	default: return FVrmVMCDebugStyle::GetInactiveStreamColor();
	}
}

FText SVrmVMCConnectionStatus::GetStatusText() const
{
	switch (Status)
	{
	case EStatus::NoServer:
		return LOCTEXT("ConnNoServer", "no server selected");
	case EStatus::NeverReceived:
		return LOCTEXT("ConnNeverReceived", "server up, no packets yet");
	case EStatus::Good:
		{
			const int32 Ms = FMath::RoundToInt(SecondsSinceLast * 1000.0f);
			return FText::FromString(FString::Printf(TEXT("live (%dms ago)"), Ms));
		}
	case EStatus::Stale:
		{
			const int32 Ms = FMath::RoundToInt(SecondsSinceLast * 1000.0f);
			return FText::FromString(FString::Printf(TEXT("stale (%dms ago)"), Ms));
		}
	case EStatus::Dead:
		{
			return FText::FromString(FString::Printf(TEXT("dead (%.1fs ago)"), SecondsSinceLast));
		}
	default:
		return FText::GetEmpty();
	}
}

#undef LOCTEXT_NAMESPACE
