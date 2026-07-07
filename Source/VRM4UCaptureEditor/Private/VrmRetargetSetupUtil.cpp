// VRM4U Copyright (c) 2021-2026 Haruyoshi Yamamoto. This software is released under the MIT License.

#include "VrmRetargetSetupUtil.h"
#include "VRM4UCaptureEditorLog.h"

#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/SkeletalMeshComponent.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "VrmMetaObject.h"
#include "VrmVMCFaceLiveLinkComponent.h"

// Face setup (no IK Rig API involved, available on every editor build)
#include "Animation/AnimBlueprint.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ScopedTransaction.h"

#if VRM4U_RETARGET_SETUP
#include "VrmVMCRetargetAnimInstance.h"
#include "VrmRigHeader.h"
#include "Retargeter/IKRetargetChainMapping.h"
#include "RetargetEditor/IKRetargeterPoseGenerator.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#endif

#define LOCTEXT_NAMESPACE "VrmRetargetSetup"

namespace
{
	void Log(TArray<FString>& Messages, const FString& Msg)
	{
		Messages.Add(Msg);
		UE_LOG(LogVRM4UCaptureEditor, Display, TEXT("[RetargetSetup] %s"), *Msg);
	}

	// VRM humanoid name to VRoid model bone name. Only used when a mesh has neither a
	// VRM4U meta table nor humanoid-renamed bones (e.g. a VRoid imported outside VRM4U).
	const TMap<FString, FString>& GetVRoidBoneTable()
	{
		static const TMap<FString, FString> Table = []()
		{
			TMap<FString, FString> M;
			M.Add(TEXT("hips"), TEXT("J_Bip_C_Hips"));
			M.Add(TEXT("spine"), TEXT("J_Bip_C_Spine"));
			M.Add(TEXT("chest"), TEXT("J_Bip_C_Chest"));
			M.Add(TEXT("upperChest"), TEXT("J_Bip_C_UpperChest"));
			M.Add(TEXT("neck"), TEXT("J_Bip_C_Neck"));
			M.Add(TEXT("head"), TEXT("J_Bip_C_Head"));

			const TCHAR* SidePrefix[2] = { TEXT("left"), TEXT("right") };
			const TCHAR* JBipPrefix[2] = { TEXT("J_Bip_L_"), TEXT("J_Bip_R_") };
			const TCHAR* Limbs[][2] = {
				{ TEXT("Shoulder"), TEXT("Shoulder") },
				{ TEXT("UpperArm"), TEXT("UpperArm") },
				{ TEXT("LowerArm"), TEXT("LowerArm") },
				{ TEXT("Hand"), TEXT("Hand") },
				{ TEXT("UpperLeg"), TEXT("UpperLeg") },
				{ TEXT("LowerLeg"), TEXT("LowerLeg") },
				{ TEXT("Foot"), TEXT("Foot") },
				{ TEXT("Toes"), TEXT("ToeBase") },
			};
			const TCHAR* Fingers[5] = { TEXT("Thumb"), TEXT("Index"), TEXT("Middle"), TEXT("Ring"), TEXT("Little") };
			const TCHAR* Segments[3] = { TEXT("Proximal"), TEXT("Intermediate"), TEXT("Distal") };

			for (int32 s = 0; s < 2; ++s)
			{
				for (const auto& Limb : Limbs)
				{
					M.Add(FString(SidePrefix[s]) + Limb[0], FString(JBipPrefix[s]) + Limb[1]);
				}
				for (const TCHAR* Finger : Fingers)
				{
					for (int32 Seg = 0; Seg < 3; ++Seg)
					{
						M.Add(FString(SidePrefix[s]) + Finger + Segments[Seg],
							FString(JBipPrefix[s]) + Finger + FString::FromInt(Seg + 1));
					}
				}
			}
			return M;
		}();
		return Table;
	}

	// Resolves VRM humanoid bone names ("leftUpperArm") to actual bones on a mesh:
	// meta humanoidBoneTable first, then bones literally named after the humanoid slot
	// (VRM4U humanoid-renamed imports), then VRoid J_Bip naming.
	struct FHumanoidResolver
	{
		const FReferenceSkeleton* RefSkeleton = nullptr;
		const TMap<FString, FString>* MetaTable = nullptr;

		FName Resolve(const TCHAR* HumanoidName) const
		{
			const FString Humanoid(HumanoidName);
			if (MetaTable != nullptr)
			{
				if (const FString* Mapped = MetaTable->Find(Humanoid))
				{
					const FName BoneName(**Mapped);
					if (!Mapped->IsEmpty() && RefSkeleton->FindBoneIndex(BoneName) != INDEX_NONE)
					{
						return BoneName;
					}
				}
			}
			{
				const FName Direct(*Humanoid);
				if (RefSkeleton->FindBoneIndex(Direct) != INDEX_NONE)
				{
					return Direct;
				}
			}
			if (const FString* VRoid = GetVRoidBoneTable().Find(Humanoid))
			{
				const FName BoneName(**VRoid);
				if (RefSkeleton->FindBoneIndex(BoneName) != INDEX_NONE)
				{
					return BoneName;
				}
			}
			return NAME_None;
		}
	};

	const TMap<FString, FString>* FindVrmMetaTableForMesh(USkeletalMesh* Mesh)
	{
		for (TObjectIterator<UVrmMetaObject> It; It; ++It)
		{
			if (It->SkeletalMesh == Mesh && It->humanoidBoneTable.Num() > 0)
			{
				return &It->humanoidBoneTable;
			}
		}
		return nullptr;
	}

	FHumanoidResolver MakeResolver(USkeletalMesh* Mesh)
	{
		FHumanoidResolver R;
		R.RefSkeleton = &Mesh->GetRefSkeleton();
		R.MetaTable = FindVrmMetaTableForMesh(Mesh);
		return R;
	}

	bool ResolverCoversCore(const FHumanoidResolver& R)
	{
		return R.Resolve(TEXT("hips")) != NAME_None
			&& R.Resolve(TEXT("leftUpperArm")) != NAME_None
			&& R.Resolve(TEXT("leftHand")) != NAME_None
			&& R.Resolve(TEXT("leftUpperLeg")) != NAME_None;
	}

	// VRM by any route, including an AutoPopulate meta table on a foreign rig (DAZ etc.).
	bool ResolvesAsVrmHumanoid(USkeletalMesh* Mesh)
	{
		if (Mesh == nullptr)
		{
			return false;
		}
		return ResolverCoversCore(MakeResolver(Mesh));
	}

	// VRM by its own bone names only (humanoid-renamed or J_Bip). A DAZ/MetaHuman with an
	// AutoPopulate meta table is NOT native: the direct VMC node path is broken for it.
	bool ResolvesAsNativeVrmHumanoid(USkeletalMesh* Mesh)
	{
		if (Mesh == nullptr)
		{
			return false;
		}
		FHumanoidResolver R;
		R.RefSkeleton = &Mesh->GetRefSkeleton();
		R.MetaTable = nullptr;
		return ResolverCoversCore(R);
	}
}

#if VRM4U_RETARGET_SETUP

namespace
{
	// Chain names every rig the wizard touches must carry so exact auto-mapping works.
	// These mirror the engine's FCharacterizationStandard names (what ApplyAutoGenerated-
	// RetargetDefinition produces), NOT the hand-built LeftShoulder/RightShoulder names.
	// Local copies because FCharacterizationStandard's statics are not dll-exported.
	namespace StandardChains
	{
		const FName Root("Root");
		const FName Spine("Spine");
		const FName Neck("Neck");
		const FName Head("Head");
		const FName LeftLeg("LeftLeg");
		const FName RightLeg("RightLeg");
		const FName LeftClavicle("LeftClavicle");
		const FName RightClavicle("RightClavicle");
		const FName LeftArm("LeftArm");
		const FName RightArm("RightArm");
		const FName LeftThumb("LeftThumb");
		const FName LeftIndex("LeftIndex");
		const FName LeftMiddle("LeftMiddle");
		const FName LeftRing("LeftRing");
		const FName LeftPinky("LeftPinky");
		const FName RightThumb("RightThumb");
		const FName RightIndex("RightIndex");
		const FName RightMiddle("RightMiddle");
		const FName RightRing("RightRing");
		const FName RightPinky("RightPinky");
	}

	const TArray<FName>& GetRequiredChainNames()
	{
		static const TArray<FName> Required = {
			StandardChains::Spine,
			StandardChains::Head,
			StandardChains::LeftArm,
			StandardChains::RightArm,
			StandardChains::LeftLeg,
			StandardChains::RightLeg,
			StandardChains::LeftClavicle,
			StandardChains::RightClavicle,
		};
		return Required;
	}

	const TArray<FName>& GetFingerChainNames()
	{
		static const TArray<FName> Fingers = {
			StandardChains::LeftThumb, StandardChains::LeftIndex,
			StandardChains::LeftMiddle, StandardChains::LeftRing,
			StandardChains::LeftPinky,
			StandardChains::RightThumb, StandardChains::RightIndex,
			StandardChains::RightMiddle, StandardChains::RightRing,
			StandardChains::RightPinky,
		};
		return Fingers;
	}

	// Case-insensitive "rig has a chain with this name".
	bool RigHasChain(const UIKRigController& Controller, const FName ChainName)
	{
		for (const FBoneChain& Chain : Controller.GetRetargetChains())
		{
			if (Chain.ChainName.ToString().Equals(ChainName.ToString(), ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	// A rig is reusable only when its pelvis is set and its chain names are a superset of
	// the standard core set. A rig with LeftShoulder-style names fails this on purpose:
	// exact mapping against a standard-named rig would silently drop those chains.
	bool ValidateRigChains(UIKRigDefinition* Rig, TArray<FString>& Missing)
	{
		const UIKRigController* Controller = UIKRigController::GetController(Rig);
		if (Controller == nullptr)
		{
			return false;
		}
		Missing.Reset();
		if (Controller->GetRetargetRoot() == NAME_None)
		{
			Missing.Add(TEXT("pelvis (retarget root)"));
		}
		for (const FName& Required : GetRequiredChainNames())
		{
			if (!RigHasChain(*Controller, Required))
			{
				Missing.Add(Required.ToString());
			}
		}
		return Missing.Num() == 0;
	}

	int32 CountFingerChains(UIKRigDefinition* Rig)
	{
		const UIKRigController* Controller = UIKRigController::GetController(Rig);
		if (Controller == nullptr)
		{
			return 0;
		}
		int32 Count = 0;
		for (const FName& Finger : GetFingerChainNames())
		{
			Count += RigHasChain(*Controller, Finger) ? 1 : 0;
		}
		return Count;
	}

	FString ChooseAssetFolder(USkeletalMesh* TargetMesh, const FString& RequestedFolder)
	{
		if (!RequestedFolder.IsEmpty())
		{
			return RequestedFolder;
		}
		const FString MeshFolder = FPackageName::GetLongPackagePath(TargetMesh->GetPackage()->GetName());
		if (MeshFolder.StartsWith(TEXT("/Game")))
		{
			return MeshFolder;
		}
		return TEXT("/Game/VRM4U/Retarget");
	}

	template <typename T>
	T* CreateAssetInFolder(const FString& Folder, const FString& BaseName, TArray<FString>& Messages)
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		FString PackageName;
		FString AssetName;
		AssetTools.CreateUniqueAssetName(Folder / BaseName, TEXT(""), PackageName, AssetName);

		UPackage* Package = CreatePackage(*PackageName);
		if (Package == nullptr)
		{
			Log(Messages, FString::Printf(TEXT("FAILED to create package '%s'."), *PackageName));
			return nullptr;
		}
		T* Asset = NewObject<T>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Asset);
		Asset->MarkPackageDirty();
		return Asset;
	}

	// Builds the standard-named retarget definition for a VRM humanoid source rig. Mirrors
	// the engine's template style (legs run to the toes when present) so chain-to-chain
	// auto-align sees comparable chains on both sides.
	bool BuildVrmRetargetChains(UIKRigDefinition* Rig, USkeletalMesh* Mesh, TArray<FString>& Messages)
	{
		const UIKRigController* Controller = UIKRigController::GetController(Rig);
		const FHumanoidResolver R = MakeResolver(Mesh);

		const FName Hips = R.Resolve(TEXT("hips"));
		if (Hips == NAME_None || !Controller->SetRetargetRoot(Hips))
		{
			Log(Messages, TEXT("FAILED: could not resolve/set the VRM hips bone as retarget root."));
			return false;
		}

		struct FChainDef { FName Chain; FName Start; FName End; bool bRequired; };
		TArray<FChainDef> Chains;

		// Root chain on the skeleton root when it isn't the hips (matches engine templates).
		const FName RootBone = Mesh->GetRefSkeleton().GetBoneName(0);
		if (RootBone != Hips)
		{
			Chains.Add({ StandardChains::Root, RootBone, RootBone, false });
		}

		const FName Spine = R.Resolve(TEXT("spine"));
		FName SpineEnd = R.Resolve(TEXT("upperChest"));
		if (SpineEnd == NAME_None) { SpineEnd = R.Resolve(TEXT("chest")); }
		if (SpineEnd == NAME_None) { SpineEnd = Spine; }
		Chains.Add({ StandardChains::Spine, Spine, SpineEnd, true });

		const FName Neck = R.Resolve(TEXT("neck"));
		Chains.Add({ StandardChains::Neck, Neck, Neck, false });
		const FName Head = R.Resolve(TEXT("head"));
		Chains.Add({ StandardChains::Head, Head, Head, true });

		const TCHAR* Sides[2] = { TEXT("left"), TEXT("right") };
		const FName ClavicleChains[2] = { StandardChains::LeftClavicle, StandardChains::RightClavicle };
		const FName ArmChains[2] = { StandardChains::LeftArm, StandardChains::RightArm };
		const FName LegChains[2] = { StandardChains::LeftLeg, StandardChains::RightLeg };
		const FName FingerChains[2][5] = {
			{ StandardChains::LeftThumb, StandardChains::LeftIndex, StandardChains::LeftMiddle, StandardChains::LeftRing, StandardChains::LeftPinky },
			{ StandardChains::RightThumb, StandardChains::RightIndex, StandardChains::RightMiddle, StandardChains::RightRing, StandardChains::RightPinky },
		};
		const TCHAR* FingerNames[5] = { TEXT("Thumb"), TEXT("Index"), TEXT("Middle"), TEXT("Ring"), TEXT("Little") };

		for (int32 s = 0; s < 2; ++s)
		{
			const FString Side(Sides[s]);
			const FName Shoulder = R.Resolve(*(Side + TEXT("Shoulder")));
			Chains.Add({ ClavicleChains[s], Shoulder, Shoulder, false });
			if (Shoulder == NAME_None)
			{
				// XR Animator routes roughly half of a full arm raise through the VRM
				// shoulder bones. Without this chain arms stop ~50 degrees short.
				Log(Messages, FString::Printf(TEXT("WARNING: no %sShoulder bone found. Arm raises will fall visibly short of the source."), *Side));
			}

			Chains.Add({ ArmChains[s], R.Resolve(*(Side + TEXT("UpperArm"))), R.Resolve(*(Side + TEXT("Hand"))), true });

			FName LegEnd = R.Resolve(*(Side + TEXT("Toes")));
			if (LegEnd == NAME_None) { LegEnd = R.Resolve(*(Side + TEXT("Foot"))); }
			Chains.Add({ LegChains[s], R.Resolve(*(Side + TEXT("UpperLeg"))), LegEnd, true });

			for (int32 f = 0; f < 5; ++f)
			{
				const FName Proximal = R.Resolve(*(Side + FingerNames[f] + TEXT("Proximal")));
				const FName Distal = R.Resolve(*(Side + FingerNames[f] + TEXT("Distal")));
				if (Proximal != NAME_None && Distal != NAME_None)
				{
					Chains.Add({ FingerChains[s][f], Proximal, Distal, false });
				}
				else
				{
					Log(Messages, FString::Printf(TEXT("WARNING: %s%s finger bones not found; that finger will not be retargeted."), *Side, FingerNames[f]));
				}
			}
		}

		for (const FChainDef& Def : Chains)
		{
			if (Def.Start == NAME_None || Def.End == NAME_None)
			{
				if (Def.bRequired)
				{
					Log(Messages, FString::Printf(TEXT("FAILED: required chain '%s' could not be resolved on '%s'."), *Def.Chain.ToString(), *Mesh->GetName()));
					return false;
				}
				continue;
			}
			Controller->AddRetargetChain(Def.Chain, Def.Start, Def.End, NAME_None);
		}
		return true;
	}

	UIKRigDefinition* FindReusableIKRig(USkeletalMesh* Mesh, TArray<FString>& Messages)
	{
		const FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FAssetData> Assets;
		AssetRegistry.Get().GetAssetsByClass(UIKRigDefinition::StaticClass()->GetClassPathName(), Assets, true);

		for (const FAssetData& AssetData : Assets)
		{
			UIKRigDefinition* Rig = Cast<UIKRigDefinition>(AssetData.GetAsset());
			if (Rig == nullptr || Rig->GetPreviewMesh() != Mesh)
			{
				continue;
			}
			TArray<FString> Missing;
			if (ValidateRigChains(Rig, Missing))
			{
				Log(Messages, FString::Printf(TEXT("Reusing existing IK rig '%s' for '%s'."), *Rig->GetName(), *Mesh->GetName()));
				const int32 Fingers = CountFingerChains(Rig);
				if (Fingers < 10)
				{
					Log(Messages, FString::Printf(TEXT("WARNING: '%s' has %d/10 finger chains; hand tracking will be partial."), *Rig->GetName(), Fingers));
				}
				return Rig;
			}
			Log(Messages, FString::Printf(TEXT("Existing IK rig '%s' targets this mesh but lacks standard chains (%s) - creating a fresh rig instead."),
				*Rig->GetName(), *FString::Join(Missing, TEXT(", "))));
		}
		return nullptr;
	}

	// bUseVrmChainBuilder: VRM chain builder (meta table if present) vs engine auto-characterizer.
	// Caller-picked because a foreign rig (e.g. DAZ) can carry a meta table yet still need the
	// auto-characterizer for its real spine layout.
	UIKRigDefinition* FindOrCreateIKRig(USkeletalMesh* Mesh, const FString& Folder, bool bUseVrmChainBuilder, TArray<FString>& Messages)
	{
		if (UIKRigDefinition* Existing = FindReusableIKRig(Mesh, Messages))
		{
			return Existing;
		}

		UIKRigDefinition* Rig = CreateAssetInFolder<UIKRigDefinition>(Folder, FString::Printf(TEXT("IK_%s_VRM4U"), *Mesh->GetName()), Messages);
		if (Rig == nullptr)
		{
			return nullptr;
		}
		const UIKRigController* Controller = UIKRigController::GetController(Rig);
		if (!Controller->SetSkeletalMesh(Mesh))
		{
			Log(Messages, FString::Printf(TEXT("FAILED: could not initialize IK rig with mesh '%s'."), *Mesh->GetName()));
			return nullptr;
		}

		bool bBuilt = false;
		if (bUseVrmChainBuilder)
		{
			Log(Messages, FString::Printf(TEXT("Building the standard VRM chain set for '%s'."), *Mesh->GetName()));
			bBuilt = BuildVrmRetargetChains(Rig, Mesh, Messages);
		}
		else
		{
			// The engine knows MetaHuman/UE5, DAZ, Mixamo, CC4 and a dozen more templates,
			// with clavicle + finger chains included.
			bBuilt = UIKRigController::GetController(Rig)->ApplyAutoGeneratedRetargetDefinition();
			Log(Messages, bBuilt
				? FString::Printf(TEXT("Engine auto-characterizer recognized '%s' and generated its chains."), *Mesh->GetName())
				: FString::Printf(TEXT("FAILED: engine auto-characterizer does not recognize '%s'. Build the IK rig manually (see the VRM4U retarget setup guide) and re-run."), *Mesh->GetName()));
		}

		TArray<FString> Missing;
		if (!bBuilt || !ValidateRigChains(Rig, Missing))
		{
			if (bBuilt)
			{
				Log(Messages, FString::Printf(TEXT("FAILED: generated rig for '%s' is missing: %s."), *Mesh->GetName(), *FString::Join(Missing, TEXT(", "))));
			}
			return nullptr;
		}
		Log(Messages, FString::Printf(TEXT("Created IK rig '%s' (%d finger chains)."), *Rig->GetName(), CountFingerChains(Rig)));
		return Rig;
	}

	// A retargeter whose target pose was never aligned ships identity offsets on every bone,
	// the exact "arms ride ~45 degrees low" failure. Create + activate an auto-aligned pose.
	// "Default Pose" stays untouched for rollback.
	bool EnsureAutoAlignedTargetPose(const UIKRetargeterController& Controller, TArray<FString>& Messages)
	{
		const FIKRetargetPose& Current = Controller.GetCurrentRetargetPose(ERetargetSourceOrTarget::Target);
		bool bHasOffsets = !Current.GetRootTranslationDelta().IsNearlyZero();
		for (const TPair<FName, FQuat>& Pair : Current.GetAllDeltaRotations())
		{
			if (!Pair.Value.IsIdentity(KINDA_SMALL_NUMBER))
			{
				bHasOffsets = true;
				break;
			}
		}
		if (bHasOffsets)
		{
			Log(Messages, FString::Printf(TEXT("Target retarget pose '%s' already has offsets - leaving it alone."),
				*Controller.GetCurrentRetargetPoseName(ERetargetSourceOrTarget::Target).ToString()));
			return true;
		}

		const FName PoseName = Controller.CreateRetargetPose(FName(TEXT("VRM4U_AutoAligned")), ERetargetSourceOrTarget::Target);
		if (PoseName == NAME_None)
		{
			Log(Messages, TEXT("FAILED: could not create the auto-aligned retarget pose."));
			return false;
		}
		Controller.SetCurrentRetargetPose(PoseName, ERetargetSourceOrTarget::Target);
		Controller.AutoAlignAllBones(ERetargetSourceOrTarget::Target, ERetargetAutoAlignMethod::ChainToChain);
		Controller.GetAsset()->MarkPackageDirty();
		Log(Messages, FString::Printf(TEXT("Created + activated auto-aligned target pose '%s' (Default Pose kept for rollback)."), *PoseName.ToString()));
		return true;
	}

	UIKRetargeter* FindExistingRetargeter(USkeletalMesh* SourceMesh, USkeletalMesh* TargetMesh, TArray<FString>& Messages)
	{
		const FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FAssetData> Assets;
		AssetRegistry.Get().GetAssetsByClass(UIKRetargeter::StaticClass()->GetClassPathName(), Assets, true);

		for (const FAssetData& AssetData : Assets)
		{
			UIKRetargeter* Retargeter = Cast<UIKRetargeter>(AssetData.GetAsset());
			const UIKRigDefinition* SrcRig = Retargeter ? Retargeter->GetIKRig(ERetargetSourceOrTarget::Source) : nullptr;
			const UIKRigDefinition* TgtRig = Retargeter ? Retargeter->GetIKRig(ERetargetSourceOrTarget::Target) : nullptr;
			if (SrcRig == nullptr || TgtRig == nullptr)
			{
				continue;
			}
			if (SrcRig->GetPreviewMesh() == SourceMesh && TgtRig->GetPreviewMesh() == TargetMesh)
			{
				Log(Messages, FString::Printf(TEXT("Found existing retargeter '%s' for this source/target pair."), *Retargeter->GetName()));
				return Retargeter;
			}
		}
		return nullptr;
	}

	UIKRetargeter* CreateRetargeter(UIKRigDefinition* SourceRig, UIKRigDefinition* TargetRig,
		USkeletalMesh* SourceMesh, USkeletalMesh* TargetMesh, const FString& Folder, TArray<FString>& Messages)
	{
		UIKRetargeter* Retargeter = CreateAssetInFolder<UIKRetargeter>(Folder,
			FString::Printf(TEXT("RTG_%s_to_%s_VRM4U"), *SourceMesh->GetName(), *TargetMesh->GetName()), Messages);
		if (Retargeter == nullptr)
		{
			return nullptr;
		}
		const UIKRetargeterController* Controller = UIKRetargeterController::GetController(Retargeter);

		// Rigs FIRST, ops second: each op binds the asset's rigs in OnAddedToStack, so this
		// order makes the editor's stale-per-op-rig trap structurally impossible.
		Controller->SetIKRig(ERetargetSourceOrTarget::Source, SourceRig);
		Controller->SetIKRig(ERetargetSourceOrTarget::Target, TargetRig);
		Controller->SetPreviewMesh(ERetargetSourceOrTarget::Source, SourceMesh);
		Controller->SetPreviewMesh(ERetargetSourceOrTarget::Target, TargetMesh);
		Controller->AddDefaultOps();
		Controller->AutoMapChains(EAutoMapChainType::Exact, /*bForceRemap =*/ true);

		// Count mapped pairs on the best-mapped op: ops like Pelvis Motion legitimately
		// keep an empty mapping, so "first mapping found" would read as zero.
		int32 MappedChains = 0;
		const int32 NumOps = Controller->GetNumRetargetOps();
		for (int32 OpIndex = 0; OpIndex < NumOps; ++OpIndex)
		{
			const FRetargetChainMapping* Mapping = Controller->GetChainMapping(Controller->GetOpName(OpIndex));
			if (Mapping == nullptr)
			{
				continue;
			}
			int32 Count = 0;
			for (const FRetargetChainPair& Pair : Mapping->GetChainPairs())
			{
				Count += (Pair.SourceChainName != NAME_None) ? 1 : 0;
			}
			MappedChains = FMath::Max(MappedChains, Count);
		}
		Log(Messages, FString::Printf(TEXT("Created retargeter '%s' with default ops; %d chains exact-mapped."), *Retargeter->GetName(), MappedChains));
		if (MappedChains < GetRequiredChainNames().Num())
		{
			Log(Messages, FString::Printf(TEXT("FAILED: only %d chains mapped (expected at least %d) - chain names diverge between the rigs."), MappedChains, GetRequiredChainNames().Num()));
			return nullptr;
		}

		if (!EnsureAutoAlignedTargetPose(*Controller, Messages))
		{
			return nullptr;
		}
		return Retargeter;
	}
}

FVrmRetargetSetupReport UVrmRetargetSetupUtil::SetupRetarget(USkeletalMesh* SourceVrmMesh, USkeletalMesh* TargetMesh, const FString& AssetFolder)
{
	FVrmRetargetSetupReport Report;
	if (SourceVrmMesh == nullptr || TargetMesh == nullptr || SourceVrmMesh == TargetMesh)
	{
		Log(Report.Messages, TEXT("FAILED: need two distinct meshes (source VRM + target)."));
		return Report;
	}
	if (!ResolvesAsVrmHumanoid(SourceVrmMesh))
	{
		Log(Report.Messages, FString::Printf(TEXT("FAILED: '%s' does not resolve as a VRM humanoid; it cannot be the VMC source."), *SourceVrmMesh->GetName()));
		return Report;
	}
	if (ResolvesAsNativeVrmHumanoid(TargetMesh))
	{
		Log(Report.Messages, FString::Printf(TEXT("NOTE: target '%s' is itself a native VRM humanoid. VRM rigs are driven directly by the VMC node - no retargeter needed. Continuing anyway (VRM-to-VRM retarget)."), *TargetMesh->GetName()));
	}
	else if (ResolvesAsVrmHumanoid(TargetMesh))
	{
		Log(Report.Messages, FString::Printf(TEXT("NOTE: target '%s' has an AutoPopulate VRM meta table (direct VMC path - known-broken for non-VRM bind poses). This wizard builds an IK Retargeter instead; its target chains come from the engine auto-characterizer."), *TargetMesh->GetName()));
	}

	const FString Folder = ChooseAssetFolder(TargetMesh, AssetFolder);
	Log(Report.Messages, FString::Printf(TEXT("Setup: '%s' -> '%s' (assets in %s)."), *SourceVrmMesh->GetName(), *TargetMesh->GetName(), *Folder));

	// Reuse first: an existing retargeter for this pair (e.g. the hand-built, validated
	// RTG_OWLVRM_to_MetaHuman) short-circuits everything, no fresh rigs get created.
	if (UIKRetargeter* Existing = FindExistingRetargeter(SourceVrmMesh, TargetMesh, Report.Messages))
	{
		Report.Retargeter = Existing;
		Report.SourceIKRig = const_cast<UIKRigDefinition*>(Existing->GetIKRig(ERetargetSourceOrTarget::Source));
		Report.TargetIKRig = const_cast<UIKRigDefinition*>(Existing->GetIKRig(ERetargetSourceOrTarget::Target));
		// Heal the known zero-offset-pose failure mode on reused assets too (only that
		// path dirties the asset, an untouched reuse stays clean).
		if (!EnsureAutoAlignedTargetPose(*UIKRetargeterController::GetController(Existing), Report.Messages))
		{
			return Report;
		}
		Report.bSuccess = true;
		Log(Report.Messages, TEXT("Existing retargeter reused. Ensure the target's Anim Class is VrmVMCRetargetAnimInstance and press Play."));
		return Report;
	}

	// Source is the validated VRM. The VRM builder handles it (native or meta-table bones).
	Report.SourceIKRig = FindOrCreateIKRig(SourceVrmMesh, Folder, /*bUseVrmChainBuilder=*/true, Report.Messages);
	if (Report.SourceIKRig == nullptr)
	{
		return Report;
	}
	// Native VRM target uses the VRM builder. Any foreign rig (even meta-tabled) uses the auto-characterizer.
	Report.TargetIKRig = FindOrCreateIKRig(TargetMesh, Folder, /*bUseVrmChainBuilder=*/ResolvesAsNativeVrmHumanoid(TargetMesh), Report.Messages);
	if (Report.TargetIKRig == nullptr)
	{
		return Report;
	}

	Report.Retargeter = CreateRetargeter(Report.SourceIKRig, Report.TargetIKRig, SourceVrmMesh, TargetMesh, Folder, Report.Messages);
	if (Report.Retargeter == nullptr)
	{
		return Report;
	}

	Report.bSuccess = true;
	Log(Report.Messages, TEXT("Retarget setup complete. Save the new assets, set the target's Anim Class to VrmVMCRetargetAnimInstance (or use the context menu action), and press Play."));
	return Report;
}

bool UVrmRetargetSetupUtil::ApplyRetargetAnimClassToComponent(USkeletalMeshComponent* TargetComponent)
{
	if (TargetComponent == nullptr || TargetComponent->GetSkeletalMeshAsset() == nullptr)
	{
		return false;
	}
	FScopedTransaction Transaction(LOCTEXT("ApplyRetargetAnimClass", "VRM4U: Set VMC Retarget Anim Class"));
	TargetComponent->Modify();
	TargetComponent->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	TargetComponent->SetAnimInstanceClass(UVrmVMCRetargetAnimInstance::StaticClass());
	return true;
}

USkeletalMeshComponent* UVrmRetargetSetupUtil::FindLikelySourceComponent(USkeletalMeshComponent* ExcludeComponent)
{
	UWorld* World = ExcludeComponent ? ExcludeComponent->GetWorld() : nullptr;
	if (World == nullptr)
	{
		return nullptr;
	}
	USkeletalMeshComponent* Best = nullptr;
	int32 BestScore = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		TInlineComponentArray<USkeletalMeshComponent*> Components(*It);
		for (USkeletalMeshComponent* Component : Components)
		{
			USkeletalMesh* Mesh = Component ? Component->GetSkeletalMeshAsset() : nullptr;
			if (Mesh == nullptr || Component == ExcludeComponent || Component->GetOwner() == ExcludeComponent->GetOwner())
			{
				continue;
			}
			if (!ResolvesAsVrmHumanoid(Mesh))
			{
				continue;
			}
			// Native VRM bone naming beats a mere AutoPopulate meta table: a DAZ with a
			// meta table resolves as VRM but is a retarget TARGET, not the live source.
			const int32 Score = (ResolvesAsNativeVrmHumanoid(Mesh) ? 4 : 0)
				+ ((FindVrmMetaTableForMesh(Mesh) != nullptr) ? 2 : 1);
			if (Score > BestScore)
			{
				BestScore = Score;
				Best = Component;
			}
		}
	}
	return Best;
}

FVrmRetargetSetupReport UVrmRetargetSetupUtil::SetupRetargetForComponent(USkeletalMeshComponent* TargetComponent)
{
	FVrmRetargetSetupReport Report;
	if (TargetComponent == nullptr || TargetComponent->GetSkeletalMeshAsset() == nullptr)
	{
		Log(Report.Messages, TEXT("FAILED: component has no skeletal mesh."));
		return Report;
	}
	USkeletalMeshComponent* Source = FindLikelySourceComponent(TargetComponent);
	if (Source == nullptr)
	{
		Log(Report.Messages, TEXT("FAILED: no VRM source rig found in this level. Add your VRM/VRoid character first (it is what VMC drives)."));
		return Report;
	}
	Log(Report.Messages, FString::Printf(TEXT("Using '%s' (%s) as the VMC source."), *Source->GetOwner()->GetActorLabel(), *Source->GetSkeletalMeshAsset()->GetName()));

	Report = SetupRetarget(Source->GetSkeletalMeshAsset(), TargetComponent->GetSkeletalMeshAsset(), FString());
	// Preserve the source note at the front of the fresh report.
	Report.Messages.Insert(FString::Printf(TEXT("Using '%s' (%s) as the VMC source."), *Source->GetOwner()->GetActorLabel(), *Source->GetSkeletalMeshAsset()->GetName()), 0);
	if (!Report.bSuccess)
	{
		return Report;
	}
	if (ApplyRetargetAnimClassToComponent(TargetComponent))
	{
		Log(Report.Messages, FString::Printf(TEXT("Anim Class on '%s' set to VrmVMCRetargetAnimInstance; auto-resolve wires source + retargeter at play time."), *TargetComponent->GetOwner()->GetActorLabel()));
	}
	return Report;
}

bool UVrmRetargetSetupUtil::IsVrmHumanoidMesh(USkeletalMesh* Mesh)
{
	return ResolvesAsVrmHumanoid(Mesh);
}

bool UVrmRetargetSetupUtil::IsNativeVrmHumanoidMesh(USkeletalMesh* Mesh)
{
	return ResolvesAsNativeVrmHumanoid(Mesh);
}

UIKRetargeter* UVrmRetargetSetupUtil::FindRetargeterTargetingMesh(USkeletalMesh* Mesh)
{
	if (Mesh == nullptr)
	{
		return nullptr;
	}
	const FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> Assets;
	AssetRegistry.Get().GetAssetsByClass(UIKRetargeter::StaticClass()->GetClassPathName(), Assets, true);
	for (const FAssetData& AssetData : Assets)
	{
		UIKRetargeter* Retargeter = Cast<UIKRetargeter>(AssetData.GetAsset());
		const UIKRigDefinition* TgtRig = Retargeter ? Retargeter->GetIKRig(ERetargetSourceOrTarget::Target) : nullptr;
		if (TgtRig != nullptr && TgtRig->GetPreviewMesh() == Mesh)
		{
			return Retargeter;
		}
	}
	return nullptr;
}

#else // !VRM4U_RETARGET_SETUP: stubs so the module links on older engines

FVrmRetargetSetupReport UVrmRetargetSetupUtil::SetupRetarget(USkeletalMesh*, USkeletalMesh*, const FString&)
{
	FVrmRetargetSetupReport Report;
	Log(Report.Messages, TEXT("Retarget setup requires UE 5.6+."));
	return Report;
}

FVrmRetargetSetupReport UVrmRetargetSetupUtil::SetupRetargetForComponent(USkeletalMeshComponent*)
{
	FVrmRetargetSetupReport Report;
	Log(Report.Messages, TEXT("Retarget setup requires UE 5.6+."));
	return Report;
}

bool UVrmRetargetSetupUtil::ApplyRetargetAnimClassToComponent(USkeletalMeshComponent*) { return false; }
USkeletalMeshComponent* UVrmRetargetSetupUtil::FindLikelySourceComponent(USkeletalMeshComponent*) { return nullptr; }
bool UVrmRetargetSetupUtil::IsVrmHumanoidMesh(USkeletalMesh* Mesh) { return ResolvesAsVrmHumanoid(Mesh); }
bool UVrmRetargetSetupUtil::IsNativeVrmHumanoidMesh(USkeletalMesh* Mesh) { return ResolvesAsNativeVrmHumanoid(Mesh); }
UIKRetargeter* UVrmRetargetSetupUtil::FindRetargeterTargetingMesh(USkeletalMesh*) { return nullptr; }

#endif // VRM4U_RETARGET_SETUP

// -- VMC face (LiveLink) setup: no IK Rig API involved, so available on every editor build --

namespace
{
	// The engine's Live Link Face anim blueprint that MetaHuman templates ship
	// (Content/MetaHumans/Common/Animation). Located by name via the asset registry so
	// the plugin needs no hard content reference into the project.
	UClass* FindMetaHumanLiveLinkFaceAnimClass()
	{
		const FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FAssetData> Assets;
		AssetRegistry.Get().GetAssetsByClass(UAnimBlueprint::StaticClass()->GetClassPathName(), Assets, true);
		for (const FAssetData& AssetData : Assets)
		{
			if (AssetData.AssetName != FName(TEXT("ABP_MH_LiveLink")))
			{
				continue;
			}
			const UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(AssetData.GetAsset());
			if (AnimBlueprint != nullptr && AnimBlueprint->GeneratedClass != nullptr)
			{
				return AnimBlueprint->GeneratedClass;
			}
		}
		return nullptr;
	}
}

bool UVrmRetargetSetupUtil::AnimClassConsumesLiveLink(const UClass* AnimClass)
{
	if (AnimClass == nullptr)
	{
		return false;
	}
	static const FName LiveLinkSubjectNameStructName(TEXT("LiveLinkSubjectName"));
	for (TFieldIterator<FStructProperty> It(AnimClass); It; ++It)
	{
		if (It->Struct != nullptr && It->Struct->GetFName() == LiveLinkSubjectNameStructName)
		{
			return true;
		}
	}
	return false;
}

FVrmRetargetSetupReport UVrmRetargetSetupUtil::SetupVMCFaceForActor(AActor* Actor, FName SubjectName)
{
	FVrmRetargetSetupReport Report;
	if (Actor == nullptr)
	{
		Log(Report.Messages, TEXT("FAILED: no actor given for VMC face setup."));
		return Report;
	}
	USkeletalMeshComponent* FaceComponent = UVrmVMCFaceLiveLinkComponent::FindFaceMeshOnActor(Actor);
	if (FaceComponent == nullptr)
	{
		Log(Report.Messages, FString::Printf(TEXT("FAILED: '%s' has no face skeletal mesh (MetaHuman Face_Archetype skeleton or a component named 'Face')."), *Actor->GetActorLabel()));
		return Report;
	}
	if (SubjectName.IsNone())
	{
		SubjectName = GetDefault<UVrmVMCFaceLiveLinkComponent>()->SubjectName;
	}
	Log(Report.Messages, FString::Printf(TEXT("Face setup: '%s' / %s, LiveLink subject '%s'."),
		*Actor->GetActorLabel(), *FaceComponent->GetName(), *SubjectName.ToString()));

	FScopedTransaction Transaction(LOCTEXT("SetupVmcFace", "VRM4U: Setup VMC Face LiveLink"));

	// 1. The face mesh needs a LiveLink-consuming anim class (the template's stock
	//    ABP_MH_LiveLink). An existing one is kept. Custom face ABPs stay untouched.
	if (AnimClassConsumesLiveLink(FaceComponent->GetAnimClass()))
	{
		Log(Report.Messages, FString::Printf(TEXT("Face anim class '%s' already consumes LiveLink - keeping it."),
			*FaceComponent->GetAnimClass()->GetName()));
	}
	else
	{
		UClass* FaceAnimClass = FindMetaHumanLiveLinkFaceAnimClass();
		if (FaceAnimClass == nullptr)
		{
			Log(Report.Messages, TEXT("FAILED: no ABP_MH_LiveLink asset found in this project. Add the MetaHuman common assets (or assign a LiveLink face AnimBP to the face mesh manually) and re-run."));
			return Report;
		}
		FaceComponent->Modify();
		FaceComponent->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		FaceComponent->SetAnimInstanceClass(FaceAnimClass);
		Log(Report.Messages, FString::Printf(TEXT("Set face anim class to '%s'."), *FaceAnimClass->GetName()));
	}

	// 2. The bridge component publishes the VMC curves as the LiveLink subject at play
	//    time and pushes the subject name into the face anim instance.
	UVrmVMCFaceLiveLinkComponent* Bridge = Actor->FindComponentByClass<UVrmVMCFaceLiveLinkComponent>();
	if (Bridge == nullptr)
	{
		Actor->Modify();
		Bridge = NewObject<UVrmVMCFaceLiveLinkComponent>(Actor,
			MakeUniqueObjectName(Actor, UVrmVMCFaceLiveLinkComponent::StaticClass(), TEXT("VrmVMCFaceLiveLink")),
			RF_Transactional);
		Actor->AddInstanceComponent(Bridge);
		Bridge->RegisterComponent();
		Log(Report.Messages, FString::Printf(TEXT("Added VMC Face LiveLink component (VMC endpoint %s:%d - match your body VMC settings)."),
			*Bridge->ServerAddress, Bridge->Port));
	}
	else
	{
		Log(Report.Messages, TEXT("Reusing the actor's existing VMC Face LiveLink component."));
	}
	Bridge->Modify();
	Bridge->SubjectName = SubjectName;

	// 3. Heads-up only: the bridge itself degrades gracefully at play time.
	if (!UVrmVMCFaceLiveLinkComponent::IsLiveLinkClientAvailable())
	{
		Log(Report.Messages, TEXT("WARNING: the LiveLink plugin is disabled in this project - the face bridge will stay inactive until you enable 'Live Link' (Edit > Plugins)."));
	}

	Report.bSuccess = true;
	Log(Report.Messages, FString::Printf(TEXT("VMC face setup complete. Press Play: face blendshapes stream as LiveLink subject '%s'."), *SubjectName.ToString()));
	return Report;
}

#undef LOCTEXT_NAMESPACE
