#include "SVrmVMCDebugPanel.h"
#include "SVrmVMCBoneRow.h"
#include "SVrmVMCCurveRow.h"
#include "SVrmVMCConnectionStatus.h"
#include "VrmVMCDebugStyle.h"

#include "VRM4U_VMCSubsystem.h"
#include "VrmVMCBlueprintLibrary.h"

#include "Engine/Engine.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Containers/Ticker.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "VrmVMCDebug"

namespace
{
	constexpr float SlowTickInterval = 0.5f;
	constexpr float FastTickInterval = 0.1f;

	FString GetRigDisplayName(USkeletalMeshComponent* Comp)
	{
		if (Comp == nullptr) return TEXT("<null>");
		const AActor* Owner = Comp->GetOwner();
		if (Owner != nullptr)
		{
			return Owner->GetActorNameOrLabel();
		}
		return Comp->GetName();
	}

	TSet<FName> KeysAsSet(const TMap<FName, float>& Map)
	{
		TSet<FName> Out;
		Out.Reserve(Map.Num());
		for (const auto& Pair : Map) { Out.Add(Pair.Key); }
		return Out;
	}
}

void SVrmVMCDebugPanel::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(8.0f)
		[
			SNew(SVerticalBox)

			// -- Header: connection status + server + rig.
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 12.0f, 0.0f)
				[
					SAssignNew(ConnectionStatus, SVrmVMCConnectionStatus)
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ServerLabel", "Server"))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SAssignNew(ServerCombo, SComboBox<TSharedPtr<FString>>)
						.OptionsSource(&ServerOptions)
						.OnGenerateWidget(this, &SVrmVMCDebugPanel::MakeServerOption)
						.OnSelectionChanged(this, &SVrmVMCDebugPanel::OnServerSelectionChanged)
						[
							SNew(STextBlock).Text(this, &SVrmVMCDebugPanel::GetSelectedServerText)
						]
					]
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("RigLabel", "Rig"))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SAssignNew(RigCombo, SComboBox<TSharedPtr<FRigOption>>)
						.OptionsSource(&RigOptions)
						.OnGenerateWidget(this, &SVrmVMCDebugPanel::MakeRigOption)
						.OnSelectionChanged(this, &SVrmVMCDebugPanel::OnRigSelectionChanged)
						[
							SNew(STextBlock).Text(this, &SVrmVMCDebugPanel::GetSelectedRigText)
						]
					]
				]
			]

			// -- Compare-to-ghost row: live per-bone alignment-error readout.
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CompareLabel", "Compare vs (ghost)"))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 12.0f, 0.0f)
				[
					SNew(SBox)
					.MinDesiredWidth(150.0f)
					[
						SAssignNew(ReferenceCombo, SComboBox<TSharedPtr<FRigOption>>)
						.OptionsSource(&RigOptions)
						.OnGenerateWidget(this, &SVrmVMCDebugPanel::MakeRigOption)
						.OnSelectionChanged(this, &SVrmVMCDebugPanel::OnReferenceSelectionChanged)
						[
							SNew(STextBlock).Text(this, &SVrmVMCDebugPanel::GetSelectedReferenceText)
						]
					]
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(this, &SVrmVMCDebugPanel::GetCompareSummaryText)
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
			[
				SNew(SSeparator).Orientation(Orient_Horizontal)
			]

			// -- Tab buttons + filter + search.
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SCheckBox)
					.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
					.IsChecked(this, &SVrmVMCDebugPanel::IsTabActive, EActiveTab::Bones)
					.OnCheckStateChanged(this, &SVrmVMCDebugPanel::OnTabClicked, EActiveTab::Bones)
					[
						SNew(STextBlock).Text(LOCTEXT("BonesTab", "  Bones  "))
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(SCheckBox)
					.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
					.IsChecked(this, &SVrmVMCDebugPanel::IsTabActive, EActiveTab::Curves)
					.OnCheckStateChanged(this, &SVrmVMCDebugPanel::OnTabClicked, EActiveTab::Curves)
					[
						SNew(STextBlock).Text(LOCTEXT("CurvesTab", "  Curves  "))
					]
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SAssignNew(SearchBox, SEditableTextBox)
					.HintText(LOCTEXT("SearchHint", "Filter by name..."))
					.OnTextChanged(this, &SVrmVMCDebugPanel::OnSearchTextChanged)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(8.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SCheckBox)
					.IsChecked(this, &SVrmVMCDebugPanel::GetFilterActiveOnly)
					.OnCheckStateChanged(this, &SVrmVMCDebugPanel::OnFilterActiveOnlyChanged)
					[
						SNew(STextBlock).Text(LOCTEXT("FilterActiveOnly", "Active only"))
					]
				]
			]

			// -- Tab content (scrolling).
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SWidgetSwitcher)
				.WidgetIndex_Lambda([this]() { return static_cast<int32>(ActiveTab); })

				+ SWidgetSwitcher::Slot()
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SAssignNew(BoneGroupsContainer, SVerticalBox)
					]
				]

				+ SWidgetSwitcher::Slot()
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SAssignNew(CurveGroupsContainer, SVerticalBox)
					]
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				SNew(SSeparator).Orientation(Orient_Horizontal)
			]

			// -- Footer: clear-all + preset clipboard.
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 4.0f, 0.0f, 0.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("ClearAllBones", "Clear All Bones"))
					.OnClicked(this, &SVrmVMCDebugPanel::OnClearAllBonesClicked)
				]

				+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("ClearAllCurves", "Clear All Curves"))
					.OnClicked(this, &SVrmVMCDebugPanel::OnClearAllCurvesClicked)
				]

				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(SSpacer)
				]

				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("CopyState", "Copy State"))
					.ToolTipText(LOCTEXT("CopyStateTip",
					                     "Copy all overrides and masks on the selected rig to the clipboard as JSON."))
					.OnClicked(this, &SVrmVMCDebugPanel::OnCopyStateClicked)
				]

				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("PasteState", "Paste State"))
					.ToolTipText(LOCTEXT("PasteStateTip", "Apply overrides and masks from JSON in the clipboard."))
					.OnClicked(this, &SVrmVMCDebugPanel::OnPasteStateClicked)
				]
			]
		]
	];

	SlowTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateSP(this, &SVrmVMCDebugPanel::SlowTick),
		SlowTickInterval);

	FastTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateSP(this, &SVrmVMCDebugPanel::FastTick),
		FastTickInterval);

	RefreshServerList();
	RefreshRigList();
	RebuildBoneRows();
	RebuildCurveRows();
}

SVrmVMCDebugPanel::~SVrmVMCDebugPanel()
{
	if (SlowTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(SlowTickerHandle);
	}
	if (FastTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(FastTickerHandle);
	}
}

// -- Ticks --

bool SVrmVMCDebugPanel::SlowTick(float DeltaTime)
{
	RefreshServerList();
	RefreshRigList();

	USkeletalMeshComponent* CurrentRig = GetSelectedRig();
	if (LastRigForRebuild.Get() != CurrentRig)
	{
		LastRigForRebuild = CurrentRig;
		RebuildBoneRows();
		LastCurveKeySnapshot.Reset();
	}

	// Curve key set is data-driven by what the sender broadcasts. Detect new
	// keys appearing so we can rebuild the curve row list.
	UVRM4U_VMCSubsystem* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UVRM4U_VMCSubsystem>() : nullptr;
	if (Subsystem != nullptr && CurrentRig != nullptr)
	{
		const TMap<FName, float> AllCurves = Subsystem->GetVMCAllCurveValues(
			GetSelectedServerAddress(), GetSelectedServerPort());
		TSet<FName> CurrentKeys = KeysAsSet(AllCurves);
		if (CurrentKeys.Num() != LastCurveKeySnapshot.Num() || !CurrentKeys.Includes(LastCurveKeySnapshot))
		{
			LastCurveKeySnapshot = CurrentKeys;
			RebuildCurveRows();
		}
	}

	return true;
}

bool SVrmVMCDebugPanel::FastTick(float DeltaTime)
{
	if (ConnectionStatus.IsValid())
	{
		ConnectionStatus->RefreshStatus();
	}

	// Refresh values inside existing rows. Cheap pass since rows hold weak
	// pointers and the BP library accessors are O(1) lookups under the lock.
	for (const TSharedPtr<SVrmVMCBoneRow>& Row : BoneRows)
	{
		if (Row.IsValid()) Row->RefreshValues();
	}

	if (ActiveTab == EActiveTab::Curves)
	{
		UVRM4U_VMCSubsystem* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UVRM4U_VMCSubsystem>() : nullptr;
		TMap<FName, float> AllCurves;
		if (Subsystem != nullptr)
		{
			AllCurves = Subsystem->GetVMCAllCurveValues(GetSelectedServerAddress(), GetSelectedServerPort());
		}
		for (const TSharedPtr<SVrmVMCCurveRow>& Row : CurveRows)
		{
			if (!Row.IsValid()) continue;
			const float* Stream = AllCurves.Find(Row->GetCurveName());
			Row->RefreshValues(Stream != nullptr ? *Stream : 0.0f);
		}
	}

	ApplyRowVisibilityFilters();

	// Live alignment-error readout vs the optional ghost rig. Computed at the
	// fast-tick rate and cached; GetCompareSummaryText() just returns the cache.
	// Read-only and silent (no logging) so it can run every tick during tuning.
	{
		USkeletalMeshComponent* Target = GetSelectedRig();
		USkeletalMeshComponent* Reference = GetSelectedReference();
		if (Target == nullptr || Reference == nullptr)
		{
			CompareSummaryCache = FText::GetEmpty();
		}
		else if (Target == Reference)
		{
			CompareSummaryCache = LOCTEXT("CompareSameRig", "dir error: pick a different rig as the ghost");
		}
		else
		{
			TMap<FName, float> PerSeg;
			FName Worst = NAME_None;
			float WorstDeg = 0.0f;
			float AvgDeg = 0.0f;
			if (UVrmVMCBlueprintLibrary::CompareRigPoseDirections(Target, Reference, PerSeg, Worst, WorstDeg, AvgDeg))
			{
				// Show the top-3 worst segments (not just #1) so the offending arm
				// chain is visible at a glance: e.g. which of shoulder / upper arm /
				// forearm is driving the error.
				PerSeg.ValueSort([](const float A, const float B) { return A > B; });
				FString Top;
				int32 Shown = 0;
				for (const TPair<FName, float>& Pair : PerSeg)
				{
					if (Shown++ >= 3) break;
					Top += FString::Printf(TEXT("  %s %.0f"), *Pair.Key.ToString(), Pair.Value);
				}
				CompareSummaryCache = FText::FromString(FString::Printf(
					TEXT("dir error:  avg %.1f deg   |  worst:%s   (%d seg)"),
					AvgDeg, *Top, PerSeg.Num()));
			}
			else
			{
				// Localize the failure: report how many humanoid bones each rig
				// maps. 0 on a side = that rig's VMC node has no resolved meta /
				// humanoid table. Both > 0 but still no shared = the mapped bone
				// names don't match that skeleton (e.g. a name-prefix mismatch).
				const int32 TargetMapped = UVrmVMCBlueprintLibrary::GetMappedHumanoidNames(Target).Num();
				const int32 RefMapped = UVrmVMCBlueprintLibrary::GetMappedHumanoidNames(Reference).Num();
				FString Msg = FString::Printf(
					TEXT("dir error: no shared bones  (this rig maps %d, ghost maps %d)"),
					TargetMapped, RefMapped);
				if (TargetMapped == 0 || RefMapped == 0)
				{
					// The 0-mapping rig has no humanoid table on its VMC node. Tell
					// the user the exact fix instead of leaving a dead-end number.
					Msg += TEXT("  ->  the rig mapping 0 needs a Vrm Meta Object assigned on its VMC node (then restart PIE)");
				}
				CompareSummaryCache = FText::FromString(Msg);
			}
		}
	}

	return true;
}

// -- Server selector --

void SVrmVMCDebugPanel::RefreshServerList()
{
	UVRM4U_VMCSubsystem* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UVRM4U_VMCSubsystem>() : nullptr;
	if (Subsystem == nullptr) return;

	TArray<FString> CurrentServers = Subsystem->GetActiveVMCServers();
	if (CurrentServers != LastServerSnapshot)
	{
		LastServerSnapshot = CurrentServers;
		ServerOptions.Reset();
		for (const FString& Entry : CurrentServers)
		{
			ServerOptions.Add(MakeShared<FString>(Entry));
		}

		if (SelectedServer.IsValid())
		{
			const FString CurrentValue = *SelectedServer;
			TSharedPtr<FString>* Match = ServerOptions.FindByPredicate([&](const TSharedPtr<FString>& O)
			{
				return O.IsValid() && *O == CurrentValue;
			});
			SelectedServer = Match != nullptr ? *Match : (ServerOptions.Num() > 0 ? ServerOptions[0] : nullptr);
		}
		else if (ServerOptions.Num() > 0)
		{
			SelectedServer = ServerOptions[0];
		}

		if (ServerCombo.IsValid())
		{
			ServerCombo->RefreshOptions();
			ServerCombo->SetSelectedItem(SelectedServer);
		}
	}

	// Always update the connection status endpoint, since the selected server
	// might be unchanged but we still need to push it on first init.
	if (ConnectionStatus.IsValid())
	{
		ConnectionStatus->SetServerEndpoint(GetSelectedServerAddress(), GetSelectedServerPort());
	}
}

void SVrmVMCDebugPanel::OnServerSelectionChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
{
	SelectedServer = NewSelection;
	LastRigSnapshot.Reset();
	if (ConnectionStatus.IsValid())
	{
		ConnectionStatus->SetServerEndpoint(GetSelectedServerAddress(), GetSelectedServerPort());
	}
	RefreshRigList();
}

TSharedRef<SWidget> SVrmVMCDebugPanel::MakeServerOption(TSharedPtr<FString> Option) const
{
	return SNew(STextBlock).Text(FText::FromString(Option.IsValid() ? *Option : TEXT("<null>")));
}

FText SVrmVMCDebugPanel::GetSelectedServerText() const
{
	if (!SelectedServer.IsValid()) return LOCTEXT("NoServer", "(no active servers)");
	return FText::FromString(*SelectedServer);
}

// -- Rig selector --

void SVrmVMCDebugPanel::RefreshRigList()
{
	UVRM4U_VMCSubsystem* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UVRM4U_VMCSubsystem>() : nullptr;
	if (Subsystem == nullptr) return;

	FString Address;
	int32 Port = 0;
	if (SelectedServer.IsValid())
	{
		ParseServerAddress(*SelectedServer, Address, Port);
	}

	TArray<USkeletalMeshComponent*> CurrentRigs = (Port > 0)
		                                              ? Subsystem->GetRigsForServer(Address, Port)
		                                              : TArray<USkeletalMeshComponent*>();

	// Compare as a set, not an ordered array: GetRigsForServer's ordering is not
	// stable, so an order-only difference must not trigger a spurious rebuild.
	TSet<FRigOption> CurrentSet;
	CurrentSet.Reserve(CurrentRigs.Num());
	for (USkeletalMeshComponent* Rig : CurrentRigs)
	{
		if (Rig != nullptr) CurrentSet.Add(Rig);
	}
	if (CurrentSet.Num() == LastRigSnapshot.Num() && CurrentSet.Includes(LastRigSnapshot)) return;
	LastRigSnapshot = CurrentSet;

	// Remember the current Rig + Ghost by display NAME before rebuilding. PIE
	// restarts recreate the actors, so the old pointers die — but matching by
	// name keeps the same rig selected across restarts (no manual re-picking).
	const FString PrevRigName =
		(SelectedRig.IsValid() && SelectedRig->Get() != nullptr) ? GetRigDisplayName(SelectedRig->Get()) : FString();
	const FString PrevRefName =
		(SelectedReference.IsValid() && SelectedReference->Get() != nullptr)
			? GetRigDisplayName(SelectedReference->Get())
			: FString();

	RigOptions.Reset();
	for (USkeletalMeshComponent* Rig : CurrentRigs)
	{
		if (Rig != nullptr) RigOptions.Add(MakeShared<FRigOption>(Rig));
	}

	// Match by name: handles both same-session reorders and cross-PIE-restart
	// re-spawns. Returns the first live option with that display name.
	auto FindByName = [&](const FString& Name) -> TSharedPtr<FRigOption>
	{
		if (Name.IsEmpty()) return nullptr;
		for (const TSharedPtr<FRigOption>& O : RigOptions)
		{
			if (O.IsValid() && O->Get() != nullptr && GetRigDisplayName(O->Get()) == Name) return O;
		}
		return nullptr;
	};

	const TSharedPtr<FRigOption> RigMatch = FindByName(PrevRigName);
	SelectedRig = RigMatch.IsValid() ? RigMatch : (RigOptions.Num() > 0 ? RigOptions[0] : nullptr);

	// Ghost is optional — keep it by name, but don't auto-select a fallback.
	SelectedReference = FindByName(PrevRefName);

	if (RigCombo.IsValid())
	{
		RigCombo->RefreshOptions();
		RigCombo->SetSelectedItem(SelectedRig);
	}
	if (ReferenceCombo.IsValid())
	{
		ReferenceCombo->RefreshOptions();
		ReferenceCombo->SetSelectedItem(SelectedReference);
	}
}

void SVrmVMCDebugPanel::OnRigSelectionChanged(TSharedPtr<FRigOption> NewSelection,
                                              ESelectInfo::Type SelectInfo)
{
	SelectedRig = NewSelection;
}

TSharedRef<SWidget> SVrmVMCDebugPanel::MakeRigOption(TSharedPtr<FRigOption> Option) const
{
	USkeletalMeshComponent* Comp = Option.IsValid() ? Option->Get() : nullptr;
	return SNew(STextBlock).Text(FText::FromString(GetRigDisplayName(Comp)));
}

FText SVrmVMCDebugPanel::GetSelectedRigText() const
{
	USkeletalMeshComponent* Comp = SelectedRig.IsValid() ? SelectedRig->Get() : nullptr;
	if (Comp == nullptr) return LOCTEXT("NoRig", "(no rigs on this server)");
	return FText::FromString(GetRigDisplayName(Comp));
}

// -- Reference ("ghost") selector + compare readout --

void SVrmVMCDebugPanel::OnReferenceSelectionChanged(TSharedPtr<FRigOption> NewSelection, ESelectInfo::Type SelectInfo)
{
	SelectedReference = NewSelection;
}

FText SVrmVMCDebugPanel::GetSelectedReferenceText() const
{
	USkeletalMeshComponent* Comp = SelectedReference.IsValid() ? SelectedReference->Get() : nullptr;
	if (Comp == nullptr) return LOCTEXT("NoReference", "(none)");
	return FText::FromString(GetRigDisplayName(Comp));
}

FText SVrmVMCDebugPanel::GetCompareSummaryText() const
{
	return CompareSummaryCache;
}

// -- Tabs / filters / search --

void SVrmVMCDebugPanel::SetActiveTab(EActiveTab NewTab) { ActiveTab = NewTab; }

ECheckBoxState SVrmVMCDebugPanel::IsTabActive(EActiveTab Tab) const
{
	return ActiveTab == Tab ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SVrmVMCDebugPanel::OnTabClicked(ECheckBoxState State, EActiveTab Tab)
{
	if (State == ECheckBoxState::Checked) SetActiveTab(Tab);
}

void SVrmVMCDebugPanel::OnFilterActiveOnlyChanged(ECheckBoxState NewState)
{
	bFilterActiveOnly = (NewState == ECheckBoxState::Checked);
}

ECheckBoxState SVrmVMCDebugPanel::GetFilterActiveOnly() const
{
	return bFilterActiveOnly ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SVrmVMCDebugPanel::OnSearchTextChanged(const FText& NewText)
{
	SearchText = NewText.ToString();
}

// -- Row construction --

void SVrmVMCDebugPanel::RebuildBoneRows()
{
	if (!BoneGroupsContainer.IsValid()) return;

	BoneGroupsContainer->ClearChildren();
	BoneRows.Reset();
	BoneRowSlots.Reset();

	USkeletalMeshComponent* Rig = GetSelectedRig();
	if (Rig == nullptr)
	{
		BoneGroupsContainer->AddSlot().AutoHeight().Padding(8.0f)
		[
			SNew(STextBlock).Text(
				LOCTEXT("NoRigBones", "No rig selected. Start PIE with a VMC-bound rig in the world."))
		];
		return;
	}

	const TWeakObjectPtr<USkeletalMeshComponent> RigWeak(Rig);

	for (const FVrmVMCDebugStyle::FBoneGroup& Group : FVrmVMCDebugStyle::GetBoneGroups())
	{
		TSharedPtr<SVerticalBox> GroupBody;
		BoneGroupsContainer->AddSlot()
		                   .AutoHeight()
		                   .Padding(0.0f, 2.0f)
		[
			SNew(SExpandableArea)
			.InitiallyCollapsed(false)
			.AreaTitle(FText::FromString(Group.Name))
			.BodyContent()
			[
				SAssignNew(GroupBody, SVerticalBox)
			]
		];

		for (const FString& HumanoidNameStr : Group.HumanoidNames)
		{
			const FName HumanoidName(*HumanoidNameStr);

			TSharedPtr<SVrmVMCBoneRow> Row;
			TSharedPtr<SBox> Slot;

			GroupBody->AddSlot()
			         .AutoHeight()
			[
				SAssignNew(Slot, SBox)
				[
					SAssignNew(Row, SVrmVMCBoneRow)
					.Rig(RigWeak)
					.HumanoidName(HumanoidName)
				]
			];

			BoneRows.Add(Row);
			BoneRowSlots.Add(Slot);
		}
	}
}

void SVrmVMCDebugPanel::RebuildCurveRows()
{
	if (!CurveGroupsContainer.IsValid()) return;

	CurveGroupsContainer->ClearChildren();
	CurveRows.Reset();
	CurveRowSlots.Reset();

	USkeletalMeshComponent* Rig = GetSelectedRig();
	if (Rig == nullptr)
	{
		CurveGroupsContainer->AddSlot().AutoHeight().Padding(8.0f)
		[
			SNew(STextBlock).Text(LOCTEXT("NoRigCurves",
			                              "No rig selected. Start PIE with a VMC-bound rig in the world."))
		];
		return;
	}

	UVRM4U_VMCSubsystem* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UVRM4U_VMCSubsystem>() : nullptr;
	if (Subsystem == nullptr) return;

	const TMap<FName, float> AllCurves = Subsystem->GetVMCAllCurveValues(
		GetSelectedServerAddress(), GetSelectedServerPort());
	if (AllCurves.Num() == 0)
	{
		CurveGroupsContainer->AddSlot().AutoHeight().Padding(8.0f)
		[
			SNew(STextBlock).Text(LOCTEXT("NoCurves", "No curves streaming on this server yet."))
		];
		return;
	}

	// Partition into Perfect Sync vs Other, sorted alphabetically within each.
	const TSet<FString>& PerfectSync = FVrmVMCDebugStyle::GetPerfectSyncCurveSet();
	TArray<TPair<FName, float>> PSCurves;
	TArray<TPair<FName, float>> OtherCurves;
	for (const auto& Pair : AllCurves)
	{
		if (PerfectSync.Contains(Pair.Key.ToString()))
		{
			PSCurves.Add({Pair.Key, Pair.Value});
		}
		else
		{
			OtherCurves.Add({Pair.Key, Pair.Value});
		}
	}
	PSCurves.Sort([](const TPair<FName, float>& A, const TPair<FName, float>& B)
	{
		return A.Key.LexicalLess(B.Key);
	});
	OtherCurves.Sort([](const TPair<FName, float>& A, const TPair<FName, float>& B)
	{
		return A.Key.LexicalLess(B.Key);
	});

	const TWeakObjectPtr<USkeletalMeshComponent> RigWeak(Rig);

	auto AddGroup = [&](const FText& Title, const TArray<TPair<FName, float>>& Entries)
	{
		if (Entries.Num() == 0) return;

		TSharedPtr<SVerticalBox> GroupBody;
		CurveGroupsContainer->AddSlot()
		                    .AutoHeight()
		                    .Padding(0.0f, 2.0f)
		[
			SNew(SExpandableArea)
			.InitiallyCollapsed(false)
			.AreaTitle(Title)
			.BodyContent()
			[
				SAssignNew(GroupBody, SVerticalBox)
			]
		];

		for (const TPair<FName, float>& Entry : Entries)
		{
			TSharedPtr<SVrmVMCCurveRow> Row;
			TSharedPtr<SBox> Slot;

			GroupBody->AddSlot()
			         .AutoHeight()
			[
				SAssignNew(Slot, SBox)
				[
					SAssignNew(Row, SVrmVMCCurveRow)
					.Rig(RigWeak)
					.CurveName(Entry.Key)
					.StreamValue(Entry.Value)
				]
			];

			CurveRows.Add(Row);
			CurveRowSlots.Add(Slot);
		}
	};

	AddGroup(LOCTEXT("PerfectSyncGroup", "Perfect Sync"), PSCurves);
	AddGroup(LOCTEXT("OtherCurvesGroup", "Other"), OtherCurves);
}

void SVrmVMCDebugPanel::ApplyRowVisibilityFilters()
{
	for (int32 i = 0; i < BoneRows.Num() && i < BoneRowSlots.Num(); ++i)
	{
		const TSharedPtr<SVrmVMCBoneRow>& Row = BoneRows[i];
		const TSharedPtr<SBox>& Slot = BoneRowSlots[i];
		if (!Row.IsValid() || !Slot.IsValid()) continue;

		const bool bPassesActive = !bFilterActiveOnly || Row->IsActive();
		const bool bPassesSearch = Row->MatchesFilter(SearchText);
		Slot->SetVisibility((bPassesActive && bPassesSearch) ? EVisibility::Visible : EVisibility::Collapsed);
	}

	for (int32 i = 0; i < CurveRows.Num() && i < CurveRowSlots.Num(); ++i)
	{
		const TSharedPtr<SVrmVMCCurveRow>& Row = CurveRows[i];
		const TSharedPtr<SBox>& Slot = CurveRowSlots[i];
		if (!Row.IsValid() || !Slot.IsValid()) continue;

		const bool bPassesActive = !bFilterActiveOnly || Row->IsActive();
		const bool bPassesSearch = Row->MatchesFilter(SearchText);
		Slot->SetVisibility((bPassesActive && bPassesSearch) ? EVisibility::Visible : EVisibility::Collapsed);
	}
}

// -- Footer actions --

FReply SVrmVMCDebugPanel::OnClearAllBonesClicked()
{
	USkeletalMeshComponent* Rig = GetSelectedRig();
	if (Rig == nullptr) return FReply::Handled();
	UVrmVMCBlueprintLibrary::ClearAllPreRebaseOverrides(Rig);
	UVrmVMCBlueprintLibrary::ClearAllPostRebaseOverrides(Rig);
	UVrmVMCBlueprintLibrary::ClearAllMasks(Rig);
	return FReply::Handled();
}

FReply SVrmVMCDebugPanel::OnClearAllCurvesClicked()
{
	USkeletalMeshComponent* Rig = GetSelectedRig();
	if (Rig == nullptr) return FReply::Handled();
	UVrmVMCBlueprintLibrary::ClearAllCurveOverrides(Rig);
	UVrmVMCBlueprintLibrary::ClearAllCurveMasks(Rig);
	return FReply::Handled();
}

FReply SVrmVMCDebugPanel::OnCopyStateClicked()
{
	// Both branches toast: clipboard writes are otherwise invisible, and the
	// no-rig case used to fail silently.
	const FString Json = SerializeCurrentState();
	if (!Json.IsEmpty())
	{
		FPlatformApplicationMisc::ClipboardCopy(*Json);
		FNotificationInfo Info(LOCTEXT("CopyStateOk", "VMC overrides copied to clipboard as JSON."));
		Info.ExpireDuration = 3.0f;
		FSlateNotificationManager::Get().AddNotification(Info)
			->SetCompletionState(SNotificationItem::CS_Success);
	}
	else
	{
		FNotificationInfo Info(LOCTEXT("CopyStateEmpty", "Nothing copied: select a rig first (requires an active PIE session)."));
		Info.ExpireDuration = 4.0f;
		FSlateNotificationManager::Get().AddNotification(Info)
			->SetCompletionState(SNotificationItem::CS_Fail);
	}
	return FReply::Handled();
}

FReply SVrmVMCDebugPanel::OnPasteStateClicked()
{
	FString Clipboard;
	FPlatformApplicationMisc::ClipboardPaste(Clipboard);
	const bool bApplied = !Clipboard.IsEmpty() && ApplyStateFromJson(Clipboard);
	if (!bApplied)
	{
		FNotificationInfo Info(LOCTEXT("PasteStateFail", "Paste failed: clipboard has no valid VMC override JSON, or no rig is selected."));
		Info.ExpireDuration = 4.0f;
		FSlateNotificationManager::Get().AddNotification(Info)
			->SetCompletionState(SNotificationItem::CS_Fail);
	}
	return FReply::Handled();
}

// -- Clipboard preset serialization --

FString SVrmVMCDebugPanel::SerializeCurrentState() const
{
	USkeletalMeshComponent* Rig = GetSelectedRig();
	if (Rig == nullptr) return FString();

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("version"), 1);
	Root->SetStringField(TEXT("rig"), GetRigDisplayName(Rig));

	TSharedRef<FJsonObject> Bones = MakeShared<FJsonObject>();
	for (const TSharedPtr<SVrmVMCBoneRow>& Row : BoneRows)
	{
		if (!Row.IsValid()) continue;
		const FName Name = Row->GetHumanoidName();
		const bool bPre = UVrmVMCBlueprintLibrary::IsPreRebaseOverridden(Rig, Name);
		const bool bPost = UVrmVMCBlueprintLibrary::IsPostRebaseOverridden(Rig, Name);
		const bool bMasked = UVrmVMCBlueprintLibrary::IsBoneMasked(Rig, Name);
		if (!bPre && !bPost && !bMasked) continue;

		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		if (bMasked) Entry->SetBoolField(TEXT("masked"), true);

		if (bPre)
		{
			FQuat StoredPre = FQuat::Identity;
			if (UVrmVMCBlueprintLibrary::GetPreRebaseRotation(Rig, Name, StoredPre))
			{
				const FRotator R = StoredPre.Rotator();
				const TArray<TSharedPtr<FJsonValue>> Euler = {
					MakeShared<FJsonValueNumber>(R.Pitch),
					MakeShared<FJsonValueNumber>(R.Yaw),
					MakeShared<FJsonValueNumber>(R.Roll)
				};
				Entry->SetArrayField(TEXT("pre"), Euler);
			}
		}
		if (bPost)
		{
			FQuat StoredPost = FQuat::Identity;
			if (UVrmVMCBlueprintLibrary::GetPostRebaseRotation(Rig, Name, StoredPost))
			{
				const FRotator R = StoredPost.Rotator();
				const TArray<TSharedPtr<FJsonValue>> Euler = {
					MakeShared<FJsonValueNumber>(R.Pitch),
					MakeShared<FJsonValueNumber>(R.Yaw),
					MakeShared<FJsonValueNumber>(R.Roll)
				};
				Entry->SetArrayField(TEXT("post"), Euler);
			}
		}

		Bones->SetObjectField(Name.ToString(), Entry);
	}
	Root->SetObjectField(TEXT("bones"), Bones);

	TSharedRef<FJsonObject> Curves = MakeShared<FJsonObject>();
	for (const TSharedPtr<SVrmVMCCurveRow>& Row : CurveRows)
	{
		if (!Row.IsValid()) continue;
		const FName Name = Row->GetCurveName();
		const bool bOver = UVrmVMCBlueprintLibrary::IsCurveOverridden(Rig, Name);
		const bool bMasked = UVrmVMCBlueprintLibrary::IsCurveMasked(Rig, Name);
		if (!bOver && !bMasked) continue;

		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		if (bMasked) Entry->SetBoolField(TEXT("masked"), true);

		float Applied = 0.0f;
		if (bOver && UVrmVMCBlueprintLibrary::GetLastAppliedCurveValue(Rig, Name, Applied))
		{
			Entry->SetNumberField(TEXT("override"), Applied);
		}

		Curves->SetObjectField(Name.ToString(), Entry);
	}
	Root->SetObjectField(TEXT("curves"), Curves);

	FString Out;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root, Writer);
	return Out;
}

bool SVrmVMCDebugPanel::ApplyStateFromJson(const FString& Json)
{
	USkeletalMeshComponent* Rig = GetSelectedRig();
	if (Rig == nullptr) return false;

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return false;

	const TSharedPtr<FJsonObject>* Bones = nullptr;
	if (Root->TryGetObjectField(TEXT("bones"), Bones) && Bones != nullptr && Bones->IsValid())
	{
		for (const auto& Pair : (*Bones)->Values)
		{
			const FName Name(*Pair.Key);
			const TSharedPtr<FJsonObject>* Entry = nullptr;
			if (!Pair.Value->TryGetObject(Entry) || Entry == nullptr || !Entry->IsValid()) continue;

			bool bMasked = false;
			if ((*Entry)->TryGetBoolField(TEXT("masked"), bMasked) && bMasked)
			{
				UVrmVMCBlueprintLibrary::SetBoneMasked(Rig, Name, true);
			}

			const TArray<TSharedPtr<FJsonValue>>* PreArr = nullptr;
			if ((*Entry)->TryGetArrayField(TEXT("pre"), PreArr) && PreArr != nullptr && PreArr->Num() == 3)
			{
				const FRotator R((*PreArr)[0]->AsNumber(), (*PreArr)[1]->AsNumber(), (*PreArr)[2]->AsNumber());
				UVrmVMCBlueprintLibrary::SetPreRebaseRotation(Rig, Name, R.Quaternion());
			}

			const TArray<TSharedPtr<FJsonValue>>* PostArr = nullptr;
			if ((*Entry)->TryGetArrayField(TEXT("post"), PostArr) && PostArr != nullptr && PostArr->Num() == 3)
			{
				const FRotator R((*PostArr)[0]->AsNumber(), (*PostArr)[1]->AsNumber(), (*PostArr)[2]->AsNumber());
				UVrmVMCBlueprintLibrary::SetPostRebaseRotation(Rig, Name, R.Quaternion());
			}
		}
	}

	const TSharedPtr<FJsonObject>* Curves = nullptr;
	if (Root->TryGetObjectField(TEXT("curves"), Curves) && Curves != nullptr && Curves->IsValid())
	{
		for (const auto& Pair : (*Curves)->Values)
		{
			const FName Name(*Pair.Key);
			const TSharedPtr<FJsonObject>* Entry = nullptr;
			if (!Pair.Value->TryGetObject(Entry) || Entry == nullptr || !Entry->IsValid()) continue;

			bool bMasked = false;
			if ((*Entry)->TryGetBoolField(TEXT("masked"), bMasked) && bMasked)
			{
				UVrmVMCBlueprintLibrary::SetCurveMasked(Rig, Name, true);
			}

			double Val = 0.0;
			if ((*Entry)->TryGetNumberField(TEXT("override"), Val))
			{
				UVrmVMCBlueprintLibrary::SetCurveOverride(Rig, Name, static_cast<float>(Val));
			}
		}
	}

	return true;
}

// -- Helpers --

void SVrmVMCDebugPanel::ParseServerAddress(const FString& Formatted, FString& OutAddress, int32& OutPort)
{
	int32 ColonIdx = INDEX_NONE;
	if (Formatted.FindLastChar(TEXT(':'), ColonIdx))
	{
		OutAddress = Formatted.Left(ColonIdx);
		OutPort = FCString::Atoi(*Formatted.Mid(ColonIdx + 1));
	}
	else
	{
		OutAddress = Formatted;
		OutPort = 0;
	}
}

USkeletalMeshComponent* SVrmVMCDebugPanel::GetSelectedRig() const
{
	// .Get() returns null if the rig was GC'd since selection — callers must
	// null-check (RebuildBoneRows/RebuildCurveRows/footer/SlowTick all do).
	return SelectedRig.IsValid() ? SelectedRig->Get() : nullptr;
}

USkeletalMeshComponent* SVrmVMCDebugPanel::GetSelectedReference() const
{
	return SelectedReference.IsValid() ? SelectedReference->Get() : nullptr;
}

FString SVrmVMCDebugPanel::GetSelectedServerAddress() const
{
	if (!SelectedServer.IsValid()) return FString();
	FString Address;
	int32 Port = 0;
	ParseServerAddress(*SelectedServer, Address, Port);
	return Address;
}

int32 SVrmVMCDebugPanel::GetSelectedServerPort() const
{
	if (!SelectedServer.IsValid()) return 0;
	FString Address;
	int32 Port = 0;
	ParseServerAddress(*SelectedServer, Address, Port);
	return Port;
}

#undef LOCTEXT_NAMESPACE
