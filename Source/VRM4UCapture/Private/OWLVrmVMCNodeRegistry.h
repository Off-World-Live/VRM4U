#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UAnimInstance;
class USkeletalMeshComponent;
struct FAnimNode_VrmVMC;

/**
 * Registry entry returned by FOWLVrmVMCNodeRegistry::Find. The weak anim
 * instance pointer gates lifetime: callers must verify WeakInstance.Get() is
 * non-null before dereferencing Node. The raw Node pointer is owned by the
 * anim instance and remains valid as long as the anim instance is alive.
 */
struct FOWLVrmVMCRegistryEntry
{
	TWeakObjectPtr<UAnimInstance> WeakInstance;
	// Weak ref to the component this entry was registered against. The map is
	// keyed by a raw component pointer that GC never nulls. This weak ref lets
	// Find() reject a stale/reused key (an address recycled by a different
	// object) instead of returning an entry bound to a dead component.
	TWeakObjectPtr<USkeletalMeshComponent> WeakComponent;
	FAnimNode_VrmVMC* Node = nullptr;

	bool IsValid() const
	{
		return Node != nullptr && WeakInstance.IsValid();
	}
};

/**
 * Module-internal registry mapping skeletal mesh components to their active
 * VMC anim node instance. Populated by FAnimNode_VrmVMC::Initialize_AnyThread,
 * cleared by the anim node destructor.
 *
 * The BP library uses Find to resolve a USkeletalMeshComponent reference into
 * a registry entry, then verifies the entry's weak anim instance pointer is
 * still alive before dereferencing the raw anim node pointer.
 *
 * Lifetime contract: the raw FAnimNode_VrmVMC* in the returned entry is owned
 * by the UAnimInstance referenced by WeakInstance. As long as the weak pointer
 * resolves to a live UObject, the anim node pointer is valid. This closes the
 * use-after-free window that existed when callers held a raw node pointer
 * across actor destruction on the game thread.
 *
 * One node per component is the supported configuration. If multiple anim
 * nodes register against the same component (the MetaHuman double-init case),
 * the last registration wins.
 */
class FOWLVrmVMCNodeRegistry
{
public:
	// Module lifetime hooks. Initialize binds a pre-GC sweep that drops entries
	// whose component has been destroyed. Shutdown unbinds it. This backs up
	// Find()'s per-lookup stale-key rejection: it bounds the map size even for
	// components that are never looked up again.
	static void Initialize();
	static void Shutdown();

	static void Register(USkeletalMeshComponent* Component, UAnimInstance* AnimInstance, FAnimNode_VrmVMC* Node);
	static void Unregister(FAnimNode_VrmVMC* Node);
	static FOWLVrmVMCRegistryEntry Find(USkeletalMeshComponent* Component);

	// Iterates every (Component, Entry) pair under the registry lock. The lock
	// is held for the entire iteration so callbacks must not call back into
	// the registry (would deadlock) and must be cheap (long callbacks block
	// all other registry access). Callbacks receive both the component and
	// the entry so they can apply the IsValid() weak-instance gate themselves.
	static void ForEach(TFunctionRef<void(USkeletalMeshComponent*, const FOWLVrmVMCRegistryEntry&)> Func);

	// Drops every entry whose WeakComponent no longer resolves (component
	// destroyed/GC'd). Bound to the pre-GC delegate by Initialize(), and also
	// callable on demand (e.g. before enumerating rigs) so a stale entry left by
	// a previous PIE session doesn't linger in the UI until the next GC.
	static void SweepStaleEntries();

private:

	static FCriticalSection RegistryLock;
	static TMap<USkeletalMeshComponent*, FOWLVrmVMCRegistryEntry> ComponentToEntry;
	static FDelegateHandle PreGCHandle;
};
