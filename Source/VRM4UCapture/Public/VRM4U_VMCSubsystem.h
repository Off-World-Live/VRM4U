// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "Misc/EngineVersionComparison.h"
#include "UObject/StrongObjectPtr.h"
#include "HAL/CriticalSection.h"

#include "OSCServer.h"
#include "VrmVMCObject.h"

#include "VRM4U_VMCSubsystem.generated.h"


#if	UE_VERSION_OLDER_THAN(4, 22, 0)

//Couldn't find parent type for 'VRM4U_AnimSubsystem' named 'UEngineSubsystem'
#error "please remove VRM4U_AnimSubsystem.h/cpp  for <=UE4.21"

#endif


UCLASS()
class VRM4UCAPTURE_API UVRM4U_VMCSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = VRM4U)
	bool CreateVMCServer(const FString ServerAddress, int port);

	void ClearData(const FString ServerAddress, int port);

	UFUNCTION(BlueprintCallable, Category = VRM4U)
	void DestroyVMCServer(const FString ServerAddress, int port);

	UFUNCTION(BlueprintCallable, Category = VRM4U)
	void DestroyVMCServerAll();

	UVrmVMCObject* FindOrAddServer(const FString ServerAddress, int port);

	TArray<TStrongObjectPtr<UVrmVMCObject>> VMCObjectList;

	bool CopyVMCData(FVMCData& DstData, FString ServerAddress, int port);

	UFUNCTION(BlueprintCallable, Category = VRM4U)
	bool GetVMCData(TMap<FString, FTransform>& BoneData, TMap<FString, float>& CurveData, FString ServerAddress,
	                int port);

	// Per-bone read access. Case-insensitive lookup on humanoid bone keys.
	UFUNCTION(BlueprintCallable, Category = "VRM4U|VMC")
	bool GetVMCBoneTransform(const FString ServerAddress, int Port, FName BoneName, FTransform& OutTransform);

	UFUNCTION(BlueprintCallable, Category = "VRM4U|VMC")
	bool GetVMCBoneTranslation(const FString ServerAddress, int Port, FName BoneName, FVector& OutTranslation);

	UFUNCTION(BlueprintCallable, Category = "VRM4U|VMC")
	bool GetVMCBoneRotation(const FString ServerAddress, int Port, FName BoneName, FRotator& OutRotation);

	UFUNCTION(BlueprintCallable, Category = "VRM4U|VMC")
	bool GetVMCBoneScale(const FString ServerAddress, int Port, FName BoneName, FVector& OutScale);

	// Bulk read access. Returns snapshot copies, not live references.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC")
	TArray<FName> GetVMCBoneNames(const FString ServerAddress, int Port);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC")
	TMap<FName, FTransform> GetVMCAllBoneTransforms(const FString ServerAddress, int Port);

	// Blendshape curves.
	UFUNCTION(BlueprintCallable, Category = "VRM4U|VMC")
	bool GetVMCCurveValue(const FString ServerAddress, int Port, FName CurveName, float& OutValue);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC")
	TMap<FName, float> GetVMCAllCurveValues(const FString ServerAddress, int Port);

	// Root translation lives in BoneData under the "root" key. Returns zero vector if missing.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC")
	FVector GetVMCRootTranslation(const FString ServerAddress, int Port);

	// Server discovery. Returned strings are formatted "address:port".
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC")
	TArray<FString> GetActiveVMCServers();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC")
	bool IsVMCServerActive(const FString ServerAddress, int Port);

	// Reverse lookup: which active rigs are bound to a given server endpoint.
	// Pairs with UVrmVMCBlueprintLibrary::GetRigServer for the per-rig query.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC")
	TArray<USkeletalMeshComponent*> GetRigsForServer(const FString ServerAddress, int Port);

	// Seconds since the server last received a VMC packet. Returns false if
	// the endpoint is not active. Returns true with OutSeconds=-1.0 if the
	// server is active but has never received a packet. Used by the debug
	// panel's connection status indicator.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC")
	bool GetServerSecondsSinceLastPacket(const FString ServerAddress, int Port, float& OutSeconds);

	// -- VMC -> LiveLink ARKit face bridge (see VrmVMCFaceLiveLink.h) --
	// Game-thread only. Publishes the endpoint's blendshape curves as a LiveLink
	// basic-role subject with the ARKit-52(+9) schema. Returns false (with one log
	// line) when the engine's LiveLink plugin is disabled — everything else in
	// VRM4U works without it. Creates the VMC server if it does not exist yet.
	UFUNCTION(BlueprintCallable, Category = "VRM4U|VMC")
	bool StartVMCFaceLiveLink(FName SubjectName, const FString ServerAddress, int Port);

	// Stops publishing and removes the subject. Leaves the VMC server running (the
	// body anim node may share it).
	UFUNCTION(BlueprintCallable, Category = "VRM4U|VMC")
	void StopVMCFaceLiveLink(FName SubjectName);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "VRM4U|VMC")
	bool IsVMCFaceLiveLinkActive(FName SubjectName) const;


	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	// Guards every access to VMCObjectList. The list is read on the animation
	// worker thread (CopyVMCData via the anim node's EvaluateSkeletalControl)
	// and mutated on the game thread (FindOrAddServer / DestroyVMCServer), so
	// all access must be serialized to avoid torn TStrongObjectPtr reads and
	// use-after-free of the UVrmVMCObject during AddDefaulted reallocation /
	// RemoveAt shifting.
	mutable FCriticalSection VMCObjectListLock;

	// "address:port" keys for servers whose creation has been deferred to the
	// game thread (when FindOrAddServer is called off the game thread). Prevents
	// enqueuing duplicate create tasks for the same endpoint. Guarded by VMCObjectListLock.
	TSet<FString> PendingServerKeys;

	// Handle for the BeginStandaloneLocalPlay teardown delegate, removed on Deinitialize.
	FDelegateHandle BeginStandaloneLocalPlayHandle;

	// Internal helpers assume VMCObjectListLock is already held by the caller.
	UVrmVMCObject* FindServer_NoLock(const FString& ServerAddress, int port) const;
	UVrmVMCObject* CreateServer_NoLock(const FString& ServerAddress, int port);

	// Per-frame snapshot for the read-only BP getters: on the game thread they
	// share one deep copy per endpoint per engine frame instead of copying both
	// VMC maps on every call (an N-bone query was N full copies). Off the game
	// thread the getter falls back to OffThreadFallback so the cache members stay
	// single-threaded. Game-thread only.
	const FVMCData* GetCachedVMCData(const FString& ServerAddress, int Port, FVMCData& OffThreadFallback);
	FVMCData CachedVMCSnapshot;
	FString CachedVMCAddress;
	int CachedVMCPort = -1;
	uint64 CachedVMCFrame = 0;
	bool bCachedVMCValid = false;

	// Active face-bridge LiveLink sources by subject name. The LiveLink client owns
	// the sources; we keep the guid for removal and a weak ptr for status queries.
	// Game-thread only (Start/Stop are game-thread entry points), so no lock.
	struct FFaceLiveLinkEntry
	{
		FGuid SourceGuid;
		TWeakPtr<class FVrmVMCLiveLinkSource> Source;
	};
	TMap<FName, FFaceLiveLinkEntry> FaceLiveLinkSources;

	void StopAllVMCFaceLiveLink();
};
