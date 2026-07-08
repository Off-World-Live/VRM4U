// VRM4U Copyright (c) 2021-2026 Haruyoshi Yamamoto. This software is released under the MIT License.

#include "VRM4UCaptureEditor.h"
#include "CoreMinimal.h"
#include "VRM4UCaptureEditorLog.h"
#include "Modules/ModuleManager.h"
#include "Internationalization/Internationalization.h"
#include "PropertyEditorModule.h"
#include "VrmMetaObject.h"
#include "VrmMetaObjectCustomization.h"
#include "VrmVMCDebugTabSpawner.h"
#include "VrmRetargetSetupMenuExtension.h"

#define LOCTEXT_NAMESPACE "VRM4UCapture"

DEFINE_LOG_CATEGORY(LogVRM4UCaptureEditor);

void FVRM4UCaptureEditorModule::StartupModule()
{
	UE_LOG(LogVRM4UCaptureEditor, Warning, TEXT("[VRM4UCaptureEditor] StartupModule ENTERED"));

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout(UVrmMetaObject::StaticClass()->GetFName(),
	                                         FOnGetDetailCustomizationInstance::CreateStatic(
		                                         &FVrmMetaObjectCustomization::MakeInstance));

	FVrmVMCDebugTabSpawner::Register();
	FVrmRetargetSetupMenuExtension::Register();

	UE_LOG(LogVRM4UCaptureEditor, Log, TEXT("VRM4UCaptureEditor module has started"));
}

void FVRM4UCaptureEditorModule::ShutdownModule()
{
	FVrmRetargetSetupMenuExtension::Unregister();
	FVrmVMCDebugTabSpawner::Unregister();

	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(
			"PropertyEditor");
		PropertyModule.UnregisterCustomClassLayout(UVrmMetaObject::StaticClass()->GetFName());
	}

	UE_LOG(LogVRM4UCaptureEditor, Log, TEXT("VRM4UCaptureEditor module has been shut down"));
}

IMPLEMENT_MODULE(FVRM4UCaptureEditorModule, VRM4UCaptureEditor)

#undef LOCTEXT_NAMESPACE