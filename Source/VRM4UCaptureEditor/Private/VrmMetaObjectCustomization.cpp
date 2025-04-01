// VRM4U Copyright (c) 2021-2024 Haruyoshi Yamamoto. This software is released under the MIT License.

#include "VrmMetaObjectCustomization.h"
#include "VrmMetaObject.h"
#include "AutoPopulateVrmMeta.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "PropertyCustomizationHelpers.h"

#define LOCTEXT_NAMESPACE "VrmMetaObjectCustomization"

TSharedRef<IDetailCustomization> FVrmMetaObjectCustomization::MakeInstance()
{
    return MakeShareable(new FVrmMetaObjectCustomization);
}

void FVrmMetaObjectCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
    // Get the object being customized
    TArray<TWeakObjectPtr<UObject>> Objects;
    DetailBuilder.GetObjectsBeingCustomized(Objects);
    
    if (Objects.Num() != 1)
    {
        return;
    }
    
    UVrmMetaObject* MetaObject = Cast<UVrmMetaObject>(Objects[0].Get());
    if (!MetaObject)
    {
        return;
    }
    
    // Add a custom row with auto-populate button to the Rendering category
    IDetailCategoryBuilder& RenderingCategory = DetailBuilder.EditCategory("Rendering", FText::GetEmpty(), ECategoryPriority::Important);
    
    RenderingCategory.AddCustomRow(LOCTEXT("AutoPopulateRow", "Auto Populate"))
        .NameContent()
        [
            SNew(STextBlock)
            .Text(LOCTEXT("AutoPopulateBoneMappings", "Auto-Populate Bone Mappings"))
            .Font(DetailBuilder.GetDetailFont())
        ]
        .ValueContent()
        .MinDesiredWidth(125.0f)
        .MaxDesiredWidth(125.0f)
        [
            SNew(SButton)
            .ContentPadding(FMargin(5.0f, 2.0f))
            .Text(LOCTEXT("AutoPopulateButton", "Auto-Populate"))
            .ToolTipText(LOCTEXT("AutoPopulateButtonTooltip", "Automatically populate bone mappings based on the detected skeleton type"))
            .OnClicked(FOnClicked::CreateRaw(this, &FVrmMetaObjectCustomization::OnAutoPopulateClicked, MetaObject))
        ];
}

FReply FVrmMetaObjectCustomization::OnAutoPopulateClicked(UVrmMetaObject* MetaObject)
{
    if (!MetaObject || !MetaObject->SkeletalMesh)
    {
        // Show error notification
        FNotificationInfo Info(LOCTEXT("NoSkeletalMesh", "Error: VrmMetaObject has no SkeletalMesh assigned"));
        Info.bUseLargeFont = false;
        Info.ExpireDuration = 5.0f;
        FSlateNotificationManager::Get().AddNotification(Info);
        return FReply::Handled();
    }
    
    ESkeletonType SkeletonType = UAutoPopulateVrmMeta::DetectSkeletonType(MetaObject->SkeletalMesh);
    
    if (SkeletonType == ESkeletonType::Unknown)
    {
        // Show error notification
        FNotificationInfo Info(LOCTEXT("UnknownSkeleton", "Error: Could not detect skeleton type"));
        Info.bUseLargeFont = false;
        Info.ExpireDuration = 5.0f;
        FSlateNotificationManager::Get().AddNotification(Info);
        return FReply::Handled();
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
        
        FNotificationInfo Info(FText::Format(
            LOCTEXT("SuccessfullyPopulated", "Successfully populated bone mappings for {0} skeleton"),
            FText::FromString(TypeName)
        ));
        Info.bUseLargeFont = false;
        Info.ExpireDuration = 5.0f;
        FSlateNotificationManager::Get().AddNotification(Info);
        
        // Mark the object as dirty so it can be saved
        MetaObject->Modify();
        
        // Force a refresh of the details panel
        FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
        PropertyEditorModule.NotifyCustomizationModuleChanged();
    }
    else
    {
        // Show error notification
        FNotificationInfo Info(LOCTEXT("FailedToPopulate", "Error: Failed to populate bone mappings"));
        Info.bUseLargeFont = false;
        Info.ExpireDuration = 5.0f;
        FSlateNotificationManager::Get().AddNotification(Info);
    }
    
    return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE