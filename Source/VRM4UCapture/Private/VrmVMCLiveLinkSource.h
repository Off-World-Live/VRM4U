// VRM4U Copyright (c) 2021-2026 Haruyoshi Yamamoto. This software is released under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "ILiveLinkSource.h"

class ILiveLinkClient;

/**
 * LiveLink source that republishes the parsed VMC blendshape map of one VMC server endpoint
 * as an ARKit-52(+9) basic-role subject (see VrmVMCFaceLiveLink.h for the schema contract).
 *
 * Owned by the LiveLink client once added; UVRM4U_VMCSubsystem keeps the (guid, weak ptr)
 * pair for stop/status. Update() runs on the game thread from FLiveLinkClient::Tick and
 * polls the subsystem's thread-safe curve snapshot — no OSC-thread coupling.
 */
class FVrmVMCLiveLinkSource : public ILiveLinkSource
{
public:
	FVrmVMCLiveLinkSource(FName InSubjectName, const FString& InServerAddress, int32 InPort);

	// ILiveLinkSource
	virtual void ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid) override;
	virtual void Update() override;
	virtual bool IsSourceStillValid() const override { return !bShutdown; }
	virtual bool RequestSourceShutdown() override;
	virtual FText GetSourceType() const override;
	virtual FText GetSourceMachineName() const override;
	virtual FText GetSourceStatus() const override;

	FName GetSubjectName() const { return SubjectName; }
	const FString& GetServerAddress() const { return ServerAddress; }
	int32 GetPort() const { return Port; }

private:
	ILiveLinkClient* Client = nullptr;
	FGuid SourceGuid;

	const FName SubjectName;
	const FString ServerAddress;
	const int32 Port;

	bool bShutdown = false;
	bool bStaticDataPushed = false;
	bool bHasReceivedPackets = false;
	TArray<float> ValueScratch;
};
