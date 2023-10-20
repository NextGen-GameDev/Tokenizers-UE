// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "TokenizerWrapper.h"

#include <TokenizersLibrary/tokenizers_c.h>

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
	std::string const TextStd(TCHAR_TO_UTF8(*Text)); // FString to std::string
	tokenizers_encode(Tokenizer, TextStd.data(), TextStd.length(), 0);
	const uint32_t* data;
	size_t len;
	tokenizers_get_encode_ids(Tokenizer, &data, &len);
	std::vector<int32_t> const Encoded = std::vector<int32_t>(data, data + len);
	return ConvertVectorToTArray(Encoded);

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