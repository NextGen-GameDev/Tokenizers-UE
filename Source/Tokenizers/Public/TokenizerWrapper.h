// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include <TokenizersLibrary/tokenizers_c.h>
#include <TokenizerWrapper.generated.h>

/**
 * 
 */
UCLASS(Blueprintable, BlueprintType, DisplayName = "Tokenizer Wrapper")
class TOKENIZERS_API UTokenizerWrapper : public UObject
{
	GENERATED_BODY()

public:

	UTokenizerWrapper();

	~UTokenizerWrapper();

	UFUNCTION(BlueprintCallable, Category = "Tokenizer")
	void InitializeTokenizerFromJson(FString const JsonBlob);

	UFUNCTION(BlueprintCallable, Category = "Tokenizer")
	bool InitializeTokenizerFromFile(FString const FilePath);

	UFUNCTION(BlueprintCallable, Category = "Tokenizer")
	TArray<int32> Encode(FString Text);

	UFUNCTION(BlueprintCallable, Category = "Tokenizer")
	FString Decode(TArray<int32> Ids);

private:
	TokenizerHandle Tokenizer;
	

};
