#pragma once

#include "CoreMinimal.h"
#include "Delegates/IDelegateInstance.h"

class FSpawnTabArgs;
class SDockTab;

/**
 * Registers and unregisters the VRM4U VMC Debug tab with the global tab
 * manager. Called from the editor module's StartupModule and ShutdownModule.
 * The tab itself lives under the Window menu in a "VRM4U" submenu so future
 * VRM4U editor panels can be grouped alongside it.
 */
class FVrmVMCDebugTabSpawner
{
public:
	static void Register();
	static void Unregister();

private:
	static void RegisterDeferred();
	static TSharedRef<SDockTab> OnSpawnTab(const FSpawnTabArgs& Args);

	static FName GetTabId();

	// Handle for the OnPostEngineInit binding so Unregister can remove it.
	// Without this, every module reload (Live Coding / hot reload) adds another
	// binding, producing duplicate "VRM4U" menu groups and tab entries.
	static FDelegateHandle PostEngineInitHandle;

	// Guards against registering the tab spawner / menu group more than once.
	static bool bRegistered;
};