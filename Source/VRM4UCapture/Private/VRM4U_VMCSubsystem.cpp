// Fill out your copyright notice in the Description page of Project Settings.


#include "VRM4U_VMCSubsystem.h"
#include "VrmVMCObject.h"
#include "VrmVMCLiveLinkSource.h"
#include "VrmVMCFaceLiveLink.h"
#include "VRM4UCaptureLog.h"
#include "OWLVrmVMCNodeRegistry.h"
#include "AnimNode_VrmVMC.h"

#include "Engine/Engine.h"
#include "Features/IModularFeatures.h"
#include "ILiveLinkClient.h"
#include "UObject/StrongObjectPtr.h"
#include "Misc/ScopeLock.h"
#include "Async/Async.h"
#include "OSCManager.h"
#include "OSCServer.h"

namespace
{
	// Case-insensitive lookup into FVMCData::BoneData / CurveData.
	// VMC wire keys preserve authored casing (e.g. "leftUpperArm").
	template <typename ValueType>
	const ValueType* FindCaseInsensitive(const TMap<FString, ValueType>& Map, const FString& Key)
	{
		for (const auto& Pair : Map)
		{
			if (Pair.Key.Compare(Key, ESearchCase::IgnoreCase) == 0)
			{
				return &Pair.Value;
			}
		}
		return nullptr;
	}
}

UVrmVMCObject* UVRM4U_VMCSubsystem::FindServer_NoLock(const FString& ServerAddress, int port) const
{
	for (int i = 0; i < VMCObjectList.Num(); ++i)
	{
		UVrmVMCObject* a = VMCObjectList[i].Get();
		if (a == nullptr) continue;
		if (a->ServerName == ServerAddress && a->port == port)
		{
			return a;
		}
	}
	return nullptr;
}

UVrmVMCObject* UVRM4U_VMCSubsystem::CreateServer_NoLock(const FString& ServerAddress, int port)
{
	// MUST run on the game thread: NewObject mutates global UObject tables,
	// TStrongObjectPtr registers a GC root, and CreateServer opens a UDP socket
	// + ticker - none safe off the game thread.
	const int ind = VMCObjectList.AddDefaulted();
	VMCObjectList[ind].Reset(NewObject<UVrmVMCObject>());
	UVrmVMCObject* Obj = VMCObjectList[ind].Get();
	if (Obj)
	{
		Obj->CreateServer(ServerAddress, port);
	}
	return Obj;
}

bool UVRM4U_VMCSubsystem::CopyVMCData(FVMCData& data, FString ServerAddress, int port)
{
	// Hold the list lock for the whole copy so the UVrmVMCObject cannot be
	// removed/destroyed mid-read. Lock order is always list -> object (the OSC
	// receive path takes only the object lock), so no deadlock.
	FScopeLock Lock(&VMCObjectListLock);
	if (UVrmVMCObject* a = FindServer_NoLock(ServerAddress, port))
	{
		a->CopyVMCData(data);
		return true;
	}
	return false;
}

const FVMCData* UVRM4U_VMCSubsystem::GetCachedVMCData(const FString& ServerAddress, int Port, FVMCData& OffThreadFallback)
{
	if (!IsInGameThread())
	{
		return CopyVMCData(OffThreadFallback, ServerAddress, Port) ? &OffThreadFallback : nullptr;
	}

	const uint64 Frame = GFrameCounter;
	if (bCachedVMCValid && CachedVMCFrame == Frame && CachedVMCPort == Port && CachedVMCAddress == ServerAddress)
	{
		return &CachedVMCSnapshot;
	}

	if (CopyVMCData(CachedVMCSnapshot, ServerAddress, Port) == false)
	{
		bCachedVMCValid = false;
		return nullptr;
	}
	CachedVMCAddress = ServerAddress;
	CachedVMCPort = Port;
	CachedVMCFrame = Frame;
	bCachedVMCValid = true;
	return &CachedVMCSnapshot;
}

bool UVRM4U_VMCSubsystem::GetVMCData(TMap<FString, FTransform>& BoneData, TMap<FString, float>& CurveData,
                                     FString ServerAddress, int port)
{
	FVMCData Fallback;
	const FVMCData* Snapshot = GetCachedVMCData(ServerAddress, port, Fallback);
	if (Snapshot == nullptr)
	{
		return false;
	}

	BoneData = Snapshot->BoneData;
	CurveData = Snapshot->CurveData;

	return true;
}

bool UVRM4U_VMCSubsystem::GetVMCBoneTransform(const FString ServerAddress, int Port, FName BoneName,
                                              FTransform& OutTransform)
{
	FVMCData Fallback;
	const FVMCData* Snapshot = GetCachedVMCData(ServerAddress, Port, Fallback);
	if (Snapshot == nullptr)
	{
		return false;
	}

	const FString Key = BoneName.ToString();
	if (const FTransform* Found = FindCaseInsensitive(Snapshot->BoneData, Key))
	{
		OutTransform = *Found;
		return true;
	}
	return false;
}

bool UVRM4U_VMCSubsystem::GetVMCBoneTranslation(const FString ServerAddress, int Port, FName BoneName,
                                                FVector& OutTranslation)
{
	FTransform T;
	if (GetVMCBoneTransform(ServerAddress, Port, BoneName, T) == false)
	{
		return false;
	}
	OutTranslation = T.GetTranslation();
	return true;
}

bool UVRM4U_VMCSubsystem::GetVMCBoneRotation(const FString ServerAddress, int Port, FName BoneName,
                                             FRotator& OutRotation)
{
	FTransform T;
	if (GetVMCBoneTransform(ServerAddress, Port, BoneName, T) == false)
	{
		return false;
	}
	OutRotation = T.GetRotation().Rotator();
	return true;
}

bool UVRM4U_VMCSubsystem::GetVMCBoneScale(const FString ServerAddress, int Port, FName BoneName, FVector& OutScale)
{
	FTransform T;
	if (GetVMCBoneTransform(ServerAddress, Port, BoneName, T) == false)
	{
		return false;
	}
	OutScale = T.GetScale3D();
	return true;
}

TArray<FName> UVRM4U_VMCSubsystem::GetVMCBoneNames(const FString ServerAddress, int Port)
{
	TArray<FName> Result;
	FVMCData Fallback;
	const FVMCData* Snapshot = GetCachedVMCData(ServerAddress, Port, Fallback);
	if (Snapshot == nullptr)
	{
		return Result;
	}

	Result.Reserve(Snapshot->BoneData.Num());
	for (const auto& Pair : Snapshot->BoneData)
	{
		Result.Add(FName(*Pair.Key));
	}
	return Result;
}

TMap<FName, FTransform> UVRM4U_VMCSubsystem::GetVMCAllBoneTransforms(const FString ServerAddress, int Port)
{
	TMap<FName, FTransform> Result;
	FVMCData Fallback;
	const FVMCData* Snapshot = GetCachedVMCData(ServerAddress, Port, Fallback);
	if (Snapshot == nullptr)
	{
		return Result;
	}

	Result.Reserve(Snapshot->BoneData.Num());
	for (const auto& Pair : Snapshot->BoneData)
	{
		Result.Add(FName(*Pair.Key), Pair.Value);
	}
	return Result;
}

bool UVRM4U_VMCSubsystem::GetVMCCurveValue(const FString ServerAddress, int Port, FName CurveName, float& OutValue)
{
	FVMCData Fallback;
	const FVMCData* Snapshot = GetCachedVMCData(ServerAddress, Port, Fallback);
	if (Snapshot == nullptr)
	{
		return false;
	}

	const FString Key = CurveName.ToString();
	if (const float* Found = FindCaseInsensitive(Snapshot->CurveData, Key))
	{
		OutValue = *Found;
		return true;
	}
	return false;
}

TMap<FName, float> UVRM4U_VMCSubsystem::GetVMCAllCurveValues(const FString ServerAddress, int Port)
{
	TMap<FName, float> Result;
	FVMCData Fallback;
	const FVMCData* Snapshot = GetCachedVMCData(ServerAddress, Port, Fallback);
	if (Snapshot == nullptr)
	{
		return Result;
	}

	Result.Reserve(Snapshot->CurveData.Num());
	for (const auto& Pair : Snapshot->CurveData)
	{
		Result.Add(FName(*Pair.Key), Pair.Value);
	}
	return Result;
}

FVector UVRM4U_VMCSubsystem::GetVMCRootTranslation(const FString ServerAddress, int Port)
{
	FVMCData Fallback;
	const FVMCData* Snapshot = GetCachedVMCData(ServerAddress, Port, Fallback);
	if (Snapshot == nullptr)
	{
		return FVector::ZeroVector;
	}

	static const FString RootKey(TEXT("root"));
	if (const FTransform* Found = FindCaseInsensitive(Snapshot->BoneData, RootKey))
	{
		return Found->GetTranslation();
	}
	return FVector::ZeroVector;
}

TArray<FString> UVRM4U_VMCSubsystem::GetActiveVMCServers()
{
	FScopeLock Lock(&VMCObjectListLock);
	TArray<FString> Result;
	Result.Reserve(VMCObjectList.Num());
	for (int i = 0; i < VMCObjectList.Num(); ++i)
	{
		auto a = VMCObjectList[i].Get();
		if (a == nullptr) continue;
		Result.Add(FString::Printf(TEXT("%s:%d"), *a->ServerName, a->port));
	}
	return Result;
}

bool UVRM4U_VMCSubsystem::IsVMCServerActive(const FString ServerAddress, int Port)
{
	FScopeLock Lock(&VMCObjectListLock);
	return FindServer_NoLock(ServerAddress, Port) != nullptr;
}


UVrmVMCObject* UVRM4U_VMCSubsystem::FindOrAddServer(const FString ServerAddress, int port)
{
	{
		FScopeLock Lock(&VMCObjectListLock);
		if (UVrmVMCObject* Existing = FindServer_NoLock(ServerAddress, port))
		{
			return Existing;
		}
		if (IsInGameThread())
		{
			return CreateServer_NoLock(ServerAddress, port);
		}
		// Off the game thread (e.g. parallel anim init): defer creation so
		// NewObject / TStrongObjectPtr / socket setup never run off-thread,
		// which is the prime cause of the GC-time crash. Dedup by endpoint so
		// repeated lookups before the task runs don't enqueue duplicates. The
		// caller gets nullptr this frame and picks up the server on a later
		// CopyVMCData once the game-thread task has created it.
		const FString Key = FString::Printf(TEXT("%s:%d"), *ServerAddress, port);
		if (PendingServerKeys.Contains(Key))
		{
			return nullptr;
		}
		PendingServerKeys.Add(Key);
	}

	const FString AddrCopy = ServerAddress;
	const int PortCopy = port;
	TWeakObjectPtr<UVRM4U_VMCSubsystem> WeakThis(this);
	AsyncTask(ENamedThreads::GameThread, [WeakThis, AddrCopy, PortCopy]()
	{
		UVRM4U_VMCSubsystem* Self = WeakThis.Get();
		if (Self == nullptr)
		{
			return; // subsystem torn down (PIE stop / shutdown) before this ran
		}
		FScopeLock Lock(&Self->VMCObjectListLock);
		Self->PendingServerKeys.Remove(FString::Printf(TEXT("%s:%d"), *AddrCopy, PortCopy));
		if (Self->FindServer_NoLock(AddrCopy, PortCopy) == nullptr)
		{
			Self->CreateServer_NoLock(AddrCopy, PortCopy);
		}
	});
	return nullptr;
}

void UVRM4U_VMCSubsystem::DestroyVMCServer(const FString ServerAddress, int port)
{
	FScopeLock Lock(&VMCObjectListLock);
	for (int i = 0; i < VMCObjectList.Num(); ++i)
	{
		auto a = VMCObjectList[i].Get();
		if (a == nullptr) continue;

		if (a->ServerName == ServerAddress && a->port == port)
		{
			a->DestroyServer();
			VMCObjectList[i].Reset(nullptr);
			VMCObjectList.RemoveAt(i);
			return;
		}
	}
}

void UVRM4U_VMCSubsystem::DestroyVMCServerAll()
{
	FScopeLock Lock(&VMCObjectListLock);
	while (VMCObjectList.Num())
	{
		auto a = VMCObjectList[0].Get();
		if (a)
		{
			a->DestroyServer();
		}
		VMCObjectList[0].Reset(nullptr);
		VMCObjectList.RemoveAt(0);
	}
}


bool UVRM4U_VMCSubsystem::CreateVMCServer(const FString ServerAddress, int port)
{
	return (FindOrAddServer(ServerAddress, port) != nullptr);
}

void UVRM4U_VMCSubsystem::ClearData(const FString ServerAddress, int port)
{
	FScopeLock Lock(&VMCObjectListLock);
	if (UVrmVMCObject* a = FindServer_NoLock(ServerAddress, port))
	{
		a->ClearVMCData();
	}
}

void UVRM4U_VMCSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

#if WITH_EDITOR
	// Store the handle so Deinitialize can remove it; otherwise a duplicate
	// binding accumulates every time the module/engine subsystem reloads.
	BeginStandaloneLocalPlayHandle = FEditorDelegates::BeginStandaloneLocalPlay.AddLambda(
		[this](const uint32 processID)
		{
			this->DestroyVMCServerAll();
		});
#endif
}

void UVRM4U_VMCSubsystem::Deinitialize()
{
#if WITH_EDITOR
	if (BeginStandaloneLocalPlayHandle.IsValid())
	{
		FEditorDelegates::BeginStandaloneLocalPlay.Remove(BeginStandaloneLocalPlayHandle);
		BeginStandaloneLocalPlayHandle.Reset();
	}
#endif
	// Face-bridge LiveLink sources reference this subsystem from their Update();
	// remove them from the client before the servers they poll go away.
	StopAllVMCFaceLiveLink();

	// Tear down all servers (sockets, tickers, UObjects) so they do not leak
	// across PIE sessions or engine-subsystem teardown.
	DestroyVMCServerAll();

	Super::Deinitialize();
}

bool UVRM4U_VMCSubsystem::StartVMCFaceLiveLink(FName SubjectName, const FString ServerAddress, int Port)
{
	if (SubjectName.IsNone() || !IsInGameThread())
	{
		return false;
	}
	if (!VrmVMCFaceLiveLink::IsLiveLinkClientAvailable())
	{
		UE_LOG(LogVRM4UCapture, Warning,
			TEXT("[FaceLiveLink] Cannot publish subject '%s': the LiveLink plugin is not enabled in this project. Enable the engine-bundled 'Live Link' plugin (Edit > Plugins) to use the VMC face bridge."),
			*SubjectName.ToString());
		return false;
	}
	ILiveLinkClient& Client = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);

	if (const FFaceLiveLinkEntry* Existing = FaceLiveLinkSources.Find(SubjectName))
	{
		TSharedPtr<FVrmVMCLiveLinkSource> Source = Existing->Source.Pin();
		if (Source.IsValid() && Source->IsSourceStillValid() &&
			Source->GetServerAddress() == ServerAddress && Source->GetPort() == Port)
		{
			return true; // Already publishing this subject from this endpoint.
		}
		// Endpoint changed or the source died (e.g. removed in the LiveLink UI): rebuild.
		Client.RemoveSource(Existing->SourceGuid);
		FaceLiveLinkSources.Remove(SubjectName);
	}

	// Ensure the OSC endpoint exists; game thread, so creation is synchronous.
	FindOrAddServer(ServerAddress, Port);

	TSharedPtr<FVrmVMCLiveLinkSource> Source = MakeShared<FVrmVMCLiveLinkSource>(SubjectName, ServerAddress, Port);
	FFaceLiveLinkEntry Entry;
	Entry.SourceGuid = Client.AddSource(Source);
	Entry.Source = Source;
	FaceLiveLinkSources.Add(SubjectName, Entry);
	return true;
}

void UVRM4U_VMCSubsystem::StopVMCFaceLiveLink(FName SubjectName)
{
	FFaceLiveLinkEntry Entry;
	if (!FaceLiveLinkSources.RemoveAndCopyValue(SubjectName, Entry))
	{
		return;
	}
	if (VrmVMCFaceLiveLink::IsLiveLinkClientAvailable())
	{
		IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName)
			.RemoveSource(Entry.SourceGuid);
	}
}

bool UVRM4U_VMCSubsystem::IsVMCFaceLiveLinkActive(FName SubjectName) const
{
	const FFaceLiveLinkEntry* Entry = FaceLiveLinkSources.Find(SubjectName);
	if (Entry == nullptr)
	{
		return false;
	}
	const TSharedPtr<FVrmVMCLiveLinkSource> Source = Entry->Source.Pin();
	return Source.IsValid() && Source->IsSourceStillValid();
}

void UVRM4U_VMCSubsystem::StopAllVMCFaceLiveLink()
{
	if (FaceLiveLinkSources.Num() == 0)
	{
		return;
	}
	if (VrmVMCFaceLiveLink::IsLiveLinkClientAvailable())
	{
		ILiveLinkClient& Client = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
		for (const TPair<FName, FFaceLiveLinkEntry>& Pair : FaceLiveLinkSources)
		{
			Client.RemoveSource(Pair.Value.SourceGuid);
		}
	}
	FaceLiveLinkSources.Empty();
}

TArray<USkeletalMeshComponent*> UVRM4U_VMCSubsystem::GetRigsForServer(const FString ServerAddress, int Port)
{
	// Prune entries left by a destroyed PIE session first, so a stale duplicate
	// rig doesn't appear in the dropdown until the next GC.
	FOWLVrmVMCNodeRegistry::SweepStaleEntries();

	TArray<USkeletalMeshComponent*> Result;
	FOWLVrmVMCNodeRegistry::ForEach([&](USkeletalMeshComponent* Comp, const FOWLVrmVMCRegistryEntry& Entry)
	{
		if (!Entry.IsValid()) return;            // dead anim instance
		if (!IsValid(Comp)) return;              // dead / pending-kill component
		if (Entry.WeakComponent.Get() != Comp) return; // stale / recycled key
		if (Entry.Node->ServerAddress == ServerAddress && Entry.Node->Port == Port)
		{
			Result.Add(Comp);
		}
	});
	return Result;
}

bool UVRM4U_VMCSubsystem::GetServerSecondsSinceLastPacket(const FString ServerAddress, int Port, float& OutSeconds)
{
	OutSeconds = 0.0f;
	FScopeLock Lock(&VMCObjectListLock);
	for (int i = 0; i < VMCObjectList.Num(); ++i)
	{
		UVrmVMCObject* Server = VMCObjectList[i].Get();
		if (Server == nullptr) continue;
		if (Server->ServerName != ServerAddress || Server->port != Port) continue;

		if (Server->LastPacketReceivedTime <= 0.0)
		{
			OutSeconds = -1.0f;
			return true;
		}

		const double Now = FPlatformTime::Seconds();
		OutSeconds = static_cast<float>(Now - Server->LastPacketReceivedTime);
		return true;
	}
	return false;
}