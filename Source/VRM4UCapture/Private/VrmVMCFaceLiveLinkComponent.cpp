// VRM4U Copyright (c) 2021-2026 Haruyoshi Yamamoto. This software is released under the MIT License.

#include "VrmVMCFaceLiveLinkComponent.h"
#include "VrmVMCFaceLiveLink.h"
#include "VRM4U_VMCSubsystem.h"
#include "VRM4UCaptureLog.h"

#include "Animation/AnimInstance.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "LiveLinkTypes.h"
#include "UObject/UnrealType.h"

namespace
{
	// Seconds to keep retrying the anim-instance subject bind before giving up (anim
	// instances on budgeted/URO'd meshes can be created a few frames after BeginPlay).
	constexpr double BindRetryWindowSeconds = 10.0;

	// The FLiveLinkSubjectName property of the face anim instance that names the FACE
	// subject. ABP_MH_LiveLink calls it "LLink Face Subj" and also carries a head
	// subject ("LLink Face Head") that must NOT be captured.
	FStructProperty* FindFaceSubjectProperty(const UClass* AnimClass)
	{
		FStructProperty* BestProperty = nullptr;
		int32 BestScore = 0;
		for (TFieldIterator<FStructProperty> It(AnimClass); It; ++It)
		{
			FStructProperty* Property = *It;
			if (Property->Struct != FLiveLinkSubjectName::StaticStruct())
			{
				continue;
			}
			const FString Name = Property->GetName();
			if (Name.Contains(TEXT("Head")))
			{
				continue;
			}
			const int32 Score = 1
				+ (Name.Contains(TEXT("Subj")) ? 2 : 0)
				+ (Name.Contains(TEXT("Face")) ? 4 : 0);
			if (Score > BestScore)
			{
				BestScore = Score;
				BestProperty = Property;
			}
		}
		return BestProperty;
	}
}

const FName UVrmVMCFaceLiveLinkComponent::DefaultSubjectName(TEXT("VMCFace"));

UVrmVMCFaceLiveLinkComponent::UVrmVMCFaceLiveLinkComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

bool UVrmVMCFaceLiveLinkComponent::StartBridge()
{
	if (!IsLiveLinkClientAvailable())
	{
		UE_LOG(LogVRM4UCapture, Warning,
			TEXT("[FaceLiveLink] '%s': LiveLink plugin is not enabled - the VMC face bridge is inactive. Enable the engine's 'Live Link' plugin to drive this face from VMC."),
			*GetPathName());
		return false;
	}
	UVRM4U_VMCSubsystem* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UVRM4U_VMCSubsystem>() : nullptr;
	if (Subsystem == nullptr)
	{
		return false;
	}
	return Subsystem->StartVMCFaceLiveLink(SubjectName, ServerAddress, Port);
}

void UVrmVMCFaceLiveLinkComponent::StopBridge()
{
	UVRM4U_VMCSubsystem* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UVRM4U_VMCSubsystem>() : nullptr;
	if (Subsystem != nullptr)
	{
		Subsystem->StopVMCFaceLiveLink(SubjectName);
	}
}

bool UVrmVMCFaceLiveLinkComponent::IsBridgeActive() const
{
	UVRM4U_VMCSubsystem* Subsystem = GEngine ? GEngine->GetEngineSubsystem<UVRM4U_VMCSubsystem>() : nullptr;
	return Subsystem != nullptr && Subsystem->IsVMCFaceLiveLinkActive(SubjectName);
}

bool UVrmVMCFaceLiveLinkComponent::IsLiveLinkClientAvailable()
{
	return VrmVMCFaceLiveLink::IsLiveLinkClientAvailable();
}

USkeletalMeshComponent* UVrmVMCFaceLiveLinkComponent::FindFaceMeshComponent() const
{
	return FindFaceMeshOnActor(GetOwner(), FaceComponentName);
}

USkeletalMeshComponent* UVrmVMCFaceLiveLinkComponent::FindFaceMeshOnActor(const AActor* Actor, FName ExplicitComponentName)
{
	if (Actor == nullptr)
	{
		return nullptr;
	}
	USkeletalMeshComponent* NamedFace = nullptr;
	TInlineComponentArray<USkeletalMeshComponent*> Components(Actor);
	for (USkeletalMeshComponent* Component : Components)
	{
		if (Component == nullptr)
		{
			continue;
		}
		if (!ExplicitComponentName.IsNone())
		{
			if (Component->GetFName() == ExplicitComponentName)
			{
				return Component;
			}
			continue;
		}
		// MetaHuman faces (any generation) share the Face_Archetype skeleton asset.
		const USkeletalMesh* Mesh = Component->GetSkeletalMeshAsset();
		const USkeleton* Skeleton = Mesh ? Mesh->GetSkeleton() : nullptr;
		if (Skeleton != nullptr && Skeleton->GetName().Contains(TEXT("Face_Archetype")))
		{
			return Component;
		}
		if (Component->GetFName() == FName(TEXT("Face")))
		{
			NamedFace = Component;
		}
	}
	return NamedFace;
}

void UVrmVMCFaceLiveLinkComponent::BeginPlay()
{
	Super::BeginPlay();

	// Give a default-configured component a per-actor-unique subject so two default
	// actors (e.g. two MetaHumans) don't collide on one FaceLiveLinkSources entry.
	if (SubjectName.IsNone() || SubjectName == DefaultSubjectName)
	{
		const AActor* Owner = GetOwner();
		const FString Base = Owner ? Owner->GetName() : GetName();
		SubjectName = FName(*(Base + TEXT("_VMCFace")));
	}

	if (bAutoStart)
	{
		StartBridge();
	}
	if (bBindFaceAnimInstanceSubject)
	{
		if (TryBindFaceSubject())
		{
			// Bound (or determined impossible) immediately. If it really bound, keep a
			// cheap 1s watch so a later anim-instance swap re-arms the bind.
			if (BoundAnimInstance.IsValid())
			{
				SetComponentTickInterval(1.0f);
				SetComponentTickEnabled(true);
			}
		}
		else
		{
			bBindPending = true;
			BindDeadlineSeconds = FPlatformTime::Seconds() + BindRetryWindowSeconds;
			SetComponentTickEnabled(true);
		}
	}
}

void UVrmVMCFaceLiveLinkComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopBridge();
	Super::EndPlay(EndPlayReason);
}

void UVrmVMCFaceLiveLinkComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bBindFaceAnimInstanceSubject)
	{
		SetComponentTickEnabled(false);
		return;
	}

	if (bBindPending)
	{
		// Initial bounded-retry phase (fast tick).
		if (TryBindFaceSubject())
		{
			bBindPending = false;
			if (BoundAnimInstance.IsValid())
			{
				SetComponentTickInterval(1.0f); // switch to the cheap swap watch
			}
			else
			{
				SetComponentTickEnabled(false); // impossible to bind: stop
			}
		}
		else if (FPlatformTime::Seconds() > BindDeadlineSeconds)
		{
			UE_LOG(LogVRM4UCapture, Warning,
				TEXT("[FaceLiveLink] '%s': no face anim instance appeared to bind subject '%s' to. Set the face mesh's Anim Class (e.g. ABP_MH_LiveLink) or point its LiveLink subject at '%s' manually."),
				*GetPathName(), *SubjectName.ToString(), *SubjectName.ToString());
			bBindPending = false;
			SetComponentTickEnabled(false);
		}
		return;
	}

	// Periodic (1s) watch: re-arm the bind after a runtime anim-instance swap
	// (SetAnimInstanceClass / mesh swap) so the face doesn't freeze.
	USkeletalMeshComponent* FaceComponent = FindFaceMeshComponent();
	UAnimInstance* Current = FaceComponent ? FaceComponent->GetAnimInstance() : nullptr;
	if (Current != nullptr && Current != BoundAnimInstance.Get())
	{
		TryBindFaceSubject();
	}
}

bool UVrmVMCFaceLiveLinkComponent::TryBindFaceSubject()
{
	USkeletalMeshComponent* FaceComponent = FindFaceMeshComponent();
	if (FaceComponent == nullptr)
	{
		UE_LOG(LogVRM4UCapture, Warning,
			TEXT("[FaceLiveLink] '%s': no face skeletal mesh component found on this actor - nothing to bind subject '%s' to."),
			*GetPathName(), *SubjectName.ToString());
		return true; // No point retrying: the component set won't change.
	}
	UAnimInstance* AnimInstance = FaceComponent->GetAnimInstance();
	if (AnimInstance == nullptr)
	{
		return false; // Retry: instances can be created a few frames after BeginPlay.
	}
	FStructProperty* SubjectProperty = FindFaceSubjectProperty(AnimInstance->GetClass());
	if (SubjectProperty == nullptr)
	{
		UE_LOG(LogVRM4UCapture, Warning,
			TEXT("[FaceLiveLink] '%s': face anim class '%s' has no FLiveLinkSubjectName variable - it does not consume LiveLink. Use ABP_MH_LiveLink (or equivalent) on the face mesh."),
			*GetPathName(), *AnimInstance->GetClass()->GetName());
		return true; // The class won't grow the property at runtime.
	}
	FLiveLinkSubjectName* Value = SubjectProperty->ContainerPtrToValuePtr<FLiveLinkSubjectName>(AnimInstance);
	Value->Name = SubjectName;
	BoundAnimInstance = AnimInstance;
	UE_LOG(LogVRM4UCapture, Display,
		TEXT("[FaceLiveLink] '%s': bound LiveLink subject '%s' to '%s.%s'."),
		*GetPathName(), *SubjectName.ToString(), *AnimInstance->GetClass()->GetName(), *SubjectProperty->GetName());
	return true;
}
