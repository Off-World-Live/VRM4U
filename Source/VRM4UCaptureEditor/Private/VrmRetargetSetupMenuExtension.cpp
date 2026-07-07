// VRM4U Copyright (c) 2021-2026 Haruyoshi Yamamoto. This software is released under the MIT License.

#include "VrmRetargetSetupMenuExtension.h"
#include "VrmRetargetSetupUtil.h"
#include "VrmSceneLint.h"
#include "VRM4UCaptureEditorLog.h"

#include "Misc/EngineVersionComparison.h"

// The wizard this menu invokes requires the UE 5.6 retargeter controller API.
#if UE_VERSION_OLDER_THAN(5, 6, 0)

void FVrmRetargetSetupMenuExtension::Register() {}
void FVrmRetargetSetupMenuExtension::Unregister() {}

#else

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "VrmVMCFaceLiveLinkComponent.h"
#include "ToolMenus.h"
#include "LevelEditorMenuContext.h"
#include "Elements/Framework/TypedElementSelectionSet.h"
#include "Editor.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Docking/TabManager.h"
#include "Misc/ScopedSlowTask.h"

#define LOCTEXT_NAMESPACE "VrmRetargetSetup"

namespace
{
	const FName MenuOwnerName(TEXT("VRM4URetargetSetup"));
	FDelegateHandle StartupCallbackHandle;

	// The component the wizard should target on this actor: a skeletal mesh that is NOT a
	// VRM humanoid (those are sources). MetaHuman actors carry several skeletal meshes
	// (body, face, ...), so prefer the one with the most bones below a pelvis-style root,
	// in practice the body. Simple tie-break: most bones wins among non-VRM meshes.
	USkeletalMeshComponent* FindTargetComponent(AActor* Actor)
	{
		if (Actor == nullptr)
		{
			return nullptr;
		}
		USkeletalMeshComponent* Best = nullptr;
		int32 BestBones = 0;
		TInlineComponentArray<USkeletalMeshComponent*> Components(Actor);
		for (USkeletalMeshComponent* Component : Components)
		{
			USkeletalMesh* Mesh = Component ? Component->GetSkeletalMeshAsset() : nullptr;
			// Only NATIVE VRM meshes are excluded (they are sources): a DAZ/MetaHuman with
			// an AutoPopulate meta table is precisely the kind of target this fixes.
			if (Mesh == nullptr || UVrmRetargetSetupUtil::IsNativeVrmHumanoidMesh(Mesh))
			{
				continue;
			}
			// A body rig has legs. Face/groom rigs do not. Require a thigh-like bone so we
			// never pick the MetaHuman face mesh (which has more bones than the body).
			const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
			const bool bHasLegs =
				Ref.FindBoneIndex(TEXT("thigh_l")) != INDEX_NONE ||
				Ref.FindBoneIndex(TEXT("lThighBend")) != INDEX_NONE ||
				Ref.FindBoneIndex(TEXT("LeftUpLeg")) != INDEX_NONE ||
				Ref.FindBoneIndex(TEXT("mixamorig:LeftUpLeg")) != INDEX_NONE ||
				Ref.FindBoneIndex(TEXT("LeftUpperLeg")) != INDEX_NONE ||
				Ref.FindBoneIndex(TEXT("l_thigh")) != INDEX_NONE;
			if (!bHasLegs)
			{
				continue;
			}
			if (Ref.GetNum() > BestBones)
			{
				BestBones = Ref.GetNum();
				Best = Component;
			}
		}
		return Best;
	}

	// Both the wizard and the lint write their real content (per-step log,
	// per-finding fix hints) to LogVRM4UCaptureEditor. Give every toast a direct
	// link there instead of asking the user to find the Output Log themselves.
	void AddOpenOutputLogHyperlink(FNotificationInfo& Info)
	{
		Info.HyperlinkText = LOCTEXT("OpenOutputLog", "Open Output Log");
		Info.Hyperlink = FSimpleDelegate::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(FName(TEXT("OutputLog")));
		});
	}

	void RunSetupForActor(AActor* Actor)
	{
		USkeletalMeshComponent* Target = FindTargetComponent(Actor);
		if (Target == nullptr)
		{
			// The menu entry is hidden when no target resolves, but the selection can
			// change between menu build and click. Fail loudly, not silently.
			FNotificationInfo Info(FText::Format(
				LOCTEXT("SetupNoTargetFmt", "VMC retarget setup: no suitable skeletal mesh found on '{0}' (needs a non-VRM body rig with legs)."),
				FText::FromString(Actor->GetActorLabel())));
			Info.ExpireDuration = 6.0f;
			Info.bUseSuccessFailIcons = true;
			if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
			{
				Item->SetCompletionState(SNotificationItem::CS_Fail);
			}
			return;
		}

		FVrmRetargetSetupReport Report;
		{
			// The wizard creates IK rigs, the retargeter, and aligns poses
			// synchronously. Without this the editor just freezes until the toast.
			FScopedSlowTask SlowTask(1.0f, FText::Format(
				LOCTEXT("SetupSlowTaskFmt", "Setting up VMC retarget for '{0}' (IK rigs, retargeter, pose alignment)..."),
				FText::FromString(Actor->GetActorLabel())));
			SlowTask.MakeDialog();
			SlowTask.EnterProgressFrame(1.0f);
			Report = UVrmRetargetSetupUtil::SetupRetargetForComponent(Target);
		}

		// The full step log has already gone to LogVRM4UCaptureEditor. Notify with the tail.
		const FString Summary = Report.Messages.Num() > 0 ? Report.Messages.Last() : FString(TEXT("See the Output Log for details."));
		FNotificationInfo Info(FText::Format(
			Report.bSuccess
				? LOCTEXT("SetupOkFmt", "VMC retarget set up for '{0}'.\n{1}")
				: LOCTEXT("SetupFailFmt", "VMC retarget setup FAILED for '{0}'.\n{1}"),
			FText::FromString(Actor->GetActorLabel()), FText::FromString(Summary)));
		Info.ExpireDuration = Report.bSuccess ? 6.0f : 10.0f;
		Info.bUseSuccessFailIcons = true;
		AddOpenOutputLogHyperlink(Info);
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(Report.bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}

	void RunFaceSetupForActor(AActor* Actor)
	{
		const FVrmRetargetSetupReport Report = UVrmRetargetSetupUtil::SetupVMCFaceForActor(Actor, NAME_None);

		const FString Summary = Report.Messages.Num() > 0 ? Report.Messages.Last() : FString(TEXT("See the Output Log for details."));
		FNotificationInfo Info(FText::Format(
			Report.bSuccess
				? LOCTEXT("FaceSetupOkFmt", "VMC face LiveLink set up for '{0}'.\n{1}")
				: LOCTEXT("FaceSetupFailFmt", "VMC face setup FAILED for '{0}'.\n{1}"),
			FText::FromString(Actor->GetActorLabel()), FText::FromString(Summary)));
		Info.ExpireDuration = Report.bSuccess ? 6.0f : 10.0f;
		Info.bUseSuccessFailIcons = true;
		AddOpenOutputLogHyperlink(Info);
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(Report.bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}

	void RunSceneLint()
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (World == nullptr)
		{
			return;
		}
		const int32 NumFindings = UVrmSceneLint::LintWorldAndLog(World);
		FNotificationInfo Info(NumFindings == 0
			? LOCTEXT("LintClean", "VRM4U scene lint: no VMC retarget issues found.")
			: FText::Format(LOCTEXT("LintFindingsFmt", "VRM4U scene lint: {0} issue(s) found.\nDetails + fix hints in the Output Log."), NumFindings));
		Info.ExpireDuration = 8.0f;
		Info.bUseSuccessFailIcons = true;
		if (NumFindings > 0)
		{
			AddOpenOutputLogHyperlink(Info);
		}
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(NumFindings == 0 ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}

	void RegisterToolsMenu()
	{
		UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
		if (ToolsMenu == nullptr)
		{
			return;
		}
		FToolMenuSection& ToolsSection = ToolsMenu->FindOrAddSection("Tools");
		ToolsSection.AddMenuEntry(
			TEXT("VrmSceneLint"),
			LOCTEXT("LintEntryLabel", "VRM4U: Lint VMC Scene"),
			LOCTEXT("LintEntryTooltip",
				"Scan the level for common VMC retarget and face setup problems, then log how to fix each one."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateStatic(&RunSceneLint)));
	}

	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(MenuOwnerName);
		RegisterToolsMenu();
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.ActorContextMenu");
		if (Menu == nullptr)
		{
			return;
		}
		FToolMenuSection& Section = Menu->FindOrAddSection("VRM4U", LOCTEXT("VrmSectionLabel", "VRM4U"));
		Section.AddDynamicEntry(TEXT("VrmRetargetSetup"), FNewToolMenuSectionDelegate::CreateLambda(
			[](FToolMenuSection& InSection)
			{
				const ULevelEditorContextMenuContext* Context = InSection.FindContext<ULevelEditorContextMenuContext>();
				if (Context == nullptr || Context->CurrentSelection == nullptr)
				{
					return;
				}
				const TArray<AActor*> Actors = Context->CurrentSelection->GetSelectedObjects<AActor>();
				if (Actors.Num() != 1)
				{
					return;
				}
				AActor* Actor = Actors[0];
				if (FindTargetComponent(Actor) != nullptr)
				{
					InSection.AddMenuEntry(
						TEXT("VrmRetargetSetupEntry"),
						LOCTEXT("SetupEntryLabel", "VRM4U: Auto-Setup VMC Retarget"),
						LOCTEXT("SetupEntryTooltip",
							"Set this character up to follow the live VMC motion through an IK Retargeter. Requires a VRM/VRoid character in the level as the motion source."),
						FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([WeakActor = TWeakObjectPtr<AActor>(Actor)]()
						{
							if (AActor* A = WeakActor.Get()) { RunSetupForActor(A); }
						})));
				}
				if (UVrmVMCFaceLiveLinkComponent::FindFaceMeshOnActor(Actor) != nullptr)
				{
					InSection.AddMenuEntry(
						TEXT("VrmFaceSetupEntry"),
						LOCTEXT("FaceSetupEntryLabel", "VRM4U: Auto-Setup VMC Face (LiveLink)"),
						LOCTEXT("FaceSetupEntryTooltip",
							"Drive this character's face from the VMC blendshape stream through a LiveLink ARKit subject. Requires the engine's Live Link plugin."),
						FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([WeakActor = TWeakObjectPtr<AActor>(Actor)]()
						{
							if (AActor* A = WeakActor.Get()) { RunFaceSetupForActor(A); }
						})));
				}
			}));
	}
}

void FVrmRetargetSetupMenuExtension::Register()
{
	StartupCallbackHandle = UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateStatic(&RegisterMenus));
}

void FVrmRetargetSetupMenuExtension::Unregister()
{
	UToolMenus::UnRegisterStartupCallback(StartupCallbackHandle);
	if (UToolMenus* ToolMenus = UToolMenus::TryGet())
	{
		ToolMenus->UnregisterOwnerByName(MenuOwnerName);
	}
}

#undef LOCTEXT_NAMESPACE

#endif // UE_VERSION_OLDER_THAN(5, 6, 0)
