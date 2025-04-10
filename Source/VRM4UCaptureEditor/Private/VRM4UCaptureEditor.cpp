// VRM4U Copyright (c) 2021-2024 Haruyoshi Yamamoto. This software is released under the MIT License.

#include "VRM4UCaptureEditor.h"
#include "CoreMinimal.h"
#include "VRM4UCaptureEditorLog.h"
#include "Modules/ModuleManager.h"
#include "Internationalization/Internationalization.h"
#include "PropertyEditorModule.h"
#include "VrmMetaObject.h"
#include "VrmMetaObjectCustomization.h"

#define LOCTEXT_NAMESPACE "VRM4UCapture"

DEFINE_LOG_CATEGORY(LogVRM4UCaptureEditor);

void FVRM4UCaptureEditorModule::StartupModule()
{
	// Register detail customization
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout(UVrmMetaObject::StaticClass()->GetFName(), 
		FOnGetDetailCustomizationInstance::CreateStatic(&FVrmMetaObjectCustomization::MakeInstance));
    
	// Notify that the module has been loaded
	UE_LOG(LogVRM4UCaptureEditor, Log, TEXT("VRM4UCaptureEditor module has started"));
}

void FVRM4UCaptureEditorModule::ShutdownModule()
{
	// Unregister detail customization
	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomClassLayout(UVrmMetaObject::StaticClass()->GetFName());
	}
    
	// Notify that the module has been unloaded
	UE_LOG(LogVRM4UCaptureEditor, Log, TEXT("VRM4UCaptureEditor module has been shut down"));
}

IMPLEMENT_MODULE(FVRM4UCaptureEditorModule, VRM4UCaptureEditor)

#undef LOCTEXT_NAMESPACE