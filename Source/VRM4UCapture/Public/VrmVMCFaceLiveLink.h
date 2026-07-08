// VRM4U Copyright (c) 2021-2026 Haruyoshi Yamamoto. This software is released under the MIT License.

#pragma once

#include "CoreMinimal.h"

/**
 * VMC face curves -> LiveLink ARKit subject: the pure-mapping half of the face bridge,
 * exposed so automation tests can exercise it without a LiveLink client.
 *
 * The published subject mirrors Epic's Live Link Face source: ULiveLinkBasicRole, static
 * PropertyNames = the EARFaceBlendShape enum names in enum order (52 ARKit blendshapes + 9
 * head/eye rotation curves), frame PropertyValues = one float per name. The stock
 * ABP_MH_LiveLink / ARKit mapping assets consume it unchanged.
 */
namespace VrmVMCFaceLiveLink
{
	/** The 61 ARKit property names in EARFaceBlendShape order (EyeBlinkLeft ... RightEyeRoll). */
	VRM4UCAPTURE_API const TArray<FName>& GetARKitPropertyNames();

	/**
	 * Translate a VMC curve snapshot (as stored by UVRM4U_VMCSubsystem) into the dense ARKit
	 * value array matching GetARKitPropertyNames(). Per incoming key:
	 *  - a leading "BlendShape." prefix is stripped (PerfectSync senders wrap names in it)
	 *  - keys matching an ARKit name pass through 1:1 (FName compare is case-insensitive)
	 *  - classic VRM 0.x / 1.0 expression keys (visemes, Blink*, emotions, Look*) fan out
	 *    through a lossy fallback table
	 *  - everything else is ignored
	 * Collisions on one target keep the max.
	 */
	VRM4UCAPTURE_API void MapVMCCurvesToARKit(const TMap<FName, float>& InCurves, TArray<float>& OutValues);

	/** True when the LiveLink client modular feature is registered (the engine-bundled
	 *  LiveLink plugin is enabled). The bridge no-ops when this is false. */
	VRM4UCAPTURE_API bool IsLiveLinkClientAvailable();
}
