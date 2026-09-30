// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "TokenizerWrapper.h"

#include <TokenizersLibrary/tokenizers_c.h>
#include <vector>
#include <string>
#include "Containers/Array.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"

UTokenizerWrapper::UTokenizerWrapper()

{
	Tokenizer = nullptr;
}


UTokenizerWrapper::~UTokenizerWrapper()
{
	if (Tokenizer != nullptr)
	{
		tokenizers_free(Tokenizer);
	}
}


TArray<int32> ConvertVectorToTArray(const std::vector<int32_t>& vec)
{
	TArray arr(vec.data(), vec.size());
	return arr;
}

std::vector<int32_t> ConvertTArrayToVector(const TArray<int32>& arr)
{
	std::vector<int32_t> vec(arr.GetData(), arr.GetData() + arr.Num());
	return vec;
}


void UTokenizerWrapper::InitializeTokenizerFromJson(FString const JsonBlob)
{
	std::string const JsonBlobStd(TCHAR_TO_UTF8(*JsonBlob));
	Tokenizer = tokenizers_new_from_str(JsonBlobStd.data(), JsonBlobStd.length());

}

bool UTokenizerWrapper::InitializeTokenizerFromFile(FString const FileName)

{
	
	// get full path in module content directory
	TSharedPtr<IPlugin> const TokPlugin = IPluginManager::Get().FindPlugin("Tokenizers");


	if (!TokPlugin.IsValid())
	{
		return false;
	}
	FString PluginContentDir = TokPlugin->GetContentDir();
	FString const FilePath = FPaths::Combine(PluginContentDir, FileName);
	


	// check if file exists
	if (!FPaths::FileExists(FilePath))
	{
		return false;
	}
	std::string const FilePathStd(TCHAR_TO_UTF8(*FilePath));
	// read file
	FString JsonBlob;
	FFileHelper::LoadFileToString(JsonBlob, *FilePath);
	// initialize tokenizer
	InitializeTokenizerFromJson(JsonBlob);
	return true;
}




TArray<int32> UTokenizerWrapper::Encode(FString Text)
{
	TArray<int32> Ids;
	if (Tokenizer == nullptr)
	{
		return Ids;
	}

	// UTF-8 bytes with an explicit length; no terminator is passed.
	FTCHARToUTF8 Utf8(*Text);

	// token_ids is allocated by the library; free it only with tokenizers_free_encode_results.
	TokenizerEncodeResult Result{};
	const int Status = tokenizers_encode(Tokenizer, Utf8.Get(), static_cast<size_t>(Utf8.Length()), 0, &Result);
	// A result longer than TArray<int32> can index is dropped (empty result), never truncated.
	if (Status == TOKENIZERS_OK && Result.token_ids != nullptr && Result.len > 0
		&& Result.len <= static_cast<size_t>(MAX_int32))
	{
		Ids.Append(reinterpret_cast<const int32*>(Result.token_ids), static_cast<int32>(Result.len));
	}
	tokenizers_free_encode_results(&Result, 1);
	return Ids;
}

FString UTokenizerWrapper::Decode(TArray<int32> Ids)
{
	std::vector<int32_t> const IdsStd = ConvertTArrayToVector(Ids);
	tokenizers_decode(Tokenizer, reinterpret_cast<const uint32_t*>(IdsStd.data()), IdsStd.size(), 0);
	const char* data;
	size_t len;
	tokenizers_get_decode_str(Tokenizer, &data, &len);
	
	return FString(data);
}