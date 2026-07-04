#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Containers/Ticker.h"

class USkeletalMeshComponent;
class SVerticalBox;
class SScrollBox;
class SEditableTextBox;
class SCheckBox;
class SVrmVMCBoneRow;
class SVrmVMCCurveRow;
class SVrmVMCConnectionStatus;
template <typename OptionType>
class SComboBox;

/**
 * Runtime debug panel for inspecting and overriding live VMC bone/curve state.
 * Nomad tab under Window > Developer Tools > VRM4U > VRM4U VMC Debug.
 *
 * Responsibilities:
 *   - Drive two tick frequencies. Structural tick (500ms) detects server/rig
 *     churn and rebuilds row lists. Fast tick (100ms) refreshes per-row values
 *     and connection status.
 *   - Own server + rig dropdowns, search box, "active only" filter, tab switch.
 *   - Lay out grouped bone rows (by body region) and grouped curve rows
 *     (Perfect Sync / Other).
 *   - Provide clipboard preset save/load.
 *
 * Per-row override logic, expansion state, and value editing all live inside
 * SVrmVMCBoneRow / SVrmVMCCurveRow. This panel is a coordinator.
 */
class SVrmVMCDebugPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVrmVMCDebugPanel)
		{
		}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SVrmVMCDebugPanel();

private:
	enum class EActiveTab : uint8
	{
		Bones = 0,
		Curves = 1,
	};

	// Rig selection is held weakly: a PIE rig can be GC'd out from under the
	// panel between ticks, so every access must go through .Get() and null-check.
	using FRigOption = TWeakObjectPtr<USkeletalMeshComponent>;

	// Two-rate polling. Slow rebuilds rows when structure changes (new server,
	// new rig, new curve key shows up in the stream). Fast just refreshes
	// values inside existing rows.
	bool SlowTick(float DeltaTime);
	bool FastTick(float DeltaTime);
	FTSTicker::FDelegateHandle SlowTickerHandle;
	FTSTicker::FDelegateHandle FastTickerHandle;

	// Header controls.
	void RefreshServerList();
	void OnServerSelectionChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
	TSharedRef<SWidget> MakeServerOption(TSharedPtr<FString> Option) const;
	FText GetSelectedServerText() const;

	void RefreshRigList();
	void OnRigSelectionChanged(TSharedPtr<FRigOption> NewSelection, ESelectInfo::Type SelectInfo);
	TSharedRef<SWidget> MakeRigOption(TSharedPtr<FRigOption> Option) const;
	FText GetSelectedRigText() const;

	// Optional reference ("ghost") rig for the live alignment-error readout.
	// Reuses RigOptions as its source. The compare is read-only diagnostics.
	void OnReferenceSelectionChanged(TSharedPtr<FRigOption> NewSelection, ESelectInfo::Type SelectInfo);
	FText GetSelectedReferenceText() const;
	FText GetCompareSummaryText() const;

	// Tabs + filter + search.
	void SetActiveTab(EActiveTab NewTab);
	ECheckBoxState IsTabActive(EActiveTab Tab) const;
	void OnTabClicked(ECheckBoxState State, EActiveTab Tab);

	void OnFilterActiveOnlyChanged(ECheckBoxState NewState);
	ECheckBoxState GetFilterActiveOnly() const;

	void OnSearchTextChanged(const FText& NewText);

	// Row construction. Bone rows are built once per rig change and grouped
	// by body region. Curve rows are rebuilt whenever the curve key set changes.
	void RebuildBoneRows();
	void RebuildCurveRows();
	void ApplyRowVisibilityFilters();

	// Footer actions.
	FReply OnClearAllBonesClicked();
	FReply OnClearAllCurvesClicked();
	FReply OnCopyStateClicked();
	FReply OnPasteStateClicked();

	// Clipboard preset format. Schema version field on the root so future
	// changes can migrate cleanly. Euler in degrees, only non-default state
	// is serialized. See header comment for shape.
	FString SerializeCurrentState() const;
	bool ApplyStateFromJson(const FString& Json);

	// Helpers.
	static void ParseServerAddress(const FString& Formatted, FString& OutAddress, int32& OutPort);
	USkeletalMeshComponent* GetSelectedRig() const;
	USkeletalMeshComponent* GetSelectedReference() const;
	FString GetSelectedServerAddress() const;
	int32 GetSelectedServerPort() const;

	// -- State --

	TArray<TSharedPtr<FString>> ServerOptions;
	TSharedPtr<FString> SelectedServer;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> ServerCombo;

	TArray<TSharedPtr<FRigOption>> RigOptions;
	TSharedPtr<FRigOption> SelectedRig;
	TWeakObjectPtr<USkeletalMeshComponent> LastRigForRebuild;
	TSharedPtr<SComboBox<TSharedPtr<FRigOption>>> RigCombo;

	// Reference ("ghost") rig + cached compare readout (recomputed on FastTick).
	TSharedPtr<FRigOption> SelectedReference;
	TSharedPtr<SComboBox<TSharedPtr<FRigOption>>> ReferenceCombo;
	FText CompareSummaryCache;

	EActiveTab ActiveTab = EActiveTab::Bones;
	bool bFilterActiveOnly = false;
	FString SearchText;

	TSharedPtr<SVerticalBox> BoneGroupsContainer;
	TSharedPtr<SVerticalBox> CurveGroupsContainer;
	TSharedPtr<SVrmVMCConnectionStatus> ConnectionStatus;
	TSharedPtr<SEditableTextBox> SearchBox;

	// Per-row references, kept so we can drive RefreshValues from FastTick
	// and apply visibility filters without rebuilding.
	TArray<TSharedPtr<SVrmVMCBoneRow>> BoneRows;
	TArray<TSharedPtr<SVrmVMCCurveRow>> CurveRows;
	TArray<TSharedPtr<class SBox>> BoneRowSlots;
	TArray<TSharedPtr<class SBox>> CurveRowSlots;

	TArray<FString> LastServerSnapshot;
	TSet<FRigOption> LastRigSnapshot;
	TSet<FName> LastCurveKeySnapshot;
};
