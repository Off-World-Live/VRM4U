// Fill out your copyright notice in the Description page of Project Settings.


#include "VrmVMCObject.h"
#include "VRM4U_VMCSubsystem.h"
#include "Engine/Engine.h"
#include "UObject/StrongObjectPtr.h"
#include "Misc/ScopeLock.h"
#include "OSCManager.h"
#include "OSCServer.h"

void UVrmVMCObject::DestroyServer()
{
	ServerName = "";
	port = 0;

	if (OSCServer.Get())
	{
		OSCServer->Stop();
	}
	OSCServer.Reset(nullptr);

	{
		FScopeLock lock(&cs);
		VMCData.ClearData();
		VMCData_Cache.ClearData();
	}
	BoneDataBuffer.Empty();
	CurveDataBuffer.Empty();
	bPendingUpdate = false;
}

void UVrmVMCObject::CreateServer(FString inName, uint16 inPort)
{
	ServerName = inName;
	port = inPort;
	LastUpdateTime = FPlatformTime::Seconds();

#if UE_VERSION_OLDER_THAN(4, 25, 0)
#else
	OSCServer.Reset(UOSCManager::CreateOSCServer(ServerName, port, true, true, FString(), this));

	// CreateOSCServer returns null if the port is already bound (e.g. another VMC
	// receiver on the same port). Guard before binding/ticking to avoid a null deref.
	if (OSCServer.IsValid())
	{
		OSCServer->OnOscMessageReceivedNative.RemoveAll(nullptr);
		OSCServer->OnOscMessageReceivedNative.AddUObject(this, &UVrmVMCObject::OSCReceivedMessageEvent);

#if WITH_EDITOR
		OSCServer->SetTickInEditor(true);
#endif // WITH_EDITOR
	}
#endif
}

void UVrmVMCObject::OSCReceivedMessageEvent(const FOSCMessage& Message, const FString& IPAddress, uint16 Port)
{
	FOSCAddress a = UOSCManager::GetOSCMessageAddress(Message);
	FString addressPath = UOSCManager::GetOSCAddressFullPath(a);

	// Early exit for non-VMC messages, before any parsing.
	if (!addressPath.StartsWith(TEXT("/VMC/Ext/")))
	{
		return;
	}

	// Wire-level liveness timestamp, used by debug tooling.
	LastPacketReceivedTime = FPlatformTime::Seconds();

	if (addressPath == TEXT("/VMC/Ext/Root/Pos") || addressPath == TEXT("/VMC/Ext/Bone/Pos"))
	{
		TArray<FString> str;
		TArray<float> curve;
		UOSCManager::GetAllStrings(Message, str);
		UOSCManager::GetAllFloats(Message, curve);

		if (str.Num() > 0 && curve.Num() >= 7)
		{
			FTransform t;
			t.SetLocation(FVector(-curve[0], curve[2], curve[1]) * 100.f);
			t.SetRotation(FQuat(-curve[3], curve[5], curve[4], curve[6]));

			if (curve.Num() >= 10)
			{
				t.SetScale3D(FVector(curve[7], curve[9], curve[8]));
			}

			BoneDataBuffer.FindOrAdd(str[0]) = t;
			bPendingUpdate = true;
		}
	}
	else if (addressPath == TEXT("/VMC/Ext/Blend/Val"))
	{
		TArray<FString> str;
		TArray<float> curve;
		UOSCManager::GetAllStrings(Message, str);
		UOSCManager::GetAllFloats(Message, curve);

		if (str.Num() > 0 && curve.Num() >= 1)
		{
			CurveDataBuffer.FindOrAdd(str[0]) = curve[0];
			bPendingUpdate = true;
		}
	}
	// Frame-completion messages flush the buffer immediately.
	else if (addressPath == TEXT("/VMC/Ext/OK") ||
		addressPath == TEXT("/VMC/Ext/T") ||
		addressPath == TEXT("/VMC/Ext/Blend/Apply"))
	{
		FlushBufferedData(true);
	}
}

void UVrmVMCObject::FlushBufferedData(bool bForceFlush)
{
	if (!bPendingUpdate) {
		return;
	}
	
	float CurrentTime = FPlatformTime::Seconds();
	float CurrentThrottleTime = UpdateThrottleTime;
    
	if (bAdaptiveThrottling && !bForceFlush) {
		static float LastFrameTime = 0.0f;
		float FrameDelta = CurrentTime - LastFrameTime;
		LastFrameTime = CurrentTime;
        
		// Adaptive range: 30 FPS (slow) to 90 FPS (fast)
		if (FrameDelta > ADAPTIVE_THROTTLE_SLOW_TIME) {
			CurrentThrottleTime = ADAPTIVE_THROTTLE_SLOW_TIME;  // System struggling
		} else if (FrameDelta < ADAPTIVE_THROTTLE_FAST_TIME) {
			CurrentThrottleTime = ADAPTIVE_THROTTLE_HIGH_PERFORMANCE_TIME;  // System performing well
		} else {
			CurrentThrottleTime = UpdateThrottleTime;
		}
	}

	// Rate limiting: respect throttle time unless forced
	if (!bForceFlush && (CurrentTime - LastUpdateTime < CurrentThrottleTime))
	{
		return;
	}

	// Minimize lock time by doing bulk operations
	{
		FScopeLock lock(&cs);

		for (const auto& BonePair : BoneDataBuffer)
		{
			VMCData.BoneData.FindOrAdd(BonePair.Key) = BonePair.Value;
		}

		for (const auto& CurvePair : CurveDataBuffer)
		{
			VMCData.CurveData.FindOrAdd(CurvePair.Key) = CurvePair.Value;
		}

		// Refresh the read cache: the expensive copy, so do it less frequently.
		VMCData_Cache = VMCData;
	}

	// Clear buffers and reset state outside of lock
	BoneDataBuffer.Empty();
	CurveDataBuffer.Empty();
	bPendingUpdate = false;
	LastUpdateTime = CurrentTime;
}

void UVrmVMCObject::Tick(float DeltaTime)
{
	// Flush updates not already flushed by a frame-completion message.
	if (bPendingUpdate && bForceUpdate)
	{
		FlushBufferedData(false);
	}
}

bool UVrmVMCObject::CopyVMCData(FVMCData& dst)
{
	FScopeLock lock(&cs);
	dst = VMCData_Cache;
	return true;
}

void UVrmVMCObject::ClearVMCData()
{
	FScopeLock lock(&cs);
	VMCData.ClearData();
	VMCData_Cache.ClearData();

	BoneDataBuffer.Empty();
	CurveDataBuffer.Empty();
	bPendingUpdate = false;
}
