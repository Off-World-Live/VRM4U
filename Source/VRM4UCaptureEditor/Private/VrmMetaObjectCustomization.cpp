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
    
    // Get category for rendering
    IDetailCategoryBuilder& RenderingCategory = DetailBuilder.EditCategory("Rendering", FText::GetEmpty(), ECategoryPriority::Important);
    
    // Hide all properties we want to manually reorder
    DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UVrmMetaObject, Version));
    DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UVrmMetaObject, SkeletonType));
    DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UVrmMetaObject, SkeletalMesh));
    
    // Create new ordered custom rows
    
    // 1. Version
    TSharedRef<IPropertyHandle> VersionProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UVrmMetaObject, Version));
    RenderingCategory.AddProperty(VersionProperty);
    
    // 2. Skeletal Mesh
    TSharedRef<IPropertyHandle> SkeletalMeshProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UVrmMetaObject, SkeletalMesh));
    RenderingCategory.AddProperty(SkeletalMeshProperty);
    
    // 3. Skeleton Type
    TSharedRef<IPropertyHandle> SkeletonTypeProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UVrmMetaObject, SkeletonType));
    RenderingCategory.AddProperty(SkeletonTypeProperty);
    
    // 4. Auto-Populate Button
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
            .ToolTipText(LOCTEXT("AutoPopulateButtonTooltip", "Automatically populate bone mappings based on the selected or detected skeleton type"))
            .OnClicked(FOnClicked::CreateRaw(this, &FVrmMetaObjectCustomization::OnAutoPopulateClicked, MetaObject))
        ];
    
    // Ensure the humanoidBoneTable property appears after our auto-populate button
    TSharedRef<IPropertyHandle> HumanoidBoneTableProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UVrmMetaObject, humanoidBoneTable));
    RenderingCategory.AddProperty(HumanoidBoneTableProperty);
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
    
    // Determine skeleton type (either from user selection or auto-detection)
    ESkeletonType DetectedType = ESkeletonType::Unknown;
    FString TypeName;
    
    if (MetaObject->SkeletonType == EVrmSkeletonType::Auto)
    {
        // Auto detect
        DetectedType = UAutoPopulateVrmMeta::DetectSkeletonType(MetaObject->SkeletalMesh);
        
        if (DetectedType == ESkeletonType::Unknown)
        {
            // Show error notification
            FNotificationInfo Info(LOCTEXT("UnknownSkeleton", "Error: Could not auto-detect skeleton type"));
            Info.bUseLargeFont = false;
            Info.ExpireDuration = 5.0f;
            FSlateNotificationManager::Get().AddNotification(Info);
            return FReply::Handled();
        }
        
        // Set type name based on detected type
        switch (DetectedType)
        {
        case ESkeletonType::Mixamo: TypeName = "Mixamo"; break;
        case ESkeletonType::MetaHuman: TypeName = "MetaHuman"; break;
        case ESkeletonType::DAZ: TypeName = "DAZ"; break;
        case ESkeletonType::VRM: TypeName = "VRM"; break;
        default: TypeName = "Unknown"; break;
        }
    }
    else
    {
        // User explicitly selected a type
        switch (MetaObject->SkeletonType)
        {
        case EVrmSkeletonType::VRM:
            TypeName = "VRM";
            break;
        case EVrmSkeletonType::Mixamo:
            TypeName = "Mixamo";
            break;
        case EVrmSkeletonType::MetaHuman:
            TypeName = "MetaHuman";
            break;
        case EVrmSkeletonType::DAZ:
            TypeName = "DAZ";
            break;
        default:
            TypeName = "Unknown";
            break;
        }
    }
    
    bool bSuccess = UAutoPopulateVrmMeta::AutoPopulateMetaObject(MetaObject, MetaObject->SkeletalMesh);
    
    if (bSuccess)
    {
        // Show success notification with appropriate message
        FText SuccessMessage;
        if (MetaObject->SkeletonType == EVrmSkeletonType::Auto)
        {
            SuccessMessage = FText::Format(
                LOCTEXT("SuccessfullyPopulatedAutoDetect", "Successfully populated bone mappings for auto-detected {0} skeleton"),
                FText::FromString(TypeName)
            );
        }
        else
        {
            SuccessMessage = FText::Format(
                LOCTEXT("SuccessfullyPopulatedSelected", "Successfully populated bone mappings for {0} skeleton"),
                FText::FromString(TypeName)
            );
        }
        
        FNotificationInfo Info(SuccessMessage);
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