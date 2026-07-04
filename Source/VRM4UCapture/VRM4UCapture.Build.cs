// VRM4U Copyright (c) 2021-2026 Haruyoshi Yamamoto. This software is released under the MIT License.

using UnrealBuildTool;

public class VRM4UCapture : ModuleRules
{
	public VRM4UCapture(ReadOnlyTargetRules Target) : base(Target)
	{
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
                "Engine",
                "Networking",
                "Sockets",
				"OSC",
				"AnimGraphRuntime",

				// Engine-core Runtime module (NOT the LiveLink plugin): types + the
				// modular-feature handle for the VMC->LiveLink face bridge. The actual
				// client is resolved at runtime and its absence is handled gracefully,
				// so projects without the LiveLink plugin lose only the face bridge.
				"LiveLinkInterface",

				"VRM4U",
            });

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}


		PrivateIncludePathModuleNames.AddRange(
			new string[] {
            });

		DynamicallyLoadedModuleNames.AddRange(
			new string[] {
			});

        PrivateIncludePaths.AddRange(
        new string[] {
        });
    }
}
