#include "SVrmVMCBoneRow.h"
#include "VrmVMCDebugStyle.h"

#include "VrmVMCBlueprintLibrary.h"
#include "Components/SkeletalMeshComponent.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "VrmVMCDebug"

namespace
{
	FText MakeBadgeText(bool bPre, bool bPost, bool bMasked)
	{
		if (bMasked) return LOCTEXT("BadgeMask", "MASK");
		if (bPost) return LOCTEXT("BadgePost", "POST");
		if (bPre) return LOCTEXT("BadgePre", "PRE");
		return FText::GetEmpty();
	}
}

void SVrmVMCBoneRow::Construct(const FArguments& InArgs)
{
	RigComponent = InArgs._Rig;
	HumanoidName = InArgs._HumanoidName;

	RefreshValues();

	ChildSlot
	[
		SAssignNew(RootBox, SVerticalBox)

		// Compact view: always visible.
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("NoBorder"))
			.Padding(FMargin(4.0f, 3.0f))
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(SBox)
					.WidthOverride(44.0f)
					.Visibility(this, &SVrmVMCBoneRow::GetBadgeVisibility)
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("RoundedWarning"))
						.BorderBackgroundColor(this, &SVrmVMCBoneRow::GetBadgeColor)
						.Padding(FMargin(4.0f, 1.0f))
						.HAlign(HAlign_Center)
						[
							SNew(STextBlock)
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
							.Text(this, &SVrmVMCBoneRow::GetBadgeText)
							.ColorAndOpacity(FSlateColor(FLinearColor::White))
						]
					]
				]

				// Bone name + last-applied summary. Click toggles expansion.
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "HoverHintOnly")
					.HAlign(HAlign_Left)
					.OnClicked(this, &SVrmVMCBoneRow::OnRowClicked)
					[
						SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
						.Text(this, &SVrmVMCBoneRow::GetCompactSummary)
						.ColorAndOpacity(this, &SVrmVMCBoneRow::GetCompactTextColor)
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("MaskBtn", "Mask"))
					.ToolTipText(LOCTEXT("MaskBtnTip",
					                     "Toggle mask. Masked bones do not contribute to the output pose."))
					.OnClicked(this, &SVrmVMCBoneRow::OnMaskClicked)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("ClearBtn", "Clear"))
					.ToolTipText(LOCTEXT("ClearBtnTip", "Clear all pre/post overrides and mask state on this bone."))
					.OnClicked(this, &SVrmVMCBoneRow::OnClearClicked)
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(20.0f, 2.0f, 4.0f, 6.0f)
		[
			SAssignNew(ExpandedContainer, SBorder)
			.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
			.Padding(FMargin(10.0f, 6.0f))
			.Visibility(this, &SVrmVMCBoneRow::GetExpandedVisibility)
			[
				SNullWidget::NullWidget
			]
		]
	];
}

void SVrmVMCBoneRow::RefreshValues()
{
	USkeletalMeshComponent* Rig = GetRig();
	if (Rig == nullptr)
	{
		bPreOverridden = false;
		bPostOverridden = false;
		bMasked = false;
		bHasLastApplied = false;
		return;
	}

	bPreOverridden = UVrmVMCBlueprintLibrary::IsPreRebaseOverridden(Rig, HumanoidName);
	bPostOverridden = UVrmVMCBlueprintLibrary::IsPostRebaseOverridden(Rig, HumanoidName);
	bMasked = UVrmVMCBlueprintLibrary::IsBoneMasked(Rig, HumanoidName);

	FQuat AppliedQuat = FQuat::Identity;
	FTransform AppliedTransform;
	bHasLastApplied = UVrmVMCBlueprintLibrary::GetVMCBoneTransformComponent(Rig, HumanoidName, AppliedTransform);
	if (bHasLastApplied)
	{
		AppliedQuat = AppliedTransform.GetRotation();
	}
	LastAppliedRotator = bHasLastApplied ? AppliedQuat.Rotator() : FRotator::ZeroRotator;
}

bool SVrmVMCBoneRow::MatchesFilter(const FString& FilterText) const
{
	if (FilterText.IsEmpty()) return true;
	return HumanoidName.ToString().Contains(FilterText, ESearchCase::IgnoreCase);
}

USkeletalMeshComponent* SVrmVMCBoneRow::GetRig() const
{
	return RigComponent.IsValid() ? RigComponent.Get() : nullptr;
}

// -- Compact accessors --

FText SVrmVMCBoneRow::GetCompactSummary() const
{
	const FString NameStr = HumanoidName.ToString();
	if (!bHasLastApplied)
	{
		return FText::FromString(FString::Printf(TEXT("%-26s  (no data)"), *NameStr));
	}

	return FText::FromString(FString::Printf(
		TEXT("%-26s  P:%+7.1f  Y:%+7.1f  R:%+7.1f"),
		*NameStr,
		LastAppliedRotator.Pitch,
		LastAppliedRotator.Yaw,
		LastAppliedRotator.Roll));
}

FSlateColor SVrmVMCBoneRow::GetCompactTextColor() const
{
	if (bMasked) return FVrmVMCDebugStyle::GetMaskedTextColor();
	if (!bHasLastApplied) return FVrmVMCDebugStyle::GetInactiveStreamColor();
	return FVrmVMCDebugStyle::GetLiveTextColor();
}

FText SVrmVMCBoneRow::GetBadgeText() const
{
	return MakeBadgeText(bPreOverridden, bPostOverridden, bMasked);
}

FSlateColor SVrmVMCBoneRow::GetBadgeColor() const
{
	if (bMasked) return FVrmVMCDebugStyle::GetMaskedBadgeColor();
	if (bPostOverridden) return FVrmVMCDebugStyle::GetPostOverrideBadgeColor();
	if (bPreOverridden) return FVrmVMCDebugStyle::GetPreOverrideBadgeColor();
	return FSlateColor(FLinearColor::Transparent);
}

EVisibility SVrmVMCBoneRow::GetBadgeVisibility() const
{
	return (bPreOverridden || bPostOverridden || bMasked) ? EVisibility::Visible : EVisibility::Hidden;
}

// -- Expansion --

FReply SVrmVMCBoneRow::OnRowClicked()
{
	bIsExpanded = !bIsExpanded;
	if (bIsExpanded)
	{
		// Seed entry fields from the current applied rotation so the user can
		// tweak from the live pose rather than starting from zero.
		Pitch = LastAppliedRotator.Pitch;
		Yaw = LastAppliedRotator.Yaw;
		Roll = LastAppliedRotator.Roll;
		RebuildExpandedSection();
	}
	return FReply::Handled();
}

EVisibility SVrmVMCBoneRow::GetExpandedVisibility() const
{
	return bIsExpanded ? EVisibility::Visible : EVisibility::Collapsed;
}

void SVrmVMCBoneRow::RebuildExpandedSection()
{
	if (!ExpandedContainer.IsValid()) return;

	ExpandedContainer->SetContent(
		SNew(SVerticalBox)

		// Row 1: Target toggle (Pre / Post).
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 2.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("TargetLabel", "Target:"))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.IsChecked(this, &SVrmVMCBoneRow::IsTargetActive, EOverrideTarget::PreRebase)
				.OnCheckStateChanged(this, &SVrmVMCBoneRow::OnTargetCheckChanged, EOverrideTarget::PreRebase)
				.ToolTipText(LOCTEXT("PreTargetTip", "Pre-rebase: pretend the VMC sender sent this rotation. Use when the source data is wrong for this bone. Most users want Post-rebase instead."))
				[
					SNew(STextBlock).Text(LOCTEXT("PreTargetLabel", "  Pre-rebase  "))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.IsChecked(this, &SVrmVMCBoneRow::IsTargetActive, EOverrideTarget::PostRebase)
				.OnCheckStateChanged(this, &SVrmVMCBoneRow::OnTargetCheckChanged, EOverrideTarget::PostRebase)
				.ToolTipText(LOCTEXT("PostTargetTip", "Post-rebase: force this rotation onto the bone, ignoring VMC data. The simple 'make the bone look like this' option. Default choice."))
				[
					SNew(STextBlock).Text(LOCTEXT("PostTargetLabel", "  Post-rebase  "))
				]
			]
		]

		// Row 2: Euler entry (Pitch / Yaw / Roll).
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 4.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SNumericEntryBox<float>)
				.Label()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("PitchLabel", "Pitch"))
					.ToolTipText(LOCTEXT("PitchTip", "Pitch: nod up and down (degrees)"))
				]
				.Value(this, &SVrmVMCBoneRow::GetPitch)
				.OnValueChanged(this, &SVrmVMCBoneRow::OnPitchChanged)
				.AllowSpin(true)
				.MinValue(-180.0f)
				.MaxValue(180.0f)
				.MinSliderValue(-180.0f)
				.MaxSliderValue(180.0f)
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SNumericEntryBox<float>)
				.Label()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("YawLabel", "Yaw"))
					.ToolTipText(LOCTEXT("YawTip", "Yaw: turn left and right (degrees)"))
				]
				.Value(this, &SVrmVMCBoneRow::GetYaw)
				.OnValueChanged(this, &SVrmVMCBoneRow::OnYawChanged)
				.AllowSpin(true)
				.MinValue(-180.0f)
				.MaxValue(180.0f)
				.MinSliderValue(-180.0f)
				.MaxSliderValue(180.0f)
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SNumericEntryBox<float>)
				.Label()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("RollLabel", "Roll"))
					.ToolTipText(LOCTEXT("RollTip", "Roll: twist about the bone's own axis (degrees)"))
				]
				.Value(this, &SVrmVMCBoneRow::GetRoll)
				.OnValueChanged(this, &SVrmVMCBoneRow::OnRollChanged)
				.AllowSpin(true)
				.MinValue(-180.0f)
				.MaxValue(180.0f)
				.MinSliderValue(-180.0f)
				.MaxSliderValue(180.0f)
			]
		]

		// Row 3: Action buttons.
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 4.0f, 0.0f, 2.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("ApplyBtn", "Apply"))
				.ToolTipText(LOCTEXT("ApplyBtnTip",
				                     "Push the current Euler values to the selected target (Pre or Post)."))
				.OnClicked(this, &SVrmVMCBoneRow::OnApplyClicked)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("ZeroBtn", "Zero"))
				.ToolTipText(LOCTEXT("ZeroBtnTip",
				                     "Force the bone to identity rotation by applying an override of (0, 0, 0)."))
				.OnClicked(this, &SVrmVMCBoneRow::OnZeroClicked)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("MirrorBtn", "Mirror"))
				.ToolTipText(LOCTEXT("MirrorBtnTip", "Copy the current override to the matching L/R sibling bone."))
				.OnClicked(this, &SVrmVMCBoneRow::OnMirrorClicked)
				.Visibility(FVrmVMCDebugStyle::GetMirrorName(HumanoidName.ToString()).IsEmpty()
					? EVisibility::Collapsed
					: EVisibility::Visible)
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SSpacer)
			]
		]
	);
}

// -- Quick actions --

FReply SVrmVMCBoneRow::OnMaskClicked()
{
	USkeletalMeshComponent* Rig = GetRig();
	if (Rig == nullptr) return FReply::Handled();
	UVrmVMCBlueprintLibrary::SetBoneMasked(Rig, HumanoidName, !bMasked);
	RefreshValues();
	return FReply::Handled();
}

FReply SVrmVMCBoneRow::OnClearClicked()
{
	USkeletalMeshComponent* Rig = GetRig();
	if (Rig == nullptr) return FReply::Handled();
	UVrmVMCBlueprintLibrary::ClearBoneState(Rig, HumanoidName);
	RefreshValues();
	return FReply::Handled();
}

// -- Expanded actions --

FReply SVrmVMCBoneRow::OnApplyClicked()
{
	USkeletalMeshComponent* Rig = GetRig();
	if (Rig == nullptr) return FReply::Handled();

	const FRotator Rot(Pitch, Yaw, Roll);
	const FQuat Quat = Rot.Quaternion();

	if (Target == EOverrideTarget::PreRebase)
	{
		UVrmVMCBlueprintLibrary::SetPreRebaseRotation(Rig, HumanoidName, Quat);
	}
	else
	{
		UVrmVMCBlueprintLibrary::SetPostRebaseRotation(Rig, HumanoidName, Quat);
	}
	RefreshValues();
	return FReply::Handled();
}

FReply SVrmVMCBoneRow::OnZeroClicked()
{
	Pitch = 0.0f;
	Yaw = 0.0f;
	Roll = 0.0f;
	return OnApplyClicked();
}

FReply SVrmVMCBoneRow::OnMirrorClicked()
{
	USkeletalMeshComponent* Rig = GetRig();
	if (Rig == nullptr) return FReply::Handled();

	const FString MirrorStr = FVrmVMCDebugStyle::GetMirrorName(HumanoidName.ToString());
	if (MirrorStr.IsEmpty()) return FReply::Handled();
	const FName MirrorName(*MirrorStr);

	const FRotator Rot(Pitch, Yaw, Roll);
	const FQuat Quat = Rot.Quaternion();

	if (Target == EOverrideTarget::PreRebase)
	{
		UVrmVMCBlueprintLibrary::SetPreRebaseRotation(Rig, MirrorName, Quat);
	}
	else
	{
		UVrmVMCBlueprintLibrary::SetPostRebaseRotation(Rig, MirrorName, Quat);
	}
	return FReply::Handled();
}

void SVrmVMCBoneRow::OnTargetChanged(EOverrideTarget NewTarget)
{
	Target = NewTarget;
}

ECheckBoxState SVrmVMCBoneRow::IsTargetActive(EOverrideTarget InTarget) const
{
	return Target == InTarget ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SVrmVMCBoneRow::OnTargetCheckChanged(ECheckBoxState State, EOverrideTarget InTarget)
{
	if (State == ECheckBoxState::Checked)
	{
		OnTargetChanged(InTarget);
	}
}

#undef LOCTEXT_NAMESPACE
