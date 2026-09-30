// Copyright Epic Games, Inc. All Rights Reserved.

#include "TokenizerWrapper.h"

#include "TokenizersModule.h"
#include "Containers/StringConv.h"
#include "Interfaces/IPluginManager.h"
#include "Math/NumericLimits.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

THIRD_PARTY_INCLUDES_START
#include <TokenizersLibrary/tokenizers_c.h>
THIRD_PARTY_INCLUDES_END

#include UE_INLINE_GENERATED_CPP_BY_NAME(TokenizerWrapper)

namespace UE::Tokenizers::Private
{
	/** Converts UTF-8 bytes with an explicit length (never null-terminated) to an FString. */
	static FString Utf8ToFString(const char* Data, SIZE_T Len)
	{
		if (Data == nullptr || Len == 0 || Len > static_cast<SIZE_T>(MAX_int32))
		{
			return FString();
		}
		const FUTF8ToTCHAR Conv(Data, static_cast<int32>(Len));
		return FString::ConstructFromPtrSize(Conv.Get(), Conv.Length());
	}

	/** The library's thread-local message for the most recent failure on this thread. */
	static FString LibraryError()
	{
		const char* Data = nullptr;
		size_t Len = 0;
		tokenizers_get_last_error(&Data, &Len);
		FString Message = Utf8ToFString(Data, Len);
		return Message.IsEmpty() ? FString(TEXT("(no message from tokenizers library)")) : Message;
	}
}

UTokenizerWrapper::UTokenizerWrapper()
{
}

void UTokenizerWrapper::BeginDestroy()
{
	if (Tokenizer != nullptr)
	{
		tokenizers_free(Tokenizer);
		Tokenizer = nullptr;
	}
	Super::BeginDestroy();
}

void UTokenizerWrapper::SetError(FString Message)
{
	LastError = MoveTemp(Message);
	UE_LOG(LogTokenizers, Warning, TEXT("%s"), *LastError);
}

bool UTokenizerWrapper::InitializeFromUtf8(const char* Data, SIZE_T Len, const TCHAR* FunctionName)
{
	TokenizerHandle NewHandle = tokenizers_new_from_str(Data, Len);
	if (NewHandle == nullptr)
	{
		SetError(FString::Printf(TEXT("%s: failed to parse tokenizer JSON: %s"),
			FunctionName, *UE::Tokenizers::Private::LibraryError()));
		return false;
	}

	// Replace only after the new tokenizer parsed, so a failure keeps the previous one.
	if (Tokenizer != nullptr)
	{
		tokenizers_free(Tokenizer);
	}
	Tokenizer = NewHandle;
	return true;
}

bool UTokenizerWrapper::InitializeTokenizerFromJson(const FString& JsonBlob)
{
	LastError.Reset();

	// A leading U+FEFF (byte order mark) is skipped; the parser rejects it.
	const int32 Skip = (JsonBlob.Len() > 0 && JsonBlob[0] == TCHAR(0xFEFF)) ? 1 : 0;
	const FTCHARToUTF8 Utf8(*JsonBlob + Skip, JsonBlob.Len() - Skip);
	return InitializeFromUtf8(reinterpret_cast<const char*>(Utf8.Get()), static_cast<SIZE_T>(Utf8.Length()),
		TEXT("InitializeTokenizerFromJson"));
}

bool UTokenizerWrapper::InitializeTokenizerFromFile(const FString& FilePath)
{
	LastError.Reset();

	if (FilePath.IsEmpty())
	{
		SetError(TEXT("InitializeTokenizerFromFile: file path is empty"));
		return false;
	}

	TArray<FString, TInlineAllocator<3>> Candidates;
	if (!FPaths::IsRelative(FilePath))
	{
		Candidates.Add(FilePath);
	}
	else
	{
		Candidates.Add(FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectContentDir(), FilePath)));

		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Tokenizers"));
		if (Plugin.IsValid())
		{
			Candidates.Add(FPaths::ConvertRelativePathToFull(FPaths::Combine(Plugin->GetContentDir(), FilePath)));
		}
	}

	const FString* Found = Candidates.FindByPredicate([](const FString& Path) { return FPaths::FileExists(Path); });
	if (Found == nullptr)
	{
		SetError(FString::Printf(TEXT("InitializeTokenizerFromFile: file '%s' not found; tried: %s"),
			*FilePath, *FString::Join(Candidates, TEXT(", "))));
		return false;
	}

	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, **Found))
	{
		SetError(FString::Printf(TEXT("InitializeTokenizerFromFile: could not read '%s'"), **Found));
		return false;
	}

	// A leading UTF-8 BOM (EF BB BF) is skipped; every other byte goes to the parser unchanged.
	int32 Offset = 0;
	if (Bytes.Num() >= 3 && Bytes[0] == 0xEF && Bytes[1] == 0xBB && Bytes[2] == 0xBF)
	{
		Offset = 3;
	}

	if (!InitializeFromUtf8(reinterpret_cast<const char*>(Bytes.GetData() + Offset), static_cast<SIZE_T>(Bytes.Num() - Offset),
		TEXT("InitializeTokenizerFromFile")))
	{
		LastError = FString::Printf(TEXT("%s (file '%s')"), *LastError, **Found);
		return false;
	}
	return true;
}

bool UTokenizerWrapper::IsInitialized() const
{
	return Tokenizer != nullptr;
}

TArray<int32> UTokenizerWrapper::Encode(const FString& Text, bool bAddSpecialTokens)
{
	LastError.Reset();

	TArray<int32> Ids;
	if (Tokenizer == nullptr)
	{
		SetError(TEXT("Encode: tokenizer not initialized"));
		return Ids;
	}

	// UTF-8 bytes with an explicit length; no terminator is passed.
	const FTCHARToUTF8 Utf8(*Text, Text.Len());

	// token_ids is allocated by the library; free it only with tokenizers_free_encode_results.
	TokenizerEncodeResult Result{};
	const int Status = tokenizers_encode(Tokenizer, reinterpret_cast<const char*>(Utf8.Get()),
		static_cast<size_t>(Utf8.Length()), bAddSpecialTokens ? 1 : 0, &Result);
	if (Status != TOKENIZERS_OK)
	{
		SetError(FString::Printf(TEXT("Encode: tokenizers_encode failed (%d): %s"),
			Status, *UE::Tokenizers::Private::LibraryError()));
	}
	else if (Result.len > static_cast<size_t>(MAX_int32))
	{
		// A result longer than TArray<int32> can index is dropped, never truncated.
		SetError(FString::Printf(TEXT("Encode: result has %llu ids, more than an array can hold"),
			static_cast<unsigned long long>(Result.len)));
	}
	else if (Result.token_ids != nullptr && Result.len > 0)
	{
		Ids.Append(reinterpret_cast<const int32*>(Result.token_ids), static_cast<int32>(Result.len));
	}
	tokenizers_free_encode_results(&Result, 1);
	return Ids;
}

FString UTokenizerWrapper::Decode(const TArray<int32>& Ids, bool bSkipSpecialTokens)
{
	LastError.Reset();

	if (Tokenizer == nullptr)
	{
		SetError(TEXT("Decode: tokenizer not initialized"));
		return FString();
	}

	for (int32 Index = 0; Index < Ids.Num(); ++Index)
	{
		if (Ids[Index] < 0)
		{
			SetError(FString::Printf(TEXT("Decode: id %d at index %d is negative"), Ids[Index], Index));
			return FString();
		}
	}

	// Non-negative int32 values are identical as uint32.
	const int DecodeStatus = tokenizers_decode(Tokenizer,
		Ids.Num() > 0 ? reinterpret_cast<const uint32_t*>(Ids.GetData()) : nullptr,
		static_cast<size_t>(Ids.Num()), bSkipSpecialTokens ? 1 : 0);
	if (DecodeStatus != TOKENIZERS_OK)
	{
		SetError(FString::Printf(TEXT("Decode: tokenizers_decode failed (%d): %s"),
			DecodeStatus, *UE::Tokenizers::Private::LibraryError()));
		return FString();
	}

	// Data points into the handle (valid until the next call on it) and is not null-terminated.
	const char* Data = nullptr;
	size_t Len = 0;
	const int StrStatus = tokenizers_get_decode_str(Tokenizer, &Data, &Len);
	if (StrStatus != TOKENIZERS_OK)
	{
		SetError(FString::Printf(TEXT("Decode: tokenizers_get_decode_str failed (%d): %s"),
			StrStatus, *UE::Tokenizers::Private::LibraryError()));
		return FString();
	}
	if (Len > static_cast<size_t>(MAX_int32))
	{
		SetError(FString::Printf(TEXT("Decode: decoded text is %llu bytes, more than a string can hold"),
			static_cast<unsigned long long>(Len)));
		return FString();
	}
	return UE::Tokenizers::Private::Utf8ToFString(Data, Len);
}

FString UTokenizerWrapper::GetLastError() const
{
	return LastError;
}
