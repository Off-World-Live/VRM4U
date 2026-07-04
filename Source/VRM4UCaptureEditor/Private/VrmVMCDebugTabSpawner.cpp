#include "VrmVMCDebugTabSpawner.h"

#include "SVrmVMCDebugPanel.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Styling/AppStyle.h"
#include "Internationalization/Internationalization.h"

#define LOCTEXT_NAMESPACE "VrmVMCDebug"

FDelegateHandle FVrmVMCDebugTabSpawner::PostEngineInitHandle;
bool FVrmVMCDebugTabSpawner::bRegistered = false;

FName FVrmVMCDebugTabSpawner::GetTabId()
{
	static const FName Id(TEXT("VrmVMCDebugPanel"));
	return Id;
}

void FVrmVMCDebugTabSpawner::Register()
{
	// VRM4UCaptureEditor loads at PreDefault, before WorkspaceMenuStructure is
	// guaranteed to be ready. Defer the actual registration until the engine
	// has finished initializing so we register against fully-initialized menu
	// structure singletons.
	//
	// If the engine has already finished initializing (e.g. a Live Coding /
	// hot reload after startup, when OnPostEngineInit will never fire again),
	// register immediately instead of binding a delegate that never runs.
	if (GIsRunning)
	{
		RegisterDeferred();
		return;
	}

	// Store the handle so Unregister can remove it; a re-registered binding is
	// what produces duplicate menu groups / tabs on module reload.
	PostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddStatic(&FVrmVMCDebugTabSpawner::RegisterDeferred);
}

void FVrmVMCDebugTabSpawner::RegisterDeferred()
{
	// Idempotent: a doubled OnPostEngineInit binding (or a manual call after the
	// delegate already fired) must not add the menu group / tab spawner twice.
	if (bRegistered)
	{
		return;
	}

	FModuleManager::Get().LoadModuleChecked<FWorkspaceMenuStructureModule>("WorkspaceMenuStructure");

	// Cache the menu group for this module lifetime so repeated RegisterDeferred
	// calls reuse it instead of appending a duplicate "VRM4U" submenu.
	static TSharedPtr<FWorkspaceItem> MenuGroupCache;
	if (!MenuGroupCache.IsValid())
	{
		MenuGroupCache = WorkspaceMenu::GetMenuStructure()
			.GetToolsCategory()
			->AddGroup(
				TEXT("VRM4UMenuGroup"),
				LOCTEXT("VRM4UMenuGroup", "VRM4U"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.StatsViewer"));
	}

	// HasTabSpawner guards against a duplicate spawner registration (which would
	// ensure() and add a second menu entry).
	if (!FGlobalTabmanager::Get()->HasTabSpawner(GetTabId()))
	{
		FGlobalTabmanager::Get()
				->RegisterNomadTabSpawner(GetTabId(), FOnSpawnTab::CreateStatic(&FVrmVMCDebugTabSpawner::OnSpawnTab))
			.SetDisplayName(LOCTEXT("VrmVMCDebugTabTitle", "VRM4U VMC Debug"))
			.SetTooltipText(LOCTEXT("VrmVMCDebugTabTooltip", "Inspect and override live VMC bone and curve state for active rigs."))
			.SetGroup(MenuGroupCache.ToSharedRef())
			.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.StatsViewer"));
	}

	bRegistered = true;
}

void FVrmVMCDebugTabSpawner::Unregister()
{
	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);
		PostEngineInitHandle.Reset();
	}

	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(GetTabId());
	}

	bRegistered = false;
}

TSharedRef<SDockTab> FVrmVMCDebugTabSpawner::OnSpawnTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SVrmVMCDebugPanel)
		];
}

#undef LOCTEXT_NAMESPACE