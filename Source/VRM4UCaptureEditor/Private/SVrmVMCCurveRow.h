#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class USkeletalMeshComponent;

/**
 * Per-curve row in the VRM4U VMC debug panel.
 *
 * Compact view: status badges (Override/Mask), curve name, stream value, and
 * applied value side by side. Mask + Clear quick buttons.
 *
 * Expanded view: 0-1 slider + numeric entry, Apply / Zero / Clear buttons.
 *
 * Curves don't have a canonical mirror concept (Perfect Sync L/R blendshapes
 * are explicitly distinct keys), so no mirror button.
 */
class SVrmVMCCurveRow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVrmVMCCurveRow)
			: _Rig(nullptr)
			  , _CurveName(NAME_None)
			  , _StreamValue(0.0f)
		{
		}

		SLATE_ARGUMENT(TWeakObjectPtr<USkeletalMeshComponent>, Rig)
		SLATE_ARGUMENT(FName, CurveName)
		SLATE_ARGUMENT(float, StreamValue)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// Called on the parent panel's fast tick. Refreshes override/mask flags
	// and applied value cache. Also accepts the latest stream value snapshot.
	void RefreshValues(float NewStreamValue);

	bool IsActive() const { return bOverridden || bMasked; }

	bool MatchesFilter(const FString& FilterText) const;

	FName GetCurveName() const { return CurveName; }

private:
	USkeletalMeshComponent* GetRig() const;

	void RebuildExpandedSection();

	FText GetCompactSummary() const;
	FSlateColor GetCompactTextColor() const;
	FText GetBadgeText() const;
	FSlateColor GetBadgeColor() const;
	EVisibility GetBadgeVisibility() const;

	FReply OnRowClicked();
	EVisibility GetExpandedVisibility() const;

	FReply OnMaskClicked();
	FReply OnClearClicked();
	FReply OnApplyClicked();
	FReply OnZeroClicked();

	TOptional<float> GetEntryValue() const { return EntryValue; }
	void OnEntryChanged(float NewValue) { EntryValue = FMath::Clamp(NewValue, 0.0f, 1.0f); }

	// State.
	TWeakObjectPtr<USkeletalMeshComponent> RigComponent;
	FName CurveName = NAME_None;

	bool bOverridden = false;
	bool bMasked = false;
	bool bHasLastApplied = false;
	float LastAppliedValue = 0.0f;
	float StreamValue = 0.0f;

	bool bIsExpanded = false;
	float EntryValue = 0.0f;

	TSharedPtr<class SBorder> ExpandedContainer;
};
