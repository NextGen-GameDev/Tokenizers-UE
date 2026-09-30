// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "TokenizerWrapper.generated.h"

/**
 * Blueprint-facing wrapper around one tokenizers_c handle (a Hugging Face tokenizer.json).
 *
 * Errors never crash: calls return false / an empty result and GetLastError() says why.
 * The handle is not thread-safe; use one wrapper per thread (locking arrives in WP0.4).
 */
UCLASS(Blueprintable, BlueprintType, DisplayName = "Tokenizer Wrapper")
class TOKENIZERS_API UTokenizerWrapper : public UObject
{
	GENERATED_BODY()

public:
	UTokenizerWrapper();

	/** Parse a tokenizer.json string. On success replaces (and frees) any previous tokenizer.
	    On failure the previous tokenizer stays loaded, returns false, GetLastError() says why. */
	UFUNCTION(BlueprintCallable, Category = "Tokenizer")
	bool InitializeTokenizerFromJson(const FString& JsonBlob);

	/** FilePath resolution, first existing file wins:
	    1. absolute path -> used as is;
	    2. relative -> FPaths::ProjectContentDir() / FilePath;
	    3. relative -> <Tokenizers plugin>/Content / FilePath.
	    Read as raw bytes and passed to the library unchanged.
	    Same replace/keep rules as FromJson. Not found -> false, error names the path(s) tried. */
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

	/** Message from the most recent call on this object: "" if that call succeeded. */
	UFUNCTION(BlueprintPure, Category = "Tokenizer")
	FString GetLastError() const;

	/** Frees the handle, then calls Super::BeginDestroy(). The destructor frees nothing. */
	virtual void BeginDestroy() override;

private:
	/** Builds a tokenizer from UTF-8 bytes and swaps it in on success. Sets LastError on failure. */
	bool InitializeFromUtf8(const char* Data, SIZE_T Len, const TCHAR* FunctionName);

	/** Sets LastError and logs it once as a warning. */
	void SetError(FString Message);

	/** TokenizerHandle from tokenizers_c.h, which is included only in the .cpp. Owned; freed with tokenizers_free. */
	void* Tokenizer = nullptr;

	FString LastError;
};
