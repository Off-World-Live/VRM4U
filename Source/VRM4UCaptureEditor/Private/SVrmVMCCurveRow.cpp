#include "SVrmVMCCurveRow.h"
#include "VrmVMCDebugStyle.h"

#include "VrmVMCBlueprintLibrary.h"
#include "Components/SkeletalMeshComponent.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "VrmVMCDebug"

namespace
{
	FText MakeCurveBadgeText(bool bOverride, bool bMasked)
	{
		if (bMasked) return LOCTEXT("BadgeMaskCurve", "MASK");
		if (bOverride) return LOCTEXT("BadgeOverride", "OVER");
		return FText::GetEmpty();
	}
}

void SVrmVMCCurveRow::Construct(const FArguments& InArgs)
{
	RigComponent = InArgs._Rig;
	CurveName = InArgs._CurveName;
	StreamValue = InArgs._StreamValue;

	RefreshValues(StreamValue);

	ChildSlot
	[
		SNew(SVerticalBox)

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
					.Visibility(this, &SVrmVMCCurveRow::GetBadgeVisibility)
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("RoundedWarning"))
						.BorderBackgroundColor(this, &SVrmVMCCurveRow::GetBadgeColor)
						.Padding(FMargin(4.0f, 1.0f))
						.HAlign(HAlign_Center)
						[
							SNew(STextBlock)
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
							.Text(this, &SVrmVMCCurveRow::GetBadgeText)
							.ColorAndOpacity(FSlateColor(FLinearColor::White))
						]
					]
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "HoverHintOnly")
					.HAlign(HAlign_Left)
					.OnClicked(this, &SVrmVMCCurveRow::OnRowClicked)
					[
						SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
						.Text(this, &SVrmVMCCurveRow::GetCompactSummary)
						.ColorAndOpacity(this, &SVrmVMCCurveRow::GetCompactTextColor)
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("MaskBtn", "Mask"))
					.ToolTipText(LOCTEXT("MaskBtnTip", "Toggle mask: skip applying this curve, leaving the upstream value intact"))
					.OnClicked(this, &SVrmVMCCurveRow::OnMaskClicked)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("ClearBtn", "Clear"))
					.ToolTipText(LOCTEXT("ClearBtnTip", "Remove this curve's override and mask, returning it to the live stream value"))
					.OnClicked(this, &SVrmVMCCurveRow::OnClearClicked)
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
			.Visibility(this, &SVrmVMCCurveRow::GetExpandedVisibility)
			[
				SNullWidget::NullWidget
			]
		]
	];
}

void SVrmVMCCurveRow::RefreshValues(float NewStreamValue)
{
	StreamValue = NewStreamValue;

	USkeletalMeshComponent* Rig = GetRig();
	if (Rig == nullptr)
	{
		bOverridden = false;
		bMasked = false;
		bHasLastApplied = false;
		return;
	}

	bOverridden = UVrmVMCBlueprintLibrary::IsCurveOverridden(Rig, CurveName);
	bMasked = UVrmVMCBlueprintLibrary::IsCurveMasked(Rig, CurveName);

	float Applied = 0.0f;
	bHasLastApplied = UVrmVMCBlueprintLibrary::GetLastAppliedCurveValue(Rig, CurveName, Applied);
	LastAppliedValue = bHasLastApplied ? Applied : 0.0f;
}

bool SVrmVMCCurveRow::MatchesFilter(const FString& FilterText) const
{
	if (FilterText.IsEmpty()) return true;
	return CurveName.ToString().Contains(FilterText, ESearchCase::IgnoreCase);
}

USkeletalMeshComponent* SVrmVMCCurveRow::GetRig() const
{
	return RigComponent.IsValid() ? RigComponent.Get() : nullptr;
}

FText SVrmVMCCurveRow::GetCompactSummary() const
{
	const FString NameStr = CurveName.ToString();
	if (bHasLastApplied)
	{
		return FText::FromString(FString::Printf(
			TEXT("%-32s  stream:%.3f  applied:%.3f"),
			*NameStr, StreamValue, LastAppliedValue));
	}
	return FText::FromString(FString::Printf(
		TEXT("%-32s  stream:%.3f  applied:---"),
		*NameStr, StreamValue));
}

FSlateColor SVrmVMCCurveRow::GetCompactTextColor() const
{
	if (bMasked) return FVrmVMCDebugStyle::GetMaskedTextColor();
	return FVrmVMCDebugStyle::GetLiveTextColor();
}

FText SVrmVMCCurveRow::GetBadgeText() const
{
	return MakeCurveBadgeText(bOverridden, bMasked);
}

FSlateColor SVrmVMCCurveRow::GetBadgeColor() const
{
	if (bMasked) return FVrmVMCDebugStyle::GetMaskedBadgeColor();
	if (bOverridden) return FVrmVMCDebugStyle::GetPostOverrideBadgeColor();
	return FSlateColor(FLinearColor::Transparent);
}

EVisibility SVrmVMCCurveRow::GetBadgeVisibility() const
{
	return (bOverridden || bMasked) ? EVisibility::Visible : EVisibility::Hidden;
}

FReply SVrmVMCCurveRow::OnRowClicked()
{
	bIsExpanded = !bIsExpanded;
	if (bIsExpanded)
	{
		EntryValue = bHasLastApplied ? LastAppliedValue : StreamValue;
		RebuildExpandedSection();
	}
	return FReply::Handled();
}

EVisibility SVrmVMCCurveRow::GetExpandedVisibility() const
{
	return bIsExpanded ? EVisibility::Visible : EVisibility::Collapsed;
}

void SVrmVMCCurveRow::RebuildExpandedSection()
{
	if (!ExpandedContainer.IsValid()) return;

	ExpandedContainer->SetContent(
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 4.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SSlider)
				.MinValue(0.0f)
				.MaxValue(1.0f)
				.Value_Lambda([this]() { return EntryValue; })
				.OnValueChanged(this, &SVrmVMCCurveRow::OnEntryChanged)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(80.0f)
				[
					SNew(SNumericEntryBox<float>)
					.Value(this, &SVrmVMCCurveRow::GetEntryValue)
					.OnValueChanged(this, &SVrmVMCCurveRow::OnEntryChanged)
					.MinValue(0.0f)
					.MaxValue(1.0f)
				]
			]
		]

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
				.OnClicked(this, &SVrmVMCCurveRow::OnApplyClicked)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("ZeroBtn", "Zero"))
				.ToolTipText(LOCTEXT("CurveZeroTip", "Apply an override of 0.0, forcing the curve flat."))
				.OnClicked(this, &SVrmVMCCurveRow::OnZeroClicked)
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SSpacer)
			]
		]
	);
}

FReply SVrmVMCCurveRow::OnMaskClicked()
{
	USkeletalMeshComponent* Rig = GetRig();
	if (Rig == nullptr) return FReply::Handled();
	UVrmVMCBlueprintLibrary::SetCurveMasked(Rig, CurveName, !bMasked);
	RefreshValues(StreamValue);
	return FReply::Handled();
}

FReply SVrmVMCCurveRow::OnClearClicked()
{
	USkeletalMeshComponent* Rig = GetRig();
	if (Rig == nullptr) return FReply::Handled();
	UVrmVMCBlueprintLibrary::ClearCurveOverride(Rig, CurveName);
	UVrmVMCBlueprintLibrary::SetCurveMasked(Rig, CurveName, false);
	RefreshValues(StreamValue);
	return FReply::Handled();
}

FReply SVrmVMCCurveRow::OnApplyClicked()
{
	USkeletalMeshComponent* Rig = GetRig();
	if (Rig == nullptr) return FReply::Handled();
	UVrmVMCBlueprintLibrary::SetCurveOverride(Rig, CurveName, EntryValue);
	RefreshValues(StreamValue);
	return FReply::Handled();
}

FReply SVrmVMCCurveRow::OnZeroClicked()
{
	EntryValue = 0.0f;
	return OnApplyClicked();
}

#undef LOCTEXT_NAMESPACE
