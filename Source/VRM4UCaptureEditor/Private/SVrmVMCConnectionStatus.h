#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

/**
 * Compact connection status indicator. Renders a colored dot and a textual
 * "Xms ago" / "no signal" readout for a single VMC server endpoint.
 *
 * Polls the subsystem on every tick from the parent panel rather than holding
 * its own ticker. Parent panel calls SetServerEndpoint when the user picks a
 * different server in the dropdown.
 *
 * Thresholds: <250ms = good (green), <2s = stale (amber), >2s or never = dead (red).
 */
class SVrmVMCConnectionStatus : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVrmVMCConnectionStatus) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// Called by the parent panel whenever the selected server changes. Pass
	// empty address / port=0 when no server is selected.
	void SetServerEndpoint(const FString& InAddress, int32 InPort);

	// Called by the parent panel's fast tick. Recomputes status from the subsystem.
	void RefreshStatus();

private:
	enum class EStatus : uint8
	{
		NoServer,
		NeverReceived,
		Good,
		Stale,
		Dead,
	};

	FSlateColor GetDotColor() const;
	FText GetStatusText() const;

	FString CurrentAddress;
	int32 CurrentPort = 0;

	EStatus Status = EStatus::NoServer;
	float SecondsSinceLast = 0.0f;
};