// VRM4U Copyright (c) 2021-2024 Haruyoshi Yamamoto. This software is released under the MIT License.

#include "VrmMetaObjectEditorExtension.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "VrmMetaObject.h"
#include "AutoPopulateVrmMeta.h"
#include "EditorStyleSet.h"
#include "Styling/AppStyle.h"

void FVrmMetaObjectEditorExtension::Register()
{
    ExtendContextMenu();
}

void FVrmMetaObjectEditorExtension::Unregister()
{
    // Cleanup if needed
}

void FVrmMetaObjectEditorExtension::ExtendContextMenu()
{
    FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
    TArray<FContentBrowserMenuExtender_SelectedAssets>& MenuExtenders = ContentBrowserModule.GetAllAssetViewContextMenuExtenders();
    
    MenuExtenders.Add(FContentBrowserMenuExtender_SelectedAssets::CreateStatic(&FVrmMetaObjectEditorExtension::OnExtendContentBrowserAssetSelectionMenu));
}

TSharedRef<FExtender> FVrmMetaObjectEditorExtension::OnExtendContentBrowserAssetSelectionMenu(const TArray<FAssetData>& SelectedAssets)
{
    TSharedRef<FExtender> Extender(new FExtender());
    
    bool bAnyVrmMetaObjects = false;
    for (const FAssetData& Asset : SelectedAssets)
    {
        if (Asset.AssetClassPath == UVrmMetaObject::StaticClass()->GetClassPathName())
        {
            bAnyVrmMetaObjects = true;
            break;
        }
    }
    
    if (bAnyVrmMetaObjects)
    {
        Extender->AddMenuExtension(
            "AssetContextAdvancedActions",
            EExtensionHook::After,
            nullptr,
            FMenuExtensionDelegate::CreateLambda([SelectedAssets](FMenuBuilder& MenuBuilder)
            {
                MenuBuilder.BeginSection("VrmMetaObjectActions", FText::FromString("VRM Meta Object"));
                {
                    MenuBuilder.AddMenuEntry(
                        FText::FromString("Auto-Populate Bone Mappings"),
                        FText::FromString("Automatically populate bone mappings based on detected skeleton type"),
                        FSlateIcon(),
                        FUIAction(
                            FExecuteAction::CreateStatic(&FVrmMetaObjectEditorExtension::OnAutoPopulateMenuEntryClicked, SelectedAssets)
                        )
                    );
                }
                MenuBuilder.EndSection();
            })
        );
    }
    
    return Extender;
}

void FVrmMetaObjectEditorExtension::OnAutoPopulateMenuEntryClicked(TArray<FAssetData> SelectedAssets)
{
    for (const FAssetData& Asset : SelectedAssets)
    {
        if (Asset.AssetClassPath == UVrmMetaObject::StaticClass()->GetClassPathName())
        {
            UVrmMetaObject* MetaObject = Cast<UVrmMetaObject>(Asset.GetAsset());
            if (MetaObject)
            {
                HandleAutoPopulate(MetaObject);
            }
        }
    }
}

void FVrmMetaObjectEditorExtension::HandleAutoPopulate(UVrmMetaObject* MetaObject)
{
    if (!MetaObject || !MetaObject->SkeletalMesh)
    {
        // Show error notification
        FNotificationInfo Info(FText::FromString("Error: VrmMetaObject has no SkeletalMesh assigned"));
        Info.bUseLargeFont = false;
        Info.ExpireDuration = 5.0f;
        FSlateNotificationManager::Get().AddNotification(Info);
        return;
    }
    
    ESkeletonType SkeletonType = UAutoPopulateVrmMeta::DetectSkeletonType(MetaObject->SkeletalMesh);
    
    if (SkeletonType == ESkeletonType::Unknown)
    {
        // Show error notification
        FNotificationInfo Info(FText::FromString("Error: Could not detect skeleton type"));
        Info.bUseLargeFont = false;
        Info.ExpireDuration = 5.0f;
        FSlateNotificationManager::Get().AddNotification(Info);
        return;
    }
    
    bool bSuccess = UAutoPopulateVrmMeta::AutoPopulateMetaObject(MetaObject, MetaObject->SkeletalMesh);
    
    if (bSuccess)
    {
        // Show success notification
        FString TypeName;
        switch (SkeletonType)
        {
        case ESkeletonType::Mixamo: TypeName = "Mixamo"; break;
        case ESkeletonType::MetaHuman: TypeName = "MetaHuman"; break;
        case ESkeletonType::DAZ: TypeName = "DAZ"; break;
        case ESkeletonType::VRM: TypeName = "VRM"; break;
        default: TypeName = "Unknown"; break;
        }
        
        FNotificationInfo Info(FText::FromString(FString::Printf(TEXT("Successfully populated bone mappings for %s skeleton"), *TypeName)));
        Info.bUseLargeFont = false;
        Info.ExpireDuration = 5.0f;
        FSlateNotificationManager::Get().AddNotification(Info);
        
        // Mark the object as dirty so it can be saved
        MetaObject->Modify();
    }
    else
    {
        // Show error notification
        FNotificationInfo Info(FText::FromString("Error: Failed to populate bone mappings"));
        Info.bUseLargeFont = false;
        Info.ExpireDuration = 5.0f;
        FSlateNotificationManager::Get().AddNotification(Info);
    }
}