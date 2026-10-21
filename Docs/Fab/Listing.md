# Fab listing: Tokenizers (DRAFT)

Ready to paste (2026-10-10). Price: free. Check field limits in the Fab publisher portal.

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

105 automation tests, all passing on UE 5.8 and UE 5.7 with the release package installed into a fresh project. The output matches Python `tokenizers` 0.22.2 exactly on bert-base-uncased (WordPiece), gpt2 (BPE) and t5-small (SentencePiece Unigram), on 24 strings (13 non-ASCII). A stress test ran 8 threads for 10 s with 0 mismatches.

### Use with Model Pipelines

Tokenizers can be installed next to the Model Pipelines plugin. Model Pipelines has its own built-in tokenizer, so it does not need this plugin.

## Technical details

- Features: encode, decode, padded and truncated batch encode with attention mask, vocabulary size, Blueprint and C++ API.
- Code modules: Tokenizers (Runtime, PreDefault loading phase). Main type: `UTokenizerWrapper` (Blueprint display name "Tokenizer Wrapper"), with the struct `FTokenizedBatch`.
- Platforms: Win64.
- Supported engine versions: 5.8 and 5.7 (built and tested).
- Network use: none at runtime.
- Dependencies: none other than the engine. It links a prebuilt static library, `tokenizers_c.lib`, which is included in the package.
- Documentation: https://github.com/NextGen-GameDev/Tokenizers-UE#readme (also `README.md` in the plugin).
- Example project / sample maps: none. The README has Blueprint and C++ quick starts.
- Version: 0.3.0.

## Tags (max 10)

AI, Machine Learning, Tokenizer, Hugging Face, NLP, Text, Blueprint, Rust, Language Model, Utility


## Category suggestion

Code Plugins (pick the closest subcategory in the portal). The `.uplugin` category is "AI".

## What's included

- The plugin: Tokenizers runtime module (Win64) and the prebuilt library. No Content folder.
- `README.md`, `CHANGELOG.md`, `THIRD_PARTY_NOTICES.md` (includes the Apache-2.0 text the third-party components need).
- Not in the Fab package (GitHub only, per Fab review): the example `Content/tokenizer.json`, the `LICENSE` files (the Fab license applies), the build scripts.
- Not included: tokenizer files for your models. Get them from the model's Hugging Face page.

## Requirements

- Windows 64-bit.
- Unreal Engine 5.8 or 5.7.
- Nothing else: the prebuilt `tokenizers_c.lib` is included. Rebuilding it yourself (optional) needs git, Rust and Visual Studio 2022 with the MSVC 14.44 toolset; see the README.

## Known limitations

- Windows 64-bit only.
- UE 5.6 and earlier are not supported.
- Calls are synchronous. Do not load a large file with `InitializeTokenizerFromFile` on the game thread in shipping code.

## Support

Email: support@holdoutlabs.ai
Issues: https://github.com/NextGen-GameDev/Tokenizers-UE/issues

## License notes

- Fab package: the Fab license applies; no license file ships. GitHub: MIT (`LICENSE`), Apache-2.0 for files with that header (`LICENSE-APACHE`).
- Third-party components and licenses are in `THIRD_PARTY_NOTICES.md`.
- Free.

## Credits

[MLC-AI tokenizers-cpp](https://github.com/mlc-ai/tokenizers-cpp), the fork [P1ayer-1/tokenizers-cpp](https://github.com/P1ayer-1/tokenizers-cpp) and [Hugging Face tokenizers](https://github.com/huggingface/tokenizers).
