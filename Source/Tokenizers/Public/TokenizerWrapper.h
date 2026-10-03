// Copyright NextGen-GameDev. Licensed under the Apache License 2.0.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "UObject/Object.h"
#include "TokenizerWrapper.generated.h"

/** Result of UTokenizerWrapper::EncodeBatch: NumRows rows of SeqLen ids each, row-major, right-padded. */
USTRUCT(BlueprintType)
struct TOKENIZERS_API FTokenizedBatch
{
	GENERATED_BODY()

	/** Row-major [NumRows * SeqLen]; row r is InputIds[r*SeqLen .. r*SeqLen+SeqLen-1]. */
	UPROPERTY(BlueprintReadOnly, Category = "Tokenizer")
	TArray<int32> InputIds;

	/** Same shape; 1 = real token, 0 = padding. */
	UPROPERTY(BlueprintReadOnly, Category = "Tokenizer")
	TArray<int32> AttentionMask;

	UPROPERTY(BlueprintReadOnly, Category = "Tokenizer")
	int32 NumRows = 0;

	/** Length of the longest row after truncation. */
	UPROPERTY(BlueprintReadOnly, Category = "Tokenizer")
	int32 SeqLen = 0;
};

/**
 * Blueprint-facing wrapper around one tokenizers_c handle (a Hugging Face tokenizer.json).
 *
 * Errors never crash: calls return false / an empty result and GetLastError() says why.
 *
 * Thread safety: every public call on one object is serialized by a per-object lock, so an
 * object may be called from any thread; different objects run in parallel. Keeping the object
 * alive across threads (TStrongObjectPtr) is the caller's job.
 */
UCLASS(Blueprintable, BlueprintType, DisplayName = "Tokenizer Wrapper")
class TOKENIZERS_API UTokenizerWrapper : public UObject
{
	GENERATED_BODY()

public:
	UTokenizerWrapper();

	/** Parse a tokenizer.json string. A leading U+FEFF (BOM) is skipped.
	    On success replaces (and frees) any previous tokenizer.
	    On failure the previous tokenizer stays loaded, returns false, GetLastError() says why. */
	UFUNCTION(BlueprintCallable, Category = "Tokenizer")
	bool InitializeTokenizerFromJson(const FString& JsonBlob);

	/** FilePath resolution, first existing file wins:
	    1. absolute path -> used as is;
	    2. relative -> FPaths::ProjectContentDir() / FilePath;
	    3. relative -> <Tokenizers plugin>/Content / FilePath.
	    Read as raw bytes; a leading UTF-8 BOM (EF BB BF) is skipped, every other byte is passed
	    to the library unchanged. The file is read before the object's lock is taken.
	    Same replace/keep rules as FromJson. Not found -> false, error names the path(s) tried.
	    Parse failure -> false, error names the file. */
	UFUNCTION(BlueprintCallable, Category = "Tokenizer")
	bool InitializeTokenizerFromFile(const FString& FilePath);

	UFUNCTION(BlueprintPure, Category = "Tokenizer")
	bool IsInitialized() const;

	/** Empty array on error (not initialized, library error); see GetLastError(). */
	UFUNCTION(BlueprintCallable, Category = "Tokenizer")
	TArray<int32> Encode(const FString& Text, bool bAddSpecialTokens = false);

	/** Any id < 0 -> returns "" and sets the last error. Unknown ids are skipped by the library. */
	UFUNCTION(BlueprintCallable, Category = "Tokenizer")
	FString Decode(const TArray<int32>& Ids, bool bSkipSpecialTokens = false);

	/** Encodes all texts in one C call (tokenizers_encode_batch_truncated, v0.1.4).
	    MaxLength 0 = no truncation; > 0 = HF truncation (special tokens included, done in Rust).
	    Pads on the right with PadTokenId to the longest row; mask 1/0.
	    Empty Texts -> true, NumRows = SeqLen = 0, arrays empty.
	    Errors (not initialized, MaxLength < 0, PadTokenId < 0, library error, NumRows*SeqLen > MAX_int32)
	    -> false, OutBatch reset to empty, GetLastError() set. */
	UFUNCTION(BlueprintCallable, Category = "Tokenizer")
	bool EncodeBatch(const TArray<FString>& Texts, FTokenizedBatch& OutBatch,
		bool bAddSpecialTokens = false, int32 MaxLength = 0, int32 PadTokenId = 0);

	/** Message from the most recent call on this object: "" if that call succeeded. */
	UFUNCTION(BlueprintPure, Category = "Tokenizer")
	FString GetLastError() const;

	/** Frees the handle (under the lock), then calls Super::BeginDestroy(). The destructor frees nothing. */
	virtual void BeginDestroy() override;

private:
	/** Builds a tokenizer from UTF-8 bytes and swaps it in on success. Sets LastError on failure,
	    with Context appended to the message. Caller holds Mutex. */
	bool InitializeFromUtf8(const char* Data, SIZE_T Len, const TCHAR* FunctionName, const FString& Context);

	/** Sets LastError and logs it once as a warning. Caller holds Mutex. */
	void SetError(FString Message);

	/** Serializes every public call on this object (the C handle is not thread-safe). */
	mutable FCriticalSection Mutex;

	/** TokenizerHandle from tokenizers_c.h, which is included only in the .cpp. Owned; freed with tokenizers_free. */
	void* Tokenizer = nullptr;

	FString LastError;
};
