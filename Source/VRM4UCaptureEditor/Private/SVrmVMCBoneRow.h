#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class USkeletalMeshComponent;
class STextBlock;

/**
 * Per-bone row in the VRM4U VMC debug panel.
 *
 * Compact view: status badges (Pre/Post/Mask), humanoid name, last-applied
 * Euler readout in degrees, and always-visible Mask + Clear quick buttons.
 *
 * Expanded view: 3-component Euler entry (Pitch/Yaw/Roll), Pre/Post target
 * toggle, Apply / Zero / Mirror / Clear buttons. Click the row name to toggle.
 *
 * Holds a weak pointer to the rig component. Self-refreshes from the BP
 * library on every fast tick driven by the parent panel.
 *
 * Coordinate convention: the panel exposes rotations as Unreal FRotator
 * (Pitch/Yaw/Roll degrees) and converts to FQuat at the BP boundary via
 * FRotator::Quaternion. Last-applied display goes the other way via
 * FQuat::Rotator. This is the natural UE convention and matches what users
 * see in editor detail panels.
 */
class SVrmVMCBoneRow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVrmVMCBoneRow)
			: _Rig(nullptr)
			  , _HumanoidName(NAME_None)
		{
		}

		SLATE_ARGUMENT(TWeakObjectPtr<USkeletalMeshComponent>, Rig)
		SLATE_ARGUMENT(FName, HumanoidName)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// Called from the parent panel's fast tick. Pulls fresh values from the
	// BP library and updates the displayed text. Does not rebuild widgets.
	void RefreshValues();

	// The bone is "active" if it has any of: pre override, post override, or mask.
	// Parent panel uses this for the "Active only" filter without re-querying.
	bool IsActive() const { return bPreOverridden || bPostOverridden || bMasked; }

	// Returns true if the bone name passes the current search filter. Empty
	// filter passes everything. Case-insensitive substring match.
	bool MatchesFilter(const FString& FilterText) const;

	FName GetHumanoidName() const { return HumanoidName; }

private:
	enum class EOverrideTarget : uint8
	{
		PreRebase = 0,
		PostRebase = 1,
	};

	USkeletalMeshComponent* GetRig() const;

	void RebuildExpandedSection();

	// Compact-view accessors driven by attribute binding.
	FText GetCompactSummary() const;
	FSlateColor GetCompactTextColor() const;
	FText GetBadgeText() const;
	FSlateColor GetBadgeColor() const;
	EVisibility GetBadgeVisibility() const;

	// Expansion toggle.
	FReply OnRowClicked();
	EVisibility GetExpandedVisibility() const;

	// Quick-action buttons (always visible).
	FReply OnMaskClicked();
	FReply OnClearClicked();

	// Expanded-section actions.
	FReply OnApplyClicked();
	FReply OnZeroClicked();
	FReply OnMirrorClicked();
	void OnTargetChanged(EOverrideTarget NewTarget);
	ECheckBoxState IsTargetActive(EOverrideTarget Target) const;
	void OnTargetCheckChanged(ECheckBoxState State, EOverrideTarget Target);

	// Numeric entry accessors.
	TOptional<float> GetPitch() const { return Pitch; }
	TOptional<float> GetYaw() const { return Yaw; }
	TOptional<float> GetRoll() const { return Roll; }
	void OnPitchChanged(float NewValue) { Pitch = NewValue; }
	void OnYawChanged(float NewValue) { Yaw = NewValue; }
	void OnRollChanged(float NewValue) { Roll = NewValue; }

	// State.
	TWeakObjectPtr<USkeletalMeshComponent> RigComponent;
	FName HumanoidName = NAME_None;

	// Cached BP-library state, refreshed each tick.
	bool bPreOverridden = false;
	bool bPostOverridden = false;
	bool bMasked = false;
	bool bHasLastApplied = false;
	FRotator LastAppliedRotator = FRotator::ZeroRotator;

	// Expanded-state editor values. Initialized to current applied rotation
	// when the row is expanded for the first time, then user-driven.
	bool bIsExpanded = false;
	EOverrideTarget Target = EOverrideTarget::PostRebase;
	float Pitch = 0.0f;
	float Yaw = 0.0f;
	float Roll = 0.0f;

	TSharedPtr<class SVerticalBox> RootBox;
	TSharedPtr<class SBorder> ExpandedContainer;
};
