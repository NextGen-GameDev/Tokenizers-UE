// Copyright 2026 Holdout Labs, LLC. Licensed under the MIT License.

using System.IO;
using UnrealBuildTool;

public class TokenizersLibrary : ModuleRules
{
	public TokenizersLibrary(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;
		PublicSystemIncludePaths.Add("$(ModuleDir)/Public");

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// Add the import library
			PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "x64", "Release", "tokenizers_c.lib"));

			// From native-static-libs in x64/Release/tokenizers_c.buildinfo.json (minus CRT libs UE already links).
			PublicSystemLibraries.AddRange(new string[] { "ntdll.lib", "userenv.lib", "ws2_32.lib", "dbghelp.lib" });


		}
		
		

	}
}
