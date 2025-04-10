// VRM4U Copyright (c) 2021-2024 Haruyoshi Yamamoto. This software is released under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "EditorUndoClient.h"

class UVrmMetaObject;

class FVrmMetaObjectEditorExtension : public FEditorUndoClient
{
public:
	static void Register();
	static void Unregister();

private:
	static void ExtendContextMenu();
	static void HandleAutoPopulate(UVrmMetaObject* MetaObject);
	static TSharedRef<FExtender> OnExtendContentBrowserAssetSelectionMenu(const TArray<FAssetData>& SelectedAssets);
	static void OnAutoPopulateMenuEntryClicked(TArray<FAssetData> SelectedAssets);
};