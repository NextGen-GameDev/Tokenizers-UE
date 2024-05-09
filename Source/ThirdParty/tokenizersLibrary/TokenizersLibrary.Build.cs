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

			PublicSystemLibraries.AddRange(new string[] {"bcrypt.lib", "ws2_32.lib", "ntdll.lib", "Userenv.lib"});


		}
		
		

	}
}
