// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Misc/EngineVersionComparison.h"
#include "UObject/StrongObjectPtr.h"
#include "OSCServer.h"  // for game build link error
#include "Engine/EngineTypes.h"  // For FTickableGameObject

#include "VrmVMCObject.generated.h"

struct FOSCMessage;

struct FVMCData {
    FString ServerAddress = "";
    int Port = 0;

    TMap<FString, FTransform> BoneData;
    TMap<FString, float> CurveData;

    void ClearData() {
       BoneData.Empty();
       CurveData.Empty();
    }

    bool operator==(const FVMCData& Other) const {
       if (Port != Other.Port) return false;
       if (ServerAddress != Other.ServerAddress) return false;
       return true;
    }
};

UCLASS()
class VRM4UCAPTURE_API UVrmVMCObject : public UObject, public FTickableGameObject
{
    GENERATED_BODY()

private:
    FCriticalSection cs;
    TStrongObjectPtr<UOSCServer> OSCServer;

    FVMCData VMCData;
    FVMCData VMCData_Cache;

    bool bDataUpdated = false;

    // Performance optimization members
    float LastUpdateTime = 0.0f;
    TMap<FString, FTransform> BoneDataBuffer;
    TMap<FString, float> CurveDataBuffer;
    bool bPendingUpdate = false;

    // Helper method to flush buffered data
    void FlushBufferedData(bool bForceFlush);

public:
    FString ServerName;
    uint16 port;
    bool bForceUpdate = false;

    // Simplified performance settings - set by AnimNode
    float UpdateThrottleTime = 1.0f / 60.0f;
    bool bAdaptiveThrottling = false;

    void CreateServer(FString name, uint16 port);
    void DestroyServer();
    void OSCReceivedMessageEvent(const FOSCMessage& Message, const FString& IPAddress, uint16 Port);

    bool CopyVMCData(FVMCData& dst);
    void ClearVMCData();

    // FTickableGameObject interface
    virtual void Tick(float DeltaTime) override;
    virtual bool IsTickable() const override { return OSCServer.IsValid(); }
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UVrmVMCObject, STATGROUP_Tickables); }
};
