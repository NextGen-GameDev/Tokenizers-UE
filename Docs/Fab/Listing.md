# Fab listing: Tokenizers (DRAFT)

DRAFT for Noah. Nothing here is published. Price: free. TODO(Noah): check each field in the Fab publisher portal.

## Title (max 60 characters)

Tokenizers: Hugging Face tokenizer.json for Unreal Engine

(56 characters)

## Short description (max 200 characters)

Load a Hugging Face tokenizer.json in Unreal. Encode, decode and padded batches from Blueprints and C++. Thread-safe, Win64. (124 characters)

## Long description

Tokenizers turns text into token ids and back, using a Hugging Face `tokenizer.json` file. You need this before you can feed text to most language models.

### Who it is for

Unreal developers who run language models in their project and need the same token ids that Hugging Face produces. It is also useful on its own for any tool that needs these tokenizers.

### Features

- Load a tokenizer from a JSON string or from a file.
- Encode text to ids, with or without special tokens such as `[CLS]` and `[SEP]`.
- Decode ids back to text, keeping or skipping special tokens.
- Encode a batch of texts into padded, optionally truncated arrays with an attention mask.
- Vocabulary size (`GetVocabSize`).
- Thread-safe: calls on one object are serialized by a lock, and separate objects run in parallel.
- Bad input does not crash. Calls return false or an empty result, and `GetLastError` says why. Errors are also logged to `LogTokenizers`.
- Works from Blueprints (category **Tokenizer**) and C++.
- Built on a Rust static library (a fork of tokenizers-cpp, which wraps the Hugging Face tokenizers crate).

### Tested

86 automation tests under `Tokenizers.*`, run on UE 5.8 (5.8.3). The output matches Python `tokenizers` 0.22.2 exactly on bert-base-uncased (WordPiece), gpt2 (BPE) and t5-small (SentencePiece Unigram), on 24 strings (13 non-ASCII). A stress test ran 8 threads for 10 s with 0 mismatches. These results are from 2026-09-30, before the Unreleased changes in the changelog. TODO(Noah): re-run the tests on the release build and update these lines.

### Use with Model Pipelines

Tokenizers can be installed next to the Model Pipelines plugin. Model Pipelines has its own built-in tokenizer, so it does not need this plugin.

## Technical details

- Features: encode, decode, padded and truncated batch encode with attention mask, vocabulary size, Blueprint and C++ API.
- Code modules: Tokenizers (Runtime, PreDefault loading phase). Main type: `UTokenizerWrapper` (Blueprint display name "Tokenizer Wrapper"), with the struct `FTokenizedBatch`.
- Platforms: Win64.
- Supported engine versions: 5.8 (built and tested) and 5.7 (built, tests not run). UE 5.6 not tested.
- Network use: none at runtime.
- Dependencies: none other than the engine. It links a prebuilt static library, `tokenizers_c.lib`. TODO(Noah): the library file is not in git. Confirm it is included in the Fab package.
- Documentation: `README.md` in the plugin. TODO(Noah): public link.
- Example project / sample maps: none. The README has Blueprint and C++ quick starts. TODO(Noah): Fab may want an example. Decide if one is needed.
- Version: 0.2.0 now. 0.3.0 is proposed for the release.

## Tags (max 10)

AI, Machine Learning, Tokenizer, Hugging Face, NLP, Text, Blueprint, Rust, Language Model, Utility

TODO(Noah): Fab may limit or restrict tags.

## Category suggestion

Code Plugins. TODO(Noah): pick the exact category and subcategory in the publisher portal. The `.uplugin` category is "AI".

## What's included

- The plugin: Tokenizers runtime module (Win64), the library build scripts (`Scripts/`), one sample tokenizer file in `Content/`.
- `README.md`, `CHANGELOG.md`, `THIRD_PARTY_NOTICES.md`, `LICENSE`, `LICENSE-APACHE`.
- Automation tests under `Tokenizers.*`.
- Not included: tokenizer files for your models. Get them from the model's Hugging Face page.

## Requirements

- Windows 64-bit.
- Unreal Engine 5.8 or 5.7.
- `tokenizers_c.lib` in `Source/ThirdParty/tokenizersLibrary/x64/Release/`. To build it you need git, Rust and Visual Studio 2022 with the MSVC 14.44 toolset. See the README.

## Known limitations

- Windows 64-bit only.
- Tests were run on UE 5.8 only. UE 5.7 was built but not tested. UE 5.6 not tested.
- Calls are synchronous. Do not load a large file with `InitializeTokenizerFromFile` on the game thread in shipping code.
- The Parity and Stress tests need golden files made with the Pipelines-UE tooling. Without them they fail by design.

## Support

Issues: https://github.com/NextGen-GameDev/Tokenizers-UE/issues

TODO(Noah): confirm the support link (or an email) before publishing.

## License notes

- The plugin code is under the MIT License (`LICENSE`). Files with an Apache-2.0 header are under the Apache License 2.0 (`LICENSE-APACHE`).
- Third-party components and licenses are in `THIRD_PARTY_NOTICES.md`.
- TODO(Noah): `THIRD_PARTY_NOTICES.md` line 3 still names tag `v0.1.4`. The library is v0.1.5. Fix before publishing.
- TODO(Noah): Fab asks for a license choice for the listing. Decide how it matches MIT/Apache-2.0.

## Credits

[MLC-AI tokenizers-cpp](https://github.com/mlc-ai/tokenizers-cpp), the fork [P1ayer-1/tokenizers-cpp](https://github.com/P1ayer-1/tokenizers-cpp) and [Hugging Face tokenizers](https://github.com/huggingface/tokenizers).
