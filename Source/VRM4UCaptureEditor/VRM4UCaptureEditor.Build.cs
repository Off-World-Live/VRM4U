// VRM4U Copyright (c) 2021-2026 Haruyoshi Yamamoto. This software is released under the MIT License.

using UnrealBuildTool;

public class VRM4UCaptureEditor : ModuleRules
{
	public VRM4UCaptureEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",

				"AnimGraphRuntime",
				"AnimGraph",
				"BlueprintGraph",

				"UnrealEd",
				"AnimationEditMode",
				"Persona",

				"VRM4U",
				"VRM4UCapture",
				"OSC",
				"EditorStyle",

				// dependencies for the editor extension
				"Slate",
				"SlateCore",
				"ContentBrowser",
				"ApplicationCore",
				"InputCore",
				"AssetRegistry",
				"AssetTools",
				"PropertyEditor",
				"EditorFramework",
				"ToolMenus",
				"AppFramework",
				"WorkspaceMenuStructure",
				
				"Json",
				"JsonUtilities",

				"LevelEditor",
				"TypedElementFramework",
				"TypedElementRuntime",
			});

		// Retarget setup wizard (VrmRetargetSetupUtil) drives the IK Rig / IK Retargeter
		// controller API; UE5 only, same guard as the other VRM4U modules.
		BuildVersion Version;
		if (BuildVersion.TryRead(BuildVersion.GetDefaultFileName(), out Version))
		{
			if (Version.MajorVersion == 5)
			{
				PrivateDependencyModuleNames.Add("IKRig");
				PrivateDependencyModuleNames.Add("IKRigEditor");
			}
		}

		PrivateIncludePathModuleNames.AddRange(
			new string[]
			{
				// Include paths only
			});

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			});

		PrivateIncludePaths.AddRange(
			new string[]
			{
				//"../Runtime/Renderer/Private",
			});
	}
}