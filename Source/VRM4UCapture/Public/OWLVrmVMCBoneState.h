#pragma once

#include "CoreMinimal.h"

/**
 * Per-bone state held by FAnimNode_VrmVMC. Stores override values written from
 * Blueprint (pre-rebase, post-rebase, mask) and the last-applied rotation cached
 * from the most recent evaluation. Read by VrmVMCBlueprintLibrary through the
 * anim node's accessor methods.
 *
 * Pre-rebase override values are interpreted in VMC source space and feed through
 * the rebase formula like normal VMC data. Post-rebase override values are
 * interpreted in target bone-local space and bypass the rebase formula entirely.
 * Mask flag causes the bone to be skipped regardless of override state.
 */
struct FOWLVMCPerBoneState
{
	FQuat PreRebaseRotation = FQuat::Identity;
	bool bHasPreRebase = false;

	FQuat PostRebaseRotation = FQuat::Identity;
	bool bHasPostRebase = false;

	bool bMasked = false;

	FQuat LastAppliedRotation = FQuat::Identity;
	FTransform LastAppliedTransform = FTransform::Identity;
	bool bHasLastApplied = false;
};

/**
 * Per-curve state held by FAnimNode_VrmVMC. Stores override values written from
 * Blueprint (override value, mask) and the last-applied curve value cached from
 * the most recent evaluation. Read by VrmVMCBlueprintLibrary through the anim
 * node's accessor methods.
 *
 * Override value substitutes the incoming VMC curve value before the morph
 * target lookup. Mask flag causes the curve to be skipped entirely, leaving
 * the upstream curve value intact.
 */
struct FOWLVMCPerCurveState
{
	float OverrideValue = 0.0f;
	bool bHasOverride = false;

	bool bMasked = false;

	float LastAppliedValue = 0.0f;
	bool bHasLastApplied = false;
};