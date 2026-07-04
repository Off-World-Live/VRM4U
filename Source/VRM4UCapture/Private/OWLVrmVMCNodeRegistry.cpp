#include "OWLVrmVMCNodeRegistry.h"

#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "UObject/UObjectGlobals.h"

FCriticalSection FOWLVrmVMCNodeRegistry::RegistryLock;
TMap<USkeletalMeshComponent*, FOWLVrmVMCRegistryEntry> FOWLVrmVMCNodeRegistry::ComponentToEntry;
FDelegateHandle FOWLVrmVMCNodeRegistry::PreGCHandle;

void FOWLVrmVMCNodeRegistry::Initialize()
{
	if (!PreGCHandle.IsValid())
	{
		PreGCHandle = FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddStatic(
			&FOWLVrmVMCNodeRegistry::SweepStaleEntries);
	}
}

void FOWLVrmVMCNodeRegistry::Shutdown()
{
	if (PreGCHandle.IsValid())
	{
		FCoreUObjectDelegates::GetPreGarbageCollectDelegate().Remove(PreGCHandle);
		PreGCHandle.Reset();
	}

	FScopeLock Lock(&RegistryLock);
	ComponentToEntry.Empty();
}

void FOWLVrmVMCNodeRegistry::SweepStaleEntries()
{
	FScopeLock Lock(&RegistryLock);
	for (auto It = ComponentToEntry.CreateIterator(); It; ++It)
	{
		if (!It.Value().WeakComponent.IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

void FOWLVrmVMCNodeRegistry::Register(USkeletalMeshComponent* Component, UAnimInstance* AnimInstance, FAnimNode_VrmVMC* Node)
{
	if (Component == nullptr || AnimInstance == nullptr || Node == nullptr)
	{
		return;
	}

	FScopeLock Lock(&RegistryLock);
	FOWLVrmVMCRegistryEntry Entry;
	Entry.WeakInstance = AnimInstance;
	Entry.WeakComponent = Component;
	Entry.Node = Node;
	ComponentToEntry.Add(Component, Entry);
}

void FOWLVrmVMCNodeRegistry::Unregister(FAnimNode_VrmVMC* Node)
{
	if (Node == nullptr)
	{
		return;
	}

	FScopeLock Lock(&RegistryLock);

	// Reverse lookup: the destructor only has its own pointer, not the
	// component it registered against. Components may have been GC'd already
	// so iterating the map by value is the safe path.
	for (auto It = ComponentToEntry.CreateIterator(); It; ++It)
	{
		if (It.Value().Node == Node)
		{
			It.RemoveCurrent();
			break;
		}
	}
}

FOWLVrmVMCRegistryEntry FOWLVrmVMCNodeRegistry::Find(USkeletalMeshComponent* Component)
{
	if (Component == nullptr)
	{
		return FOWLVrmVMCRegistryEntry{};
	}

	FScopeLock Lock(&RegistryLock);
	if (FOWLVrmVMCRegistryEntry* Found = ComponentToEntry.Find(Component))
	{
		// Reject a stale/reused key: the raw key matched, but if the live
		// component the entry was registered against is gone (or an unrelated
		// object now occupies that address), do not hand back a dead entry.
		if (Found->WeakComponent.Get() != Component)
		{
			ComponentToEntry.Remove(Component);
			return FOWLVrmVMCRegistryEntry{};
		}
		return *Found;
	}
	return FOWLVrmVMCRegistryEntry{};
}

void FOWLVrmVMCNodeRegistry::ForEach(TFunctionRef<void(USkeletalMeshComponent*, const FOWLVrmVMCRegistryEntry&)> Func)
{
	FScopeLock Lock(&RegistryLock);
	for (const auto& Pair : ComponentToEntry)
	{
		Func(Pair.Key, Pair.Value);
	}
}