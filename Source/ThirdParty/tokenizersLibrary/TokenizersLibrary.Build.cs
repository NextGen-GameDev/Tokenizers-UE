// Fill out your copyright notice in the Description page of Project Settings.

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
			PublicAdditionalLibraries.AddRange(new string[]
			{
				"ws2_32.lib",
				"bcrypt.lib",
				
				"Advapi32.lib",
				"ntdll.lib", // if you're targeting Windows 8 and later
				"Userenv.lib", // Add this line
			});		

		}

	}
}
