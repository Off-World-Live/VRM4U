// VRM4U Copyright (c) 2021-2026 Haruyoshi Yamamoto. This software is released under the MIT License.

#pragma once

#include "CoreMinimal.h"

/**
 * Adds "VRM4U: Auto-Setup VMC Retarget" to the level-editor actor context menu for actors
 * that carry a retargetable (non-VRM) skeletal mesh. Runs UVrmRetargetSetupUtil and shows
 * the result as an editor notification (full step log in the VRM4UCaptureEditor log).
 */
struct FVrmRetargetSetupMenuExtension
{
	static void Register();
	static void Unregister();
};
