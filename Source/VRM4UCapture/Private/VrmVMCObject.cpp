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

	// Clear all data when server is destroyed
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

	OSCServer->OnOscMessageReceivedNative.RemoveAll(nullptr);
	OSCServer->OnOscMessageReceivedNative.AddUObject(this, &UVrmVMCObject::OSCReceivedMessageEvent);

#if WITH_EDITOR
	OSCServer->SetTickInEditor(true);
#endif // WITH_EDITOR
#endif
}

void UVrmVMCObject::OSCReceivedMessageEvent(const FOSCMessage& Message, const FString& IPAddress, uint16 Port)
{
	FOSCAddress a = UOSCManager::GetOSCMessageAddress(Message);
	FString addressPath = UOSCManager::GetOSCAddressFullPath(a);

	// Early exit for non-VMC messages - major performance improvement
	if (!addressPath.StartsWith(TEXT("/VMC/Ext/")))
	{
		return;
	}

	// Process bone/root position messages
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

			// Store in buffer instead of immediate processing
			BoneDataBuffer.FindOrAdd(str[0]) = t;
			bPendingUpdate = true;
		}
	}
	// Process blend shape values
	else if (addressPath == TEXT("/VMC/Ext/Blend/Val"))
	{
		TArray<FString> str;
		TArray<float> curve;
		UOSCManager::GetAllStrings(Message, str);
		UOSCManager::GetAllFloats(Message, curve);

		if (str.Num() > 0 && curve.Num() >= 1)
		{
			// Store in buffer instead of immediate processing
			CurveDataBuffer.FindOrAdd(str[0]) = curve[0];
			bPendingUpdate = true;
		}
	}
	// Frame completion messages - these trigger immediate updates
	else if (addressPath == TEXT("/VMC/Ext/OK") ||
		addressPath == TEXT("/VMC/Ext/T") ||
		addressPath == TEXT("/VMC/Ext/Blend/Apply"))
	{
		// Force flush all buffered data on frame completion
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
    
	// Handle adaptive throttling if enabled
	if (bAdaptiveThrottling && !bForceFlush) {
		static float LastFrameTime = 0.0f;
		float FrameDelta = CurrentTime - LastFrameTime;
		LastFrameTime = CurrentTime;
        
		// Adaptive range: 30 FPS (slow) to 90 FPS (fast)
		if (FrameDelta > 1.0f / 30.0f) {
			CurrentThrottleTime = 1.0f / 30.0f;  // System struggling
		} else if (FrameDelta < 1.0f / 60.0f) {
			CurrentThrottleTime = 1.0f / 90.0f;  // System performing well
		} else {
			CurrentThrottleTime = UpdateThrottleTime; // Use base rate
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

		// Batch update bone data
		for (const auto& BonePair : BoneDataBuffer)
		{
			VMCData.BoneData.FindOrAdd(BonePair.Key) = BonePair.Value;
		}

		// Batch update curve data
		for (const auto& CurvePair : CurveDataBuffer)
		{
			VMCData.CurveData.FindOrAdd(CurvePair.Key) = CurvePair.Value;
		}

		// Update cache - this is the expensive operation, so do it less frequently
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
	// Handle any pending buffered updates that haven't been flushed by frame completion messages
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

	// Also clear buffers
	BoneDataBuffer.Empty();
	CurveDataBuffer.Empty();
	bPendingUpdate = false;
}
