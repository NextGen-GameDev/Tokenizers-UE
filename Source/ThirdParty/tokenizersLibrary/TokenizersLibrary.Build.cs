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
			// Built by Scripts/BuildTokenizersLib.ps1.
			PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "x64", "Release", "tokenizers_c.lib"));

			// From native-static-libs in x64/Release/tokenizers_c.buildinfo.json (minus CRT libs UE already links).
			PublicSystemLibraries.AddRange(new string[] { "ntdll.lib", "userenv.lib", "ws2_32.lib", "dbghelp.lib" });
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux || Target.Platform == UnrealTargetPlatform.LinuxArm64)
		{
			// Built by Scripts/BuildTokenizersLib.sh --target linux-x64 / linux-arm64.
			string Triple = Target.Platform == UnrealTargetPlatform.LinuxArm64 ? "aarch64-unknown-linux-gnu" : "x86_64-unknown-linux-gnu";
			PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "Linux", Triple, "libtokenizers_c.a"));

			// From native-static-libs in Linux/<triple>/tokenizers_c.buildinfo.json (the script fails on any other entry).
			PublicSystemLibraries.AddRange(new string[] { "gcc_s", "util", "rt", "pthread", "m", "dl", "c" });
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			// Built by Scripts/BuildTokenizersLib.sh --target mac: one universal lib (arm64 + x86_64).
			PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "Mac", "libtokenizers_c.a"));

			// From native-static-libs in Mac/tokenizers_c.buildinfo.json (libSystem, libc and libm are always linked;
			// the script fails on any entry not listed here).
			PublicSystemLibraries.AddRange(new string[] { "iconv", "resolv" });
			PublicFrameworks.AddRange(new string[] { "CoreFoundation", "Security" });
		}
	}
}
