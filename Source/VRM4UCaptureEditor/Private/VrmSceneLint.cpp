// VRM4U Copyright (c) 2021-2026 Haruyoshi Yamamoto. This software is released under the MIT License.

#include "VrmSceneLint.h"
#include "VrmRetargetSetupUtil.h"
#include "VRM4UCaptureEditorLog.h"

#include <initializer_list>

#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimNode_LinkedAnimGraph.h"
#include "Animation/Skeleton.h"
#include "AnimNode_VrmVMC.h"
#include "VrmVMCFaceLiveLinkComponent.h"

#if VRM4U_RETARGET_SETUP
#include "VrmVMCRetargetAnimInstance.h"
#include "VrmRigHeader.h"
#endif

namespace
{
	FVrmSceneLintFinding MakeFinding(EVrmLintSeverity Severity, const TCHAR* CheckId, const FString& Detail, const FString& FixHint)
	{
		FVrmSceneLintFinding Finding;
		Finding.Severity = Severity;
		Finding.CheckId = CheckId;
		Finding.Detail = Detail;
		Finding.FixHint = FixHint;
		return Finding;
	}

	// True when the (generated) anim class contains an FAnimNode_VrmVMC anywhere in its graph,
	// following Linked Anim Graph / Linked Anim Layer nodes into their separately-compiled
	// instance classes (whose nodes are NOT in the outer class's GetAnimNodeProperties()).
	bool AnimClassContainsVmcNode(const UClass* AnimClass, TSet<const UClass*>& Visited, int32 Depth)
	{
		const UAnimBlueprintGeneratedClass* Generated = Cast<UAnimBlueprintGeneratedClass>(AnimClass);
		if (Generated == nullptr || Depth > 8)
		{
			return false;
		}
		bool bAlreadyVisited = false;
		Visited.Add(Generated, &bAlreadyVisited);
		if (bAlreadyVisited)
		{
			return false;
		}
		const UObject* CDO = Generated->GetDefaultObject();
		for (const FStructProperty* Property : Generated->GetAnimNodeProperties())
		{
			if (Property == nullptr || Property->Struct == nullptr)
			{
				continue;
			}
			if (Property->Struct->IsChildOf(FAnimNode_VrmVMC::StaticStruct()))
			{
				return true;
			}
			// FAnimNode_LinkedAnimLayer derives from FAnimNode_LinkedAnimGraph; both expose InstanceClass.
			if (CDO != nullptr && Property->Struct->IsChildOf(FAnimNode_LinkedAnimGraph::StaticStruct()))
			{
				const FAnimNode_LinkedAnimGraph* Linked = Property->ContainerPtrToValuePtr<FAnimNode_LinkedAnimGraph>(CDO);
				if (Linked != nullptr &&
					AnimClassContainsVmcNode(Linked->InstanceClass.Get(), Visited, Depth + 1))
				{
					return true;
				}
			}
		}
		return false;
	}

	bool AnimClassContainsVmcNode(const UClass* AnimClass)
	{
		TSet<const UClass*> Visited;
		return AnimClassContainsVmcNode(AnimClass, Visited, 0);
	}

	FTransform ComponentSpaceRefTransform(const FReferenceSkeleton& Ref, int32 BoneIndex)
	{
		FTransform Transform = FTransform::Identity;
		while (BoneIndex != INDEX_NONE)
		{
			Transform = Transform * Ref.GetRefBonePose()[BoneIndex];
			BoneIndex = Ref.GetParentIndex(BoneIndex);
		}
		return Transform;
	}

	// The direct VMC path breaks when the bind pose differs from the source's T-pose (the
	// A-pose MetaHuman/DAZ failure); a T-pose bind (Mixamo) tracks 1:1. Measure the bind
	// arm elevation instead of guessing by rig family.
	// Returns +1 (T-pose-ish), -1 (A-pose / lowered arms), 0 (arm bones not found).
	int32 ClassifyBindArmPose(USkeletalMesh* Mesh)
	{
		const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
		const TCHAR* Candidates[][2] = {
			{ TEXT("upperarm_l"), TEXT("hand_l") },             // MetaHuman / UE
			{ TEXT("LeftArm"), TEXT("LeftHand") },              // Mixamo / standard MetaHuman naming
			{ TEXT("mixamorig:LeftArm"), TEXT("mixamorig:LeftHand") },
			{ TEXT("lShldrBend"), TEXT("lHand") },              // DAZ G8
			{ TEXT("l_upperarm"), TEXT("l_hand") },
		};
		for (const auto& Pair : Candidates)
		{
			const int32 UpperArm = Ref.FindBoneIndex(Pair[0]);
			const int32 Hand = Ref.FindBoneIndex(Pair[1]);
			if (UpperArm == INDEX_NONE || Hand == INDEX_NONE)
			{
				continue;
			}
			FVector Dir = ComponentSpaceRefTransform(Ref, Hand).GetLocation()
				- ComponentSpaceRefTransform(Ref, UpperArm).GetLocation();
			if (!Dir.Normalize())
			{
				return 0;
			}
			const double ElevationDeg = FMath::RadiansToDegrees(FMath::Asin(FMath::Abs(Dir.Z)));
			return ElevationDeg <= 25.0 ? +1 : -1;
		}
		return 0;
	}

	FString ComponentLabel(const USkeletalMeshComponent* Component)
	{
		const AActor* Owner = Component->GetOwner();
		return FString::Printf(TEXT("%s / %s (%s)"),
			Owner ? *Owner->GetActorLabel() : TEXT("<no actor>"),
			*Component->GetName(),
			Component->GetSkeletalMeshAsset() ? *Component->GetSkeletalMeshAsset()->GetName() : TEXT("<no mesh>"));
	}

#if VRM4U_RETARGET_SETUP
	void LintRetargeterAssets(USkeletalMesh* Mesh, const FString& Label, TArray<FVrmSceneLintFinding>& Findings)
	{
		UIKRetargeter* Retargeter = UVrmRetargetSetupUtil::FindRetargeterTargetingMesh(Mesh);
		if (Retargeter == nullptr)
		{
			return;
		}
		const UIKRetargeterController* Controller = UIKRetargeterController::GetController(Retargeter);
		if (Controller == nullptr)
		{
			return;
		}

		// Zero-offset target retarget pose: motion transfers but every angle is offset by
		// the bind-pose difference (A-pose arms ride ~45 degrees low on a T-pose source).
		{
			const FIKRetargetPose& Pose = Controller->GetCurrentRetargetPose(ERetargetSourceOrTarget::Target);
			bool bHasOffsets = !Pose.GetRootTranslationDelta().IsNearlyZero();
			for (const TPair<FName, FQuat>& Pair : Pose.GetAllDeltaRotations())
			{
				if (!Pair.Value.IsIdentity(KINDA_SMALL_NUMBER)) { bHasOffsets = true; break; }
			}
			if (!bHasOffsets)
			{
				Findings.Add(MakeFinding(EVrmLintSeverity::Error, TEXT("ZeroOffsetRetargetPose"),
					FString::Printf(TEXT("'%s' (targets %s): current target retarget pose '%s' has zero offsets."),
						*Retargeter->GetName(), *Label, *Controller->GetCurrentRetargetPoseName(ERetargetSourceOrTarget::Target).ToString()),
					TEXT("Run 'VRM4U: Auto-Setup VMC Retarget' on the actor (heals in place), or in the RTG editor create a pose and Auto-Align All toward the target.")));
			}
		}

		// Stale per-op IK rig: an op still resolving chains against a rig other than the
		// asset-level target (the create-from-source-rig trap).
		{
			const UIKRigDefinition* AssetTarget = Retargeter->GetIKRig(ERetargetSourceOrTarget::Target);
			const int32 NumOps = Controller->GetNumRetargetOps();
			for (int32 OpIndex = 0; OpIndex < NumOps; ++OpIndex)
			{
				const FName OpName = Controller->GetOpName(OpIndex);
				const UIKRigDefinition* OpRig = Controller->GetTargetIKRigForOp(OpName);
				if (OpRig != nullptr && AssetTarget != nullptr && OpRig != AssetTarget)
				{
					Findings.Add(MakeFinding(EVrmLintSeverity::Error, TEXT("StalePerOpIKRig"),
						FString::Printf(TEXT("'%s': op '%s' uses IK rig '%s' but the asset target rig is '%s'."),
							*Retargeter->GetName(), *OpName.ToString(), *OpRig->GetName(), *AssetTarget->GetName()),
						TEXT("In the RTG's op stack set each op's IK Rig Asset to the target rig, then Auto-Map Chains -> Map All (Exact). Cleanest: delete + recreate via the setup wizard.")));
				}
			}
		}

		// Chain-mapping coverage, judged on the best-mapped op and by SUBSTRING, not exact
		// standard names: hand-built rigs legitimately use LeftShoulder-style names — what
		// breaks mocap is a missing PAIR, not a nonstandard name.
		{
			const FRetargetChainMapping* BestMapping = nullptr;
			int32 BestMapped = 0;
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
				if (Count > BestMapped)
				{
					BestMapped = Count;
					BestMapping = Mapping;
				}
			}

			auto HasMappedChainContaining = [BestMapping](std::initializer_list<const TCHAR*> AnyOf) -> bool
			{
				if (BestMapping == nullptr)
				{
					return false;
				}
				for (const FRetargetChainPair& Pair : BestMapping->GetChainPairs())
				{
					if (Pair.SourceChainName == NAME_None)
					{
						continue;
					}
					const FString Target = Pair.TargetChainName.ToString();
					for (const TCHAR* Sub : AnyOf)
					{
						if (Target.Contains(Sub))
						{
							return true;
						}
					}
				}
				return false;
			};

			if (BestMapped == 0)
			{
				Findings.Add(MakeFinding(EVrmLintSeverity::Error, TEXT("ChainMappingEmpty"),
					FString::Printf(TEXT("'%s' (targets %s): no chains are mapped in any op."), *Retargeter->GetName(), *Label),
					TEXT("In the RTG's chain panel run Auto-Map Chains -> Map All (Exact/Fuzzy), or recreate via the setup wizard.")));
			}
			else
			{
				const struct { const TCHAR* What; std::initializer_list<const TCHAR*> Subs; } Required[] = {
					{ TEXT("left arm"), { TEXT("LeftArm") } },
					{ TEXT("right arm"), { TEXT("RightArm") } },
					{ TEXT("left leg"), { TEXT("LeftLeg") } },
					{ TEXT("right leg"), { TEXT("RightLeg") } },
				};
				for (const auto& Req : Required)
				{
					if (!HasMappedChainContaining(Req.Subs))
					{
						Findings.Add(MakeFinding(EVrmLintSeverity::Error, TEXT("LimbChainUnmapped"),
							FString::Printf(TEXT("'%s' (targets %s): no mapped %s chain — that limb will stay limp."), *Retargeter->GetName(), *Label, Req.What),
							TEXT("Add/map the chain on both rigs (RTG chain panel -> Auto-Map Chains), or recreate via the setup wizard.")));
					}
				}
				// Clavicle carries ~half of a full arm raise from XR Animator; without a
				// mapped chain the arms stop ~50 degrees short of overhead.
				if (!HasMappedChainContaining({ TEXT("Clavicle"), TEXT("Shoulder") }))
				{
					Findings.Add(MakeFinding(EVrmLintSeverity::Warning, TEXT("ClavicleChainUnmapped"),
						FString::Printf(TEXT("'%s' (targets %s): no mapped clavicle/shoulder chain — arm raises will fall ~50 degrees short."), *Retargeter->GetName(), *Label),
						TEXT("Add single-bone shoulder chains on both rigs and re-map (see the VRM4U retarget setup guide).")));
				}
				int32 MappedFingers = 0;
				for (const TCHAR* Finger : { TEXT("Thumb"), TEXT("Index"), TEXT("Middle"), TEXT("Ring"), TEXT("Pinky"), TEXT("Little") })
				{
					MappedFingers += HasMappedChainContaining({ Finger }) ? 1 : 0;
				}
				if (MappedFingers > 0 && MappedFingers < 5)
				{
					Findings.Add(MakeFinding(EVrmLintSeverity::Warning, TEXT("FingerChainsPartiallyMapped"),
						FString::Printf(TEXT("'%s' (targets %s): only %d finger chain kinds mapped — hand tracking will be partial."), *Retargeter->GetName(), *Label, MappedFingers),
						TEXT("Add the missing finger chains on BOTH rigs (proximal -> distal), re-map, re-align the new bones.")));
				}
				else if (MappedFingers == 0)
				{
					Findings.Add(MakeFinding(EVrmLintSeverity::Warning, TEXT("FingerChainsUnmapped"),
						FString::Printf(TEXT("'%s' (targets %s): no finger chains mapped — fists/hand tracking will not transfer."), *Retargeter->GetName(), *Label),
						TEXT("Add finger chains on BOTH rigs (proximal -> distal), re-map, then re-align the new bones.")));
				}
			}
		}
	}
#endif // VRM4U_RETARGET_SETUP
}

TArray<FVrmSceneLintFinding> UVrmSceneLint::LintComponent(USkeletalMeshComponent* Component)
{
	TArray<FVrmSceneLintFinding> Findings;
	USkeletalMesh* Mesh = Component ? Component->GetSkeletalMeshAsset() : nullptr;
	if (Mesh == nullptr)
	{
		return Findings;
	}
	const FString Label = ComponentLabel(Component);
	// NATIVE check on purpose: a DAZ/MetaHuman with an AutoPopulate meta table still has a
	// non-VRM bind pose, so the direct VMC path is exactly as broken for it.
	const bool bIsNativeVrm = UVrmRetargetSetupUtil::IsNativeVrmHumanoidMesh(Mesh);

	// Direct VMC node on a non-VRM rig: the exact broken path of the MetaHuman/DAZ
	// investigation. The node's legacy rebase assumes a T-pose-like bind, so measure the
	// actual bind arm elevation: a T-pose rig (Mixamo — our gold standard) merely gets a
	// heads-up, an A-pose rig (MetaHuman/DAZ) is visibly broken.
	const UClass* AnimClass = Component->GetAnimClass();
	if (!bIsNativeVrm && AnimClassContainsVmcNode(AnimClass))
	{
		if (ClassifyBindArmPose(Mesh) > 0)
		{
			Findings.Add(MakeFinding(EVrmLintSeverity::Warning, TEXT("VmcNodeOnNonVrmRig"),
				FString::Printf(TEXT("%s: anim class '%s' drives this non-VRM rig with a direct VMC node. Its bind pose measures as T-pose, so this can track correctly - but it depends on that bind pose staying T-pose."), *Label, *AnimClass->GetName()),
				TEXT("Working setups can stay; for new rigs prefer 'VRM4U: Auto-Setup VMC Retarget'.")));
		}
		else
		{
			Findings.Add(MakeFinding(EVrmLintSeverity::Error, TEXT("VmcNodeOnNonVrmRig"),
				FString::Printf(TEXT("%s: anim class '%s' drives this non-VRM, non-T-pose rig with a direct VMC node - poses will be visibly wrong (arms and legs ride offset from the source)."), *Label, *AnimClass->GetName()),
				TEXT("Use the IK Retargeter path instead: run 'VRM4U: Auto-Setup VMC Retarget' on this actor.")));
		}
	}

	// VMC node inside a post-process AnimBP: runs after the main instance on every
	// instance of that MESH asset - never intended, and it fights the main graph.
	if (const UClass* PostProcessClass = Mesh->GetPostProcessAnimBlueprint())
	{
		if (AnimClassContainsVmcNode(PostProcessClass))
		{
			Findings.Add(MakeFinding(EVrmLintSeverity::Error, TEXT("VmcNodeInPostProcess"),
				FString::Printf(TEXT("%s: post-process AnimBP '%s' on the MESH asset contains a VMC node."), *Label, *PostProcessClass->GetName()),
				TEXT("Remove the VMC node from the post-process AnimBP; drive the rig from its main anim class (or the retarget instance) instead.")));
		}
	}

	// -- VMC face bridge checks: a MetaHuman face riding on a VMC-driven body --
	// The double gap the 2026-07 face investigation found: the VMC node cannot reach the
	// separate face component, and the face is RigLogic-driven anyway. The supported path
	// is the VMC -> LiveLink ARKit bridge; flag actors that are half-wired.
	{
		const USkeleton* Skeleton = Mesh->GetSkeleton();
		AActor* Owner = Component->GetOwner();
		if (Owner != nullptr && Skeleton != nullptr && Skeleton->GetName().Contains(TEXT("Face_Archetype")))
		{
			bool bBodyVmcDriven = false;
			TInlineComponentArray<USkeletalMeshComponent*> Siblings(Owner);
			for (USkeletalMeshComponent* Sibling : Siblings)
			{
				if (Sibling == nullptr || Sibling == Component)
				{
					continue;
				}
				const UClass* SiblingAnimClass = Sibling->GetAnimClass();
				if (SiblingAnimClass == nullptr)
				{
					continue;
				}
				if (AnimClassContainsVmcNode(SiblingAnimClass))
				{
					bBodyVmcDriven = true;
					break;
				}
#if VRM4U_RETARGET_SETUP
				if (SiblingAnimClass->IsChildOf(UVrmVMCRetargetAnimInstance::StaticClass()))
				{
					bBodyVmcDriven = true;
					break;
				}
#endif
			}
			if (bBodyVmcDriven)
			{
				const UVrmVMCFaceLiveLinkComponent* Bridge = Owner->FindComponentByClass<UVrmVMCFaceLiveLinkComponent>();
				if (!UVrmRetargetSetupUtil::AnimClassConsumesLiveLink(Component->GetAnimClass()))
				{
					Findings.Add(MakeFinding(EVrmLintSeverity::Warning, TEXT("FaceNotDriven"),
						FString::Printf(TEXT("%s: the body follows VMC, but the face anim class ('%s') consumes no LiveLink subject - the face will stay frozen while the body moves."),
							*Label, Component->GetAnimClass() ? *Component->GetAnimClass()->GetName() : TEXT("<none>")),
						TEXT("Run 'VRM4U: Auto-Setup VMC Face (LiveLink)' on the actor (assigns ABP_MH_LiveLink + the VMC face bridge component).")));
				}
				else if (Bridge == nullptr)
				{
					Findings.Add(MakeFinding(EVrmLintSeverity::Warning, TEXT("FaceBridgeMissing"),
						FString::Printf(TEXT("%s: the face anim class consumes LiveLink, but nothing on this actor publishes the VMC face subject (fine if an iPhone/Live Link Face drives it)."), *Label),
						TEXT("To drive the face from the VMC stream, run 'VRM4U: Auto-Setup VMC Face (LiveLink)' on the actor (adds the bridge component).")));
				}
				else if (!UVrmVMCFaceLiveLinkComponent::IsLiveLinkClientAvailable())
				{
					Findings.Add(MakeFinding(EVrmLintSeverity::Error, TEXT("LiveLinkPluginDisabled"),
						FString::Printf(TEXT("%s: a VMC Face LiveLink component is set up, but the LiveLink plugin is disabled - the bridge stays inactive."), *Label),
						TEXT("Enable the engine-bundled 'Live Link' plugin (Edit > Plugins > Live Link) and restart the editor.")));
				}
			}
		}
	}

#if VRM4U_RETARGET_SETUP
	// Asset-level checks for whatever retargeter targets this mesh.
	LintRetargeterAssets(Mesh, Label, Findings);

	// An RTG for this mesh exists but the component isn't using the retarget instance -
	// someone did the asset work and missed the level wiring.
	if (!bIsNativeVrm &&
		UVrmRetargetSetupUtil::FindRetargeterTargetingMesh(Mesh) != nullptr &&
		(AnimClass == nullptr || !AnimClass->IsChildOf(UVrmVMCRetargetAnimInstance::StaticClass())))
	{
		Findings.Add(MakeFinding(EVrmLintSeverity::Warning, TEXT("RetargetInstanceNotUsed"),
			FString::Printf(TEXT("%s: a retargeter targets this mesh, but the anim class is '%s'."),
				*Label, AnimClass ? *AnimClass->GetName() : TEXT("<none>")),
			TEXT("Set the component's Anim Class to VrmVMCRetargetAnimInstance (auto-resolve finds the RTG and source at play time).")));
	}
#endif

	return Findings;
}

TArray<FVrmSceneLintFinding> UVrmSceneLint::LintWorld(UObject* WorldContextObject)
{
	TArray<FVrmSceneLintFinding> Findings;
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (World == nullptr)
	{
		return Findings;
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		TInlineComponentArray<USkeletalMeshComponent*> Components(*It);
		for (USkeletalMeshComponent* Component : Components)
		{
			Findings.Append(LintComponent(Component));
		}
	}
	return Findings;
}

int32 UVrmSceneLint::LintWorldAndLog(UObject* WorldContextObject)
{
	const TArray<FVrmSceneLintFinding> Findings = LintWorld(WorldContextObject);
	if (Findings.Num() == 0)
	{
		UE_LOG(LogVRM4UCaptureEditor, Display, TEXT("[SceneLint] Clean: no VMC retarget issues found."));
		return 0;
	}
	for (const FVrmSceneLintFinding& Finding : Findings)
	{
		UE_LOG(LogVRM4UCaptureEditor, Warning, TEXT("[SceneLint] %s %s: %s FIX: %s"),
			Finding.Severity == EVrmLintSeverity::Error ? TEXT("ERROR") : TEXT("WARN"),
			*Finding.CheckId, *Finding.Detail, *Finding.FixHint);
	}
	return Findings.Num();
}
