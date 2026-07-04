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

    // Cache so OnAutoPopulateClicked can force a refresh after mutating the asset.
    CachedDetailBuilder = &DetailBuilder;

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
            .OnClicked(FOnClicked::CreateSP(this, &FVrmMetaObjectCustomization::OnAutoPopulateClicked,
                                            TWeakObjectPtr<UVrmMetaObject>(MetaObject)))
        ];
    
    // Ensure the humanoidBoneTable property appears after our auto-populate button
    TSharedRef<IPropertyHandle> HumanoidBoneTableProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UVrmMetaObject, humanoidBoneTable));
    RenderingCategory.AddProperty(HumanoidBoneTableProperty);
}

FReply FVrmMetaObjectCustomization::OnAutoPopulateClicked(TWeakObjectPtr<UVrmMetaObject> MetaObjectWeak)
{
    // Validation, type resolution, transaction, and notifications are shared
    // across all Auto-Populate entry points.
    const FVrmAutoPopulateUiResult Result =
        UAutoPopulateVrmMeta::AutoPopulateWithUi(MetaObjectWeak.Get(), /*bShowAssignReminder=*/true);

    if (Result.bSuccess && CachedDetailBuilder != nullptr)
    {
        // Refresh just this details panel so the new bone table rows appear.
        CachedDetailBuilder->ForceRefreshDetails();
    }

    return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE