// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MCPTest : ModuleRules
{
	public MCPTest(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"SlateCore",
			"DeveloperSettings",
			"Sockets",
			"Networking"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { "AnimationCore", "Niagara" });

		PublicIncludePaths.AddRange(new string[] {
			"MCPTest",
			"MCPTest/Variant_Platforming",
			"MCPTest/Variant_Platforming/Animation",
			"MCPTest/Variant_Combat",
			"MCPTest/Variant_Combat/AI",
			"MCPTest/Variant_Combat/Animation",
			"MCPTest/Variant_Combat/Gameplay",
			"MCPTest/Variant_Combat/Interfaces",
			"MCPTest/Variant_Combat/UI",
			"MCPTest/Variant_SideScrolling",
			"MCPTest/Variant_SideScrolling/AI",
			"MCPTest/Variant_SideScrolling/Gameplay",
			"MCPTest/Variant_SideScrolling/Interfaces",
			"MCPTest/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
