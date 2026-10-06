# Changelog

## 0.3.0 (2026-10-06)

### Added

- `GetVocabSize`: vocabulary size including added tokens (-1 when not initialized).

### Changed

- The library is now tokenizers-cpp fork v0.1.5.

### Fixed

- tokenizer.json padding no longer leaks into Encode/EncodeBatch.

## 0.2.0 (2026-09-30)

### Added

- `EncodeBatch` and `FTokenizedBatch`: padded, optionally truncated batches with an attention mask.
- `bAddSpecialTokens` on `Encode` and `bSkipSpecialTokens` on `Decode`.
- `GetLastError` and `IsInitialized`.
- `LogTokenizers` log category.
- Loading from any path with `InitializeTokenizerFromFile`.
- A build script (`Scripts/BuildTokenizersLib.ps1`) and a pinned library source.
- Automation tests under `Tokenizers.*`.

### Changed

- `InitializeTokenizerFromJson` now returns `bool`. Existing Blueprint nodes gain an output pin.
- Parameters are now `const FString&`.
- The public header no longer includes `tokenizers_c.h`.
- The library is now tokenizers-cpp fork v0.1.4 (Rust `tokenizers` 0.21.4).

### Fixed

- `Decode` read past the end of the library's buffer, which gave garbage or a crash on non-ASCII text.
- A null tokenizer handle crashed.
- Re-initializing leaked the old tokenizer.
- The handle was freed in the destructor instead of `BeginDestroy`.
- Invalid tokenizer JSON or invalid UTF-8 aborted the process. The library now returns errors.
- `IMPLEMENT_MODULE` name mismatch.
- A trailing U+0000 was dropped.
