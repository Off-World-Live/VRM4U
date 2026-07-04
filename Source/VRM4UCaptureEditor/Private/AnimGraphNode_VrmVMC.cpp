// VRM4U Copyright (c) 2021-2026 Haruyoshi Yamamoto. This software is released under the MIT License.

#include "AnimGraphNode_VrmVMC.h"
#include "Misc/EngineVersionComparison.h"
#include "UnrealWidget.h"
#include "AnimNodeEditModes.h"
#include "AnimationRuntime.h"
#include "Animation/AnimInstance.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Components/SkeletalMeshComponent.h"
#include "VrmMetaObject.h"
#include "VrmUtil.h"

/////////////////////////////////////////////////////
// UAnimGraphNode_ModifyBone

#define LOCTEXT_NAMESPACE "A3Nodesaaaa"
////////////////////


UAnimGraphNode_VrmVMC::UAnimGraphNode_VrmVMC(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if	UE_VERSION_OLDER_THAN(5, 0, 0)
	CurWidgetMode = (int32)FWidget::WM_Rotate;
#else
	CurWidgetMode = UE::Widget::EWidgetMode::WM_Rotate;
#endif
}

void UAnimGraphNode_VrmVMC::ValidateAnimNodePostCompile(FCompilerResultsLog& MessageLog,
														UAnimBlueprintGeneratedClass* CompiledClass,
														int32 CompiledNodeIndex)
{
	if (Node.VrmMetaObject == nullptr)
	{
		//MessageLog.Warning(*LOCTEXT("VrmNoMetaObject", "@@ - You must set VrmMetaObject").ToString(), this);
	}
	else
	{
		// GetTargetSkeleton() can be null for a freshly created / duplicated /
		// reimporting AnimBP; dereferencing it unconditionally crashes the editor
		// at compile. Cache and guard.
		USkeleton* TargetSkeleton = CompiledClass ? CompiledClass->GetTargetSkeleton() : nullptr;
		if (Node.VrmMetaObject->SkeletalMesh && TargetSkeleton)
		{
			if (VRMGetSkeleton(Node.VrmMetaObject->SkeletalMesh) != TargetSkeleton)
			{
				MessageLog.Warning(
					*LOCTEXT("VrmDifferentSkeleton", "@@ - You must set VrmMetaObject has same skeleton").ToString(),
					this);
			}
		}
		if (TargetSkeleton && TargetSkeleton->GetReferenceSkeleton().GetRawBoneNum() <= 0)
		{
			MessageLog.Warning(*LOCTEXT("VrmNoBone", "@@ - Skeleton bad data").ToString(), this);
		}
	}

	Super::ValidateAnimNodePostCompile(MessageLog, CompiledClass, CompiledNodeIndex);
}

void UAnimGraphNode_VrmVMC::ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog)
{
	// Temporary fix where skeleton is not fully loaded during AnimBP compilation and thus virtual bone name check is invalid UE-39499 (NEED FIX) 
	if (ForSkeleton && !ForSkeleton->HasAnyFlags(RF_NeedPostLoad))
	{
		if (Node.VrmMetaObject == nullptr)
		{
			// Auto-search only resolves a humanoid table for VRM-imported meshes
			// (via the asset list). For MetaHuman / DAZ / Mixamo nothing is found,
			// so the node silently drives no bones. Nudge the user at compile time.
			MessageLog.Note(*LOCTEXT("VrmNoMetaObject",
				"@@ - No Vrm Meta Object set. Auto-search only resolves VRM-imported meshes; for "
				"MetaHuman / DAZ / Mixamo, run Auto-Populate on a Vrm Meta Object and assign it here.").ToString(), this);
		}
		else
		{
			if (Node.VrmMetaObject->SkeletalMesh)
			{
				if (VRMGetSkeleton(Node.VrmMetaObject->SkeletalMesh) != ForSkeleton)
				{
					//		MessageLog.Warning(*LOCTEXT("VrmDifferentSkeleton", "@@ - You must set VrmMetaObject has same skeleton").ToString(), this);
				}
			}
			if (ForSkeleton->GetReferenceSkeleton().GetRawBoneNum() <= 0)
			{
				//	MessageLog.Warning(*LOCTEXT("VrmNoBone", "@@ - Skeleton bad data").ToString(), this);
			}
		}


		/*
		//if (ForSkeleton->GetReferenceSkeleton().FindBoneIndex(Node.BoneToModify.BoneName) == INDEX_NONE)
		if (ForSkeleton->GetReferenceSkeleton().FindBoneIndex(Node.BoneNameToModify) == INDEX_NONE)
		{
			//if (Node.BoneToModify.BoneName == NAME_None)
			if (Node.BoneNameToModify == NAME_None)
			{
				MessageLog.Warning(*LOCTEXT("NoBoneSelectedToModify", "@@ - You must pick a bone to modify").ToString(), this);
			}
			else
			{
				FFormatNamedArguments Args;
				//Args.Add(TEXT("BoneName"), FText::FromName(Node.BoneToModify.BoneName));
				Args.Add(TEXT("BoneName"), FText::FromName(Node.BoneNameToModify));

				FText Msg = FText::Format(LOCTEXT("NoBoneFoundToModify", "@@ - Bone {BoneName} not found in Skeleton"), Args);

				MessageLog.Warning(*Msg.ToString(), this);
			}
		}
		*/
	}

	//if ((Node.TranslationMode == BMM_Ignore) && (Node.RotationMode == BMM_Ignore) && (Node.ScaleMode == BMM_Ignore))
	{
		//	MessageLog.Warning(*LOCTEXT("NothingToModify", "@@ - No components to modify selected.  Either Rotation, Translation, or Scale should be set to something other than Ignore").ToString(), this);
	}

	Super::ValidateAnimNodeDuringCompilation(ForSkeleton, MessageLog);
}

FText UAnimGraphNode_VrmVMC::GetControllerDescription() const
{
	return LOCTEXT("VrmVMC", "VrmVMC");
}

FText UAnimGraphNode_VrmVMC::GetTooltipText() const
{
	return LOCTEXT("AnimGraphNode_VrmVMC_Tooltip", "VrmVMC");
}

FText UAnimGraphNode_VrmVMC::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return GetControllerDescription();
	//if ((TitleType == ENodeTitleType::ListView || TitleType == ENodeTitleType::MenuTitle) && (Node.SpringBone.BoneName == NAME_None))
	//{
	//	return GetControllerDescription();
	//}

	/*
	FFormatNamedArguments Args;
	Args.Add(TEXT("ControllerDescription"), GetControllerDescription());
	Args.Add(TEXT("Server"), FText::FromString(Node.ServerAddress));
	Args.Add(TEXT("Port"), FText::FromString(FString::FromInt(Node.Port)));

	if (TitleType == ENodeTitleType::ListView || TitleType == ENodeTitleType::MenuTitle)
	{
		CachedNodeTitles.SetCachedTitle(TitleType, FText::Format(LOCTEXT("AnimGraphNode_VrmVMC_ListTitle", "{ControllerDescription} - {Server}:{Port}"), Args), this);
	}
	else
	{
		CachedNodeTitles.SetCachedTitle(TitleType, FText::Format(LOCTEXT("AnimGraphNode_VrmVMC_ListTitle", "{ControllerDescription}\n{Server}:{Port}"), Args), this);
	}

	return CachedNodeTitles[TitleType];
	*/
}

//void UAnimGraphNode_VrmVMC::CopyNodeDataToPreviewNode(FAnimNode_Base* InPreviewNode)
//{
//}

FEditorModeID UAnimGraphNode_VrmVMC::GetEditorMode() const
{
	return Super::GetEditorMode();
}

void UAnimGraphNode_VrmVMC::Draw(FPrimitiveDrawInterface* PDI, USkeletalMeshComponent* PreviewSkelMeshComp) const
{
	if (PreviewSkelMeshComp)
	{
		if (FAnimNode_VrmVMC* ActiveNode = GetActiveInstanceNode<FAnimNode_VrmVMC>(
			PreviewSkelMeshComp->GetAnimInstance()))
		{
			if (bPreviewLive)
			{
				//ActiveNode->ConditionalDebugDraw(PDI, PreviewSkelMeshComp, bPreviewForeground);
			}
		}
	}
}

void UAnimGraphNode_VrmVMC::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FAnimNode_VrmVMC, VrmMetaObject))
	{
		if (Node.VrmMetaObject && Node.VrmMetaObject->SkeletalMesh)
		{
			// Try to automatically populate metadata if needed. Validation, type
			// resolution, the undo transaction, and notifications are shared across
			// all Auto-Populate entry points. No assign reminder here: the meta is
			// being assigned to the node right now.
			if (Node.VrmMetaObject->humanoidBoneTable.Num() == 0)
			{
				// We need a non-const pointer to modify the meta object
				UVrmMetaObject* MutableMetaObject = const_cast<UVrmMetaObject*>(Node.VrmMetaObject);
				UAutoPopulateVrmMeta::AutoPopulateWithUi(MutableMetaObject, /*bShowAssignReminder=*/false);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
