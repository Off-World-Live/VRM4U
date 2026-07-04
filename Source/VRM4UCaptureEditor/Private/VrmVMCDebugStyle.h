#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateColor.h"

/**
 * Shared static data and color helpers for the VRM4U VMC debug panel.
 *
 * Centralizes:
 *   - The body-region grouping used to partition the 51 humanoid bones into
 *     collapsible UI sections.
 *   - The Perfect Sync blendshape set used to surface well-known curves
 *     above rig-specific ones.
 *   - Color helpers for override/mask/inactive row states.
 *
 * Pure static data. No state, no Slate types in the header.
 */
struct FVrmVMCDebugStyle
{
	struct FBoneGroup
	{
		FString Name;
		TArray<FString> HumanoidNames;
	};

	static const TArray<FBoneGroup>& GetBoneGroups();
	static const TSet<FString>& GetPerfectSyncCurveSet();

	// Returns the canonical mirror pair for a humanoid name, or empty if none.
	// Used by the per-row "Mirror" button to apply overrides to the L/R sibling.
	static FString GetMirrorName(const FString& HumanoidName);

	// Row state colors. All values pulled from FAppStyle-compatible palette
	// so the panel still looks at home in any editor theme.
	static FSlateColor GetLiveTextColor();
	static FSlateColor GetMaskedTextColor();
	static FSlateColor GetPreOverrideBadgeColor();
	static FSlateColor GetPostOverrideBadgeColor();
	static FSlateColor GetMaskedBadgeColor();
	static FSlateColor GetInactiveStreamColor();
	static FSlateColor GetConnectionGoodColor();
	static FSlateColor GetConnectionStaleColor();
	static FSlateColor GetConnectionDeadColor();
};