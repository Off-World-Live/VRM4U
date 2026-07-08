// VRM4U Copyright (c) 2021-2024 Haruyoshi Yamamoto. This software is released under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "PropertyHandle.h"
#include "DetailWidgetRow.h"
#include "DetailLayoutBuilder.h"

class UVrmMetaObject;

/**
 * Customizes the appearance of VrmMetaObject in the property editor
 */
class FVrmMetaObjectCustomization : public IDetailCustomization
{
public:
	/** Makes a new instance of this detail layout class for the specified detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	/** Handle auto-populate button click. MetaObject is held weakly: the asset can
	 *  be GC'd/closed between layout and click. */
	FReply OnAutoPopulateClicked(TWeakObjectPtr<UVrmMetaObject> MetaObjectWeak);

	/** Cached during CustomizeDetails so the click handler can force a refresh of the
	 *  details panel after populating (valid for the lifetime of this customization). */
	IDetailLayoutBuilder* CachedDetailBuilder = nullptr;
};