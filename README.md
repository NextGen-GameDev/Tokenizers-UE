# Tokenizers

Hugging Face tokenizers (`tokenizer.json`) for Unreal Engine: encode, decode and padded batches, thread-safe, via a Rust static library. Works from C++ and Blueprints.

Repo: https://github.com/NextGen-GameDev/Tokenizers-UE. Author: P1ayer-1.

Contents: [What it is](#what-it-is) | [Install](#install) | [Quick start](#quick-start) | [API reference](#api-reference) | [Threading](#threading) | [Errors](#errors) | [Testing](#testing) | [License](#license) | [Credits](#credits)

## What it is

Tokenizers turns text into token ids and back, using a Hugging Face `tokenizer.json` file. You need this before you can feed text to most language models.

You can:

- Load a tokenizer from a JSON string or from a file.
- Encode text to ids, with or without special tokens such as `[CLS]` and `[SEP]`.
- Decode ids back to text, keeping or skipping special tokens.
- Encode a batch of texts into padded, optionally truncated arrays with an attention mask.
- Do all of this from Blueprints and C++.

The plugin never crashes on bad input. Calls return `false` or an empty result, and `GetLastError` tells you why.

Platforms: Win64, Linux, LinuxArm64 and Mac (Apple silicon and Intel). Each needs its own native library build (see below). The automation tests have been run on Win64 only. On Linux and Mac, the native library is tested without the engine (see [Linux and Mac](#linux-and-mac)).

## Install

### Engine versions

Built and tested on UE 5.8 (5.8.3). UE 5.6 has not been tested.

| Engine | Status |
|---|---|
| UE 5.8 | Built (BuildPlugin) and tested: 86/86 automation tests |
| UE 5.7 | Built (BuildPlugin); tests not run on 5.7 |
| UE 5.6 | Not tested |

### Steps

1. Copy this folder into your project's `Plugins` folder, so the plugin is at `<Project>/Plugins/Tokenizers-UE/`.
2. Build the native library (next section). The library file is not in git.
3. Open your project. Generate project files and build it. The plugin is enabled by default in the editor's Plugins window under **AI > Tokenizers**.

### Build the native library

The library is built from a pinned fork, [P1ayer-1/tokenizers-cpp](https://github.com/P1ayer-1/tokenizers-cpp), tag `v0.1.5`. The fork returns error codes instead of aborting, adds truncated batch encoding and never pads in the encode calls. Build it on each platform you ship. The plugin links:

| Platform | Library | Build with |
|---|---|---|
| Win64 | `Source/ThirdParty/tokenizersLibrary/x64/Release/tokenizers_c.lib` | `Scripts\BuildTokenizersLib.ps1` |
| Linux | `Source/ThirdParty/tokenizersLibrary/Linux/x86_64-unknown-linux-gnu/libtokenizers_c.a` | `Scripts/BuildTokenizersLib.sh --target linux-x64` |
| LinuxArm64 | `Source/ThirdParty/tokenizersLibrary/Linux/aarch64-unknown-linux-gnu/libtokenizers_c.a` | `Scripts/BuildTokenizersLib.sh --target linux-arm64` |
| Mac | `Source/ThirdParty/tokenizersLibrary/Mac/libtokenizers_c.a` (universal: arm64 + x86_64) | `Scripts/BuildTokenizersLib.sh --target mac` |

#### Windows

You need:

- git
- Rust (rustup), with the target `x86_64-pc-windows-msvc`
- Visual Studio 2022 with the MSVC 14.44 toolset (the same toolset UE 5.8 uses)

Run this in PowerShell 5.1 from the plugin folder:

```powershell
.\Scripts\BuildTokenizersLib.ps1
```

Parameters:

| Parameter | Default | Meaning |
|---|---|---|
| `-WorkDir` | `%TEMP%\tokenizers-cpp-build` | Where the source is cloned and built. Must be outside the plugin folder. |
| `-MsvcToolset` | `14.44` | MSVC toolset version to build with. |
| `-Clean` | off | Start from a clean build. |

A cold build takes about 5 minutes. When it finishes, `tokenizers_c.lib` is in place, and `tokenizers_c.buildinfo.json` sits next to it. The buildinfo file records the commit, rustc, toolset, crate version and SHA256.

#### Linux and Mac

You need:

- git and bash
- Rust (rustup), with the target for your platform: `x86_64-unknown-linux-gnu`, `aarch64-unknown-linux-gnu`, or both `aarch64-apple-darwin` and `x86_64-apple-darwin` on Mac
- A C compiler (the library includes oniguruma, which is C). On Mac, the Xcode command line tools.

Run this from the plugin folder:

```bash
./Scripts/BuildTokenizersLib.sh
```

With no `--target`, it builds for the machine it runs on. Build the Mac library on a Mac, or cross-compile it (below).

Parameters:

| Parameter | Default | Meaning |
|---|---|---|
| `--target` | the host | `linux-x64`, `linux-arm64` or `mac`. |
| `--work-dir` | `$TMPDIR/tokenizers-cpp-build` | Where the source is cloned and built. Must be outside the plugin folder. |
| `--ue-toolchain` | `$LINUX_MULTIARCH_ROOT` | Linux only. The Unreal Linux cross toolchain. Its clang and sysroot compile the C code, so it matches the glibc the engine links. Needed to cross-build `linux-arm64` on an x86_64 host, or to build Linux on Windows/Mac. |
| `--macos-min` | `11.0` | Mac only. `MACOSX_DEPLOYMENT_TARGET`. |
| `--clean` | off | Start from a clean build. |

The script checks the pinned commit, builds with `cargo --locked`, checks that all 13 C API functions are in the library, and checks that the library needs no system library the plugin does not link. On Linux it also fails if the C code needs glibc 2.38 or newer. `tokenizers_c.buildinfo.json` is written next to the library.

Without the Unreal toolchain, the host compiler is used. That works on current distributions; the script tells you if it does not.

To use another C compiler, set cc-rs's own variable for the Rust target, for example `CC_aarch64_unknown_linux_gnu`. This also lets you cross-compile, for example the Mac library on Linux with `zig cc` (set `CC_aarch64_apple_darwin` and `CC_x86_64_apple_darwin`; it then needs `llvm-nm` and a `lipo`).

Then test the library without the engine:

```bash
./Scripts/TestTokenizersLib.sh
```

It links `Tests/Native/TokenizersLibSmoke.cpp` into a shared library, the way Unreal links the Tokenizers module. It allows no undefined symbols and uses exactly the system libraries `TokenizersLibrary.Build.cs` lists. Then it runs 11 checks on `Content/tokenizer.json`: encode, decode, non-ASCII round trip, a truncated batch, bad JSON, invalid UTF-8 and 4 threads. On Mac, `--arch arm64` or `--arch x86_64` picks the slice to test. `CXX`, `LDFLAGS` and `RUNNER` (for example `qemu-aarch64-static -L /usr/aarch64-linux-gnu`) set the compiler, extra link flags and an emulator. `--link-only` only links.

The [Native libs workflow](.github/workflows/native-libs.yml) runs both scripts on GitHub Actions for Linux x64, Linux arm64, Mac arm64 and Mac x86_64. On Linux it also links against glibc 2.17 with libc++, as the Unreal toolchain does. Download the built libraries from the run's artifacts.

### Building a renamed copy

Win64 only for now. Another plugin can ship its own copy of the library next to this one, if every symbol in its copy has a prefix. Without the prefix, the two copies collide at link time. `Scripts\MakePrefixedTokenizersLib.ps1` makes such a copy from the installed lib without rebuilding it:

```powershell
.\Scripts\MakePrefixedTokenizersLib.ps1 -InputLib .\Source\ThirdParty\tokenizersLibrary\x64\Release\tokenizers_c.lib -SymbolPrefix mpt_ -OutDir <dir>
```

It writes four files to `<dir>`:

- `mpt_tokenizers_c.lib`: every defined symbol is renamed, including the Rust runtime and oniguruma. MSVC constants and inline CRT helpers are not renamed.
- `mpt_tokenizers_c.h`: the functions are `mpt_tokenizers_*`, the types `MptTokenizerHandle` and `MptTokenizerEncodeResult`, the status codes `MPT_TOKENIZERS_*`. It can be included in the same file as `tokenizers_c.h`.
- `mpt_tokenizers_c.buildinfo.json`: the prefix, the input and output SHA256, the rename count and the tool versions.
- `mpt_tokenizers_c.renames.txt`: the rename map.

The script needs `lib.exe` and `dumpbin.exe` from Visual Studio 2022 (MSVC 14.44) and `rust-objcopy.exe` from the rustc sysroot. It takes about a minute. The same input lib and tools always give the same output bytes. It checks the result and fails with a message if a check fails. It only moves files into `<dir>` once all checks pass.

`BuildTokenizersLib.ps1 -SymbolPrefix mpt_ -VariantOutDir <dir>` runs the same step after a build. This plugin's own lib is the same with or without it.

The two copies are separate libraries, each with its own allocator and thread-local state. Pass a handle, encode result or error string from the renamed copy only to `mpt_` functions.

## Quick start

### Blueprint

All nodes are in the **Tokenizer** category.

1. Call **Construct Object from Class** with class **Tokenizer Wrapper**. Store the result in a variable so it stays alive.
2. Call **Initialize Tokenizer From File** on it. Pass a path to a `tokenizer.json`. Check the returned Boolean. If it is false, print **Get Last Error**.
3. Call **Encode** with your text. Set **Add Special Tokens** to true for BERT-style models. You get an array of ints.
4. Call **Decode** with the ids to get the text back.

### C++

```cpp
#include "TokenizerWrapper.h"

UTokenizerWrapper* Tok = NewObject<UTokenizerWrapper>();
// Keep Tok alive, for example in a UPROPERTY() member.

if (!Tok->InitializeTokenizerFromFile(TEXT("tokenizer.json")))
{
    UE_LOG(LogTemp, Warning, TEXT("Load failed: %s"), *Tok->GetLastError());
    return;
}

// Single string, with [CLS] / [SEP] style special tokens.
TArray<int32> Ids = Tok->Encode(TEXT("Hello world"), true);

// Padded batch, truncated to 128 ids per row, padded with id 0.
FTokenizedBatch Batch;
TArray<FString> Texts = { TEXT("first text"), TEXT("a second, longer text") };
if (Tok->EncodeBatch(Texts, Batch, true, 128, 0))
{
    // Batch.InputIds and Batch.AttentionMask hold Batch.NumRows * Batch.SeqLen values.
}
else
{
    UE_LOG(LogTemp, Warning, TEXT("Batch failed: %s"), *Tok->GetLastError());
}
```

The relative path `tokenizer.json` is looked up as described in the API reference below.

## API reference

Class: `UTokenizerWrapper` (display name "Tokenizer Wrapper"). Header: `Source/Tokenizers/Public/TokenizerWrapper.h`. All nodes are in the Blueprint category "Tokenizer".

| Function | Notes |
|---|---|
| `bool InitializeTokenizerFromJson(const FString& JsonBlob)` | Parses a `tokenizer.json` string. |
| `bool InitializeTokenizerFromFile(const FString& FilePath)` | Loads a file. Tries an absolute path first, then `<Project>/Content/<path>`, then `<Tokenizers plugin>/Content/<path>`. A UTF-8 BOM is skipped. |
| `bool IsInitialized() const` | True once a tokenizer is loaded. |
| `TArray<int32> Encode(const FString& Text, bool bAddSpecialTokens = false)` | Returns ids. Empty array on error. |
| `FString Decode(const TArray<int32>& Ids, bool bSkipSpecialTokens = false)` | Returns text. A negative id returns an empty string and sets the error. |
| `bool EncodeBatch(const TArray<FString>& Texts, FTokenizedBatch& OutBatch, bool bAddSpecialTokens = false, int32 MaxLength = 0, int32 PadTokenId = 0)` | Encodes many texts in one call. See below. |
| `FString GetLastError() const` | Message from the latest call on this object. Empty after a success. |

If an initialize call fails, the previous tokenizer stays loaded.

Special tokens are off by default. BERT-style models need `bAddSpecialTokens = true` to get `[CLS]` and `[SEP]`.

### EncodeBatch and FTokenizedBatch

`FTokenizedBatch` has:

- `InputIds`: row-major, `NumRows * SeqLen` ids.
- `AttentionMask`: same shape. 1 is a real token, 0 is padding.
- `NumRows`: number of texts.
- `SeqLen`: length of the longest row after truncation.

`MaxLength` 0 means no truncation. A positive value truncates the way Hugging Face does, and special tokens are kept (for example `[SEP]`). Rows are right-padded with `PadTokenId` to the longest row.

An empty `Texts` array returns true with `NumRows` and `SeqLen` both 0.

On an error (not initialized, negative `MaxLength`, negative `PadTokenId`, library error, or a result too large for `int32`) the call returns false, `OutBatch` is emptied, and `GetLastError` says why.

## Threading

- Calls on one object are serialized by a per-object lock, so you may call it from any thread.
- Separate objects run in parallel.
- Keep the object alive (`TStrongObjectPtr` or a `UPROPERTY`) while other threads use it.
- Calls are synchronous. Do not load a large file with `InitializeTokenizerFromFile` on the game thread in shipping code.

## Errors

- Bad input does not crash. Invalid tokenizer JSON and invalid UTF-8 come back as errors.
- Failed calls return `false` or an empty result.
- `GetLastError` holds the message from the latest call on that object, and is empty after a success.
- Errors are also logged to the `LogTokenizers` category.

## Testing

Automation tests live under `Tokenizers.*`. There are 86:

| Group | Count |
|---|---|
| Lib | 6 |
| Wrapper | 37 |
| Batch | 20 |
| Parity | 21 (7 per tokenizer) |
| Stress | 2 |

Results from a run on 2026-09-30 on UE 5.8:

- Parity: exact match with Python `tokenizers` 0.22.2 on bert-base-uncased (WordPiece), gpt2 (BPE) and t5-small (SentencePiece Unigram). This covers 24 strings (13 non-ASCII), encode with and without special tokens, decode keeping and skipping them, and 4 padded or truncated batches.
- Stress: 8 threads for 10 s, 0 mismatches.

The Parity and Stress tests need golden files. To make them, run `Tools/fetch_tokenizers.py` and `Tools/make_tokenizer_goldens.py` from the Pipelines-UE tooling. Then set the `TOKENIZERS_GOLDENS_DIR` environment variable to the goldens folder. Without goldens, the Parity and Stress tests fail by design.

Run the tests:

```
UnrealEditor-Cmd.exe <Project>.uproject -ExecCmds="Automation RunTests Tokenizers; Quit" -unattended -nullrhi
```

On Linux and Mac the editor binary is `UnrealEditor` (Linux: `Engine/Binaries/Linux/UnrealEditor`, Mac: `Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor`). The arguments are the same.

## License

The plugin code is under the MIT License (`LICENSE`). Files with an Apache-2.0 header are under the Apache License 2.0 (`LICENSE-APACHE`). Third-party components and their licenses are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Changes are listed in [CHANGELOG.md](CHANGELOG.md).

## Credits

- [MLC-AI tokenizers-cpp](https://github.com/mlc-ai/tokenizers-cpp): the C/C++ binding this plugin is based on. The plugin builds its library from a fork, [P1ayer-1/tokenizers-cpp](https://github.com/P1ayer-1/tokenizers-cpp).
- [Hugging Face tokenizers](https://github.com/huggingface/tokenizers): the tokenizer library that does the work.

Contributing: see [CONTRIBUTING.md](CONTRIBUTING.md). Issues: https://github.com/NextGen-GameDev/Tokenizers-UE/issues
