// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class WebVRHMD : ModuleRules
	{
		public WebVRHMD(TargetInfo Target)
		{
			PrivateIncludePaths.AddRange(
				new string[] {
					"WebVRHMD/Private",
					"../../../../Source/Runtime/Renderer/Private",
					// ... add other private include paths required here ...
				}
				);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"InputCore",
					"RHI",
					"RenderCore",
					"Renderer",
					"ShaderCore",
					"HeadMountedDisplay"
				}
				);
		}
	}
}