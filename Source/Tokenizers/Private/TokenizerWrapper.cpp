// Copyright NextGen-GameDev. Licensed under the Apache License 2.0.

#include "TokenizerWrapper.h"

#include "TokenizersModule.h"
#include "GenericPlatform/GenericPlatformString.h"
#include "HAL/PlatformString.h"
#include "Interfaces/IPluginManager.h"
#include "Math/NumericLimits.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

THIRD_PARTY_INCLUDES_START
#include <TokenizersLibrary/tokenizers_c.h>
THIRD_PARTY_INCLUDES_END

#include UE_INLINE_GENERATED_CPP_BY_NAME(TokenizerWrapper)

static_assert(sizeof(SIZE_T) == sizeof(size_t), "SIZE_T and size_t must have the same size");

namespace UE::Tokenizers::Private
{
	/** Converts UTF-8 bytes with an explicit length (never null-terminated) to an FString.
	    Every byte counts, including a trailing 0. */
	static FString Utf8ToFString(const char* Data, SIZE_T Len)
	{
		if (Data == nullptr || Len == 0 || Len > static_cast<SIZE_T>(MAX_int32))
		{
			return FString();
		}
		return FString::ConstructFromPtrSize(reinterpret_cast<const UTF8CHAR*>(Data), static_cast<int32>(Len));
	}

	/** Converts exactly SrcLen TCHARs to UTF-8 (a trailing 0 is kept, unlike FTCHARToUTF8).
	    Out holds the bytes, no terminator. Returns false if the conversion failed. */
	static bool TCharToUtf8(const TCHAR* Src, int32 SrcLen, TArray<UTF8CHAR>& Out)
	{
		Out.Reset();
		if (Src == nullptr || SrcLen <= 0)
		{
			return true;
		}
		const int32 DestLen = FPlatformString::ConvertedLength<UTF8CHAR>(Src, SrcLen);
		if (DestLen <= 0)
		{
			return DestLen == 0;
		}
		Out.SetNumUninitialized(DestLen);
		const UTF8CHAR* End = FPlatformString::Convert(Out.GetData(), DestLen, Src, SrcLen);
		if (End == nullptr)
		{
			Out.Reset();
			return false;
		}
		Out.SetNum(static_cast<int32>(End - Out.GetData()), EAllowShrinking::No);
		return true;
	}

	/** Pointer for the C API: (NULL, 0) for an empty buffer. */
	static const char* Utf8Data(const TArray<UTF8CHAR>& Bytes)
	{
		return Bytes.Num() > 0 ? reinterpret_cast<const char*>(Bytes.GetData()) : nullptr;
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
	FScopeLock Lock(&Mutex);
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

bool UTokenizerWrapper::InitializeFromUtf8(const char* Data, SIZE_T Len, const TCHAR* FunctionName, const FString& Context)
{
	TokenizerHandle NewHandle = tokenizers_new_from_str(Data, Len);
	if (NewHandle == nullptr)
	{
		SetError(FString::Printf(TEXT("%s: failed to parse tokenizer JSON%s: %s"),
			FunctionName, *Context, *UE::Tokenizers::Private::LibraryError()));
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
	FScopeLock Lock(&Mutex);
	LastError.Reset();

	// A leading U+FEFF (byte order mark) is skipped; the parser rejects it.
	const int32 Skip = (JsonBlob.Len() > 0 && JsonBlob[0] == TCHAR(0xFEFF)) ? 1 : 0;
	TArray<UTF8CHAR> Utf8;
	if (!UE::Tokenizers::Private::TCharToUtf8(*JsonBlob + Skip, JsonBlob.Len() - Skip, Utf8))
	{
		SetError(TEXT("InitializeTokenizerFromJson: could not convert the JSON to UTF-8"));
		return false;
	}
	return InitializeFromUtf8(UE::Tokenizers::Private::Utf8Data(Utf8), static_cast<SIZE_T>(Utf8.Num()),
		TEXT("InitializeTokenizerFromJson"), FString());
}

bool UTokenizerWrapper::InitializeTokenizerFromFile(const FString& FilePath)
{
	// Resolve and read the file without the lock, so file IO never holds it.
	// Only locals are touched here; members are written below, under the lock.
	FString PendingError;
	FString FoundPath;
	TArray<uint8> Bytes;

	if (FilePath.IsEmpty())
	{
		PendingError = TEXT("InitializeTokenizerFromFile: file path is empty");
	}
	else
	{
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
			PendingError = FString::Printf(TEXT("InitializeTokenizerFromFile: file '%s' not found; tried: %s"),
				*FilePath, *FString::Join(Candidates, TEXT(", ")));
		}
		else
		{
			FoundPath = *Found;
			if (!FFileHelper::LoadFileToArray(Bytes, *FoundPath))
			{
				PendingError = FString::Printf(TEXT("InitializeTokenizerFromFile: could not read '%s'"), *FoundPath);
			}
		}
	}

	FScopeLock Lock(&Mutex);
	LastError.Reset();

	if (!PendingError.IsEmpty())
	{
		SetError(MoveTemp(PendingError));
		return false;
	}

	// A leading UTF-8 BOM (EF BB BF) is skipped; every other byte goes to the parser unchanged.
	int32 Offset = 0;
	if (Bytes.Num() >= 3 && Bytes[0] == 0xEF && Bytes[1] == 0xBB && Bytes[2] == 0xBF)
	{
		Offset = 3;
	}

	const SIZE_T Len = static_cast<SIZE_T>(Bytes.Num() - Offset);
	return InitializeFromUtf8(Len > 0 ? reinterpret_cast<const char*>(Bytes.GetData() + Offset) : nullptr, Len,
		TEXT("InitializeTokenizerFromFile"), FString::Printf(TEXT(" (file '%s')"), *FoundPath));
}

bool UTokenizerWrapper::IsInitialized() const
{
	FScopeLock Lock(&Mutex);
	return Tokenizer != nullptr;
}

TArray<int32> UTokenizerWrapper::Encode(const FString& Text, bool bAddSpecialTokens)
{
	FScopeLock Lock(&Mutex);
	LastError.Reset();

	TArray<int32> Ids;
	if (Tokenizer == nullptr)
	{
		SetError(TEXT("Encode: tokenizer not initialized"));
		return Ids;
	}

	// UTF-8 bytes with an explicit length (every TCHAR, including a trailing 0); no terminator is passed.
	TArray<UTF8CHAR> Utf8;
	if (!UE::Tokenizers::Private::TCharToUtf8(*Text, Text.Len(), Utf8))
	{
		SetError(TEXT("Encode: could not convert the text to UTF-8"));
		return Ids;
	}

	// token_ids is allocated by the library; free it only with tokenizers_free_encode_results.
	TokenizerEncodeResult Result{};
	const int32 Status = tokenizers_encode(Tokenizer, UE::Tokenizers::Private::Utf8Data(Utf8),
		static_cast<size_t>(Utf8.Num()), bAddSpecialTokens ? 1 : 0, &Result);
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
	FScopeLock Lock(&Mutex);
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
	const int32 DecodeStatus = tokenizers_decode(Tokenizer,
		Ids.Num() > 0 ? reinterpret_cast<const uint32_t*>(Ids.GetData()) : nullptr,
		static_cast<size_t>(Ids.Num()), bSkipSpecialTokens ? 1 : 0);
	if (DecodeStatus != TOKENIZERS_OK)
	{
		SetError(FString::Printf(TEXT("Decode: tokenizers_decode failed (%d): %s"),
			DecodeStatus, *UE::Tokenizers::Private::LibraryError()));
		return FString();
	}

	// Data points into the handle (valid until the next call on it, so it is copied under the lock)
	// and is not null-terminated.
	const char* Data = nullptr;
	size_t Len = 0;
	const int32 StrStatus = tokenizers_get_decode_str(Tokenizer, &Data, &Len);
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

bool UTokenizerWrapper::EncodeBatch(const TArray<FString>& Texts, FTokenizedBatch& OutBatch,
	bool bAddSpecialTokens, int32 MaxLength, int32 PadTokenId)
{
	FScopeLock Lock(&Mutex);
	LastError.Reset();
	OutBatch = FTokenizedBatch();

	if (Tokenizer == nullptr)
	{
		SetError(TEXT("EncodeBatch: tokenizer not initialized"));
		return false;
	}
	if (MaxLength < 0)
	{
		SetError(FString::Printf(TEXT("EncodeBatch: MaxLength %d is negative (0 = no truncation)"), MaxLength));
		return false;
	}
	if (PadTokenId < 0)
	{
		SetError(FString::Printf(TEXT("EncodeBatch: PadTokenId %d is negative"), PadTokenId));
		return false;
	}

	const int32 NumTexts = Texts.Num();
	if (NumTexts == 0)
	{
		return true;
	}

	// One UTF-8 buffer per text, alive until the C call returns. Exact lengths, a trailing 0 is kept.
	TArray<TArray<UTF8CHAR>> Buffers;
	Buffers.SetNum(NumTexts);
	TArray<const char*> DataPtrs;
	DataPtrs.SetNumUninitialized(NumTexts);
	TArray<SIZE_T> Lens;
	Lens.SetNumUninitialized(NumTexts);
	for (int32 Row = 0; Row < NumTexts; ++Row)
	{
		if (!UE::Tokenizers::Private::TCharToUtf8(*Texts[Row], Texts[Row].Len(), Buffers[Row]))
		{
			SetError(FString::Printf(TEXT("EncodeBatch: could not convert text %d to UTF-8"), Row));
			return false;
		}
		DataPtrs[Row] = UE::Tokenizers::Private::Utf8Data(Buffers[Row]);
		Lens[Row] = static_cast<SIZE_T>(Buffers[Row].Num());
	}

	// Each results[i].token_ids is allocated by the library; freed below with tokenizers_free_encode_results.
	TArray<TokenizerEncodeResult> Results;
	Results.SetNumZeroed(NumTexts);
	const int32 Status = tokenizers_encode_batch_truncated(Tokenizer, DataPtrs.GetData(),
		reinterpret_cast<size_t*>(Lens.GetData()), static_cast<size_t>(NumTexts), bAddSpecialTokens ? 1 : 0,
		static_cast<size_t>(MaxLength), Results.GetData());
	if (Status != TOKENIZERS_OK)
	{
		SetError(FString::Printf(TEXT("EncodeBatch: tokenizers_encode_batch_truncated failed (%d): %s"),
			Status, *UE::Tokenizers::Private::LibraryError()));
		tokenizers_free_encode_results(Results.GetData(), static_cast<size_t>(NumTexts));
		return false;
	}

	size_t Longest = 0;
	for (const TokenizerEncodeResult& Result : Results)
	{
		const size_t RowLen = Result.token_ids != nullptr ? Result.len : 0;
		Longest = RowLen > Longest ? RowLen : Longest;
	}

	const uint64 Total = static_cast<uint64>(NumTexts) * static_cast<uint64>(Longest);
	if (Longest > static_cast<size_t>(MAX_int32) || Total > static_cast<uint64>(MAX_int32))
	{
		SetError(FString::Printf(TEXT("EncodeBatch: %d rows x %llu ids is more than an array can hold"),
			NumTexts, static_cast<unsigned long long>(Longest)));
		tokenizers_free_encode_results(Results.GetData(), static_cast<size_t>(NumTexts));
		return false;
	}

	const int32 SeqLen = static_cast<int32>(Longest);
	FTokenizedBatch Batch;
	Batch.NumRows = NumTexts;
	Batch.SeqLen = SeqLen;
	Batch.InputIds.Init(PadTokenId, static_cast<int32>(Total));
	Batch.AttentionMask.Init(0, static_cast<int32>(Total));
	for (int32 Row = 0; Row < NumTexts; ++Row)
	{
		const TokenizerEncodeResult& Result = Results[Row];
		if (Result.token_ids == nullptr || Result.len == 0)
		{
			continue;
		}
		const int32 RowLen = static_cast<int32>(Result.len);
		const int32 Base = Row * SeqLen;
		FMemory::Memcpy(Batch.InputIds.GetData() + Base, Result.token_ids, sizeof(int32) * RowLen);
		for (int32 Col = 0; Col < RowLen; ++Col)
		{
			Batch.AttentionMask[Base + Col] = 1;
		}
	}
	tokenizers_free_encode_results(Results.GetData(), static_cast<size_t>(NumTexts));

	OutBatch = MoveTemp(Batch);
	return true;
}

int32 UTokenizerWrapper::GetVocabSize()
{
	FScopeLock Lock(&Mutex);
	LastError.Reset();

	if (Tokenizer == nullptr)
	{
		SetError(TEXT("GetVocabSize: tokenizer not initialized"));
		return -1;
	}

	size_t Size = 0;
	const int32 Status = tokenizers_get_vocab_size(Tokenizer, &Size);
	if (Status != TOKENIZERS_OK)
	{
		SetError(FString::Printf(TEXT("GetVocabSize: tokenizers_get_vocab_size failed (%d): %s"),
			Status, *UE::Tokenizers::Private::LibraryError()));
		return -1;
	}
	return Size > static_cast<size_t>(MAX_int32) ? MAX_int32 : static_cast<int32>(Size);
}

FString UTokenizerWrapper::GetLastError() const
{
	FScopeLock Lock(&Mutex);
	return LastError;
}
