// WP3.8b tests: a tokenizer.json's own `padding` block must not leak into Encode/EncodeBatch (contract C24 + C6).
// Expected values come from Python `tokenizers` 0.22.2 with no_padding() (see docs/briefs/WP3.8b.md), not from the code under test.
// MiniLM tokenizer: <ProjectDir>/../.pipelines-dev/models/reference/all-MiniLM-L6-v2/tokenizer.json
// (padding Fixed 128, truncation 128); fetch with Tools/fetch_reference_models.py.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/StrongObjectPtr.h"
#include "TokenizerWrapper.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TokenizerPaddingSpecPrivate
{
	// Tiny WordLevel tokenizer ([UNK]=0 hello=1 world=2 [CLS]=3 [SEP]=4), template "[CLS] $A [SEP]",
	// with its own padding block: BatchLongest, pad_to_multiple_of 8, pad id 0.
	static const char* GPaddedJson = R"JSON({"version":"1.0","truncation":null,"padding":{"strategy":"BatchLongest","direction":"Right","pad_to_multiple_of":8,"pad_id":0,"pad_type_id":0,"pad_token":"[UNK]"},"added_tokens":[{"id":3,"content":"[CLS]","single_word":false,"lstrip":false,"rstrip":false,"normalized":false,"special":true},{"id":4,"content":"[SEP]","single_word":false,"lstrip":false,"rstrip":false,"normalized":false,"special":true}],"normalizer":null,"pre_tokenizer":{"type":"Whitespace"},"post_processor":{"type":"TemplateProcessing","single":[{"SpecialToken":{"id":"[CLS]","type_id":0}},{"Sequence":{"id":"A","type_id":0}},{"SpecialToken":{"id":"[SEP]","type_id":0}}],"pair":[{"SpecialToken":{"id":"[CLS]","type_id":0}},{"Sequence":{"id":"A","type_id":0}},{"SpecialToken":{"id":"[SEP]","type_id":0}},{"Sequence":{"id":"B","type_id":1}},{"SpecialToken":{"id":"[SEP]","type_id":1}}],"special_tokens":{"[CLS]":{"id":"[CLS]","ids":[3],"tokens":["[CLS]"]},"[SEP]":{"id":"[SEP]","ids":[4],"tokens":["[SEP]"]}}},"decoder":null,"model":{"type":"WordLevel","vocab":{"[UNK]":0,"hello":1,"world":2,"[CLS]":3,"[SEP]":4},"unk_token":"[UNK]"}})JSON";

	static FString MiniLMPath()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../.pipelines-dev/models/reference/all-MiniLM-L6-v2/tokenizer.json"));
	}

	static FString IdsToString(const TArray<int32>& Ids)
	{
		FString Out = TEXT("[");
		for (int32 i = 0; i < Ids.Num(); ++i)
		{
			Out += FString::Printf(TEXT("%s%d"), i ? TEXT(", ") : TEXT(""), Ids[i]);
		}
		return Out + TEXT("]");
	}
}

BEGIN_DEFINE_SPEC(FTokenizerPaddingSpec, "Tokenizers.Padding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	TStrongObjectPtr<UTokenizerWrapper> Tok;

	/** Loads the MiniLM tokenizer into Tok; AddError naming the path and the fetch script on failure. */
	bool LoadMiniLM()
	{
		const FString Path = TokenizerPaddingSpecPrivate::MiniLMPath();
		Tok.Reset(NewObject<UTokenizerWrapper>(GetTransientPackage()));
		if (!FPaths::FileExists(Path))
		{
			AddError(FString::Printf(TEXT("MiniLM tokenizer missing: %s. Fetch it with Tools/fetch_reference_models.py"), *Path));
			return false;
		}
		if (!Tok->InitializeTokenizerFromFile(Path))
		{
			AddError(FString::Printf(TEXT("InitializeTokenizerFromFile(%s) failed: %s"), *Path, *Tok->GetLastError()));
			return false;
		}
		return true;
	}

	void CheckIds(const FString& What, const TArray<int32>& Actual, const TArray<int32>& Expected)
	{
		TestTrue(FString::Printf(TEXT("%s: expected=%s actual=%s"), *What,
			*TokenizerPaddingSpecPrivate::IdsToString(Expected), *TokenizerPaddingSpecPrivate::IdsToString(Actual)),
			Actual == Expected);
	}

	void CheckBatch(const FString& What, bool bReturned, const FTokenizedBatch& Out, int32 Rows, int32 Len,
		const TArray<int32>& Ids, const TArray<int32>& Mask)
	{
		TestTrue(What + TEXT(": EncodeBatch should return true (") + Tok->GetLastError() + TEXT(")"), bReturned);
		TestEqual(What + TEXT(": NumRows"), Out.NumRows, Rows);
		TestEqual(What + TEXT(": SeqLen"), Out.SeqLen, Len);
		CheckIds(What + TEXT(": InputIds"), Out.InputIds, Ids);
		CheckIds(What + TEXT(": AttentionMask"), Out.AttentionMask, Mask);
	}
END_DEFINE_SPEC(FTokenizerPaddingSpec)

void FTokenizerPaddingSpec::Define()
{
	AfterEach([this]()
	{
		Tok.Reset();
	});

	Describe("MiniLMFixed128", [this]()
	{
		It("EncodeWithSpecialsIsUnpadded", [this]()
		{
			if (!LoadMiniLM()) { return; }
			CheckIds(TEXT("Encode(specials=true)"), Tok->Encode(TEXT("Where is the blacksmith?"), true),
				{ 101, 2073, 2003, 1996, 20987, 1029, 102 });
		});

		It("EncodeWithoutSpecialsIsUnpadded", [this]()
		{
			if (!LoadMiniLM()) { return; }
			CheckIds(TEXT("Encode(specials=false)"), Tok->Encode(TEXT("Where is the blacksmith?"), false),
				{ 2073, 2003, 1996, 20987, 1029 });
		});

		It("EncodeBatchPadsToLongestRowOnly", [this]()
		{
			if (!LoadMiniLM()) { return; }
			FTokenizedBatch Out;
			const bool bOk = Tok->EncodeBatch({ TEXT("Where is the blacksmith?"), TEXT("Hi") }, Out, true, 0, 0);
			CheckBatch(TEXT("MiniLM batch, no MaxLength"), bOk, Out, 2, 7,
				{ 101, 2073, 2003, 1996, 20987, 1029, 102,   101, 7632, 102, 0, 0, 0, 0 },
				{ 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 0, 0, 0, 0 });
		});

		It("EncodeBatchTruncatedToMaxLength5", [this]()
		{
			if (!LoadMiniLM()) { return; }
			FTokenizedBatch Out;
			const bool bOk = Tok->EncodeBatch({ TEXT("Where is the blacksmith?"), TEXT("Hi") }, Out, true, 5, 0);
			CheckBatch(TEXT("MiniLM batch, MaxLength 5"), bOk, Out, 2, 5,
				{ 101, 2073, 2003, 1996, 102,   101, 7632, 102, 0, 0 },
				{ 1, 1, 1, 1, 1,   1, 1, 1, 0, 0 });
		});

		It("RepeatedCallsStayUnpadded", [this]()
		{
			// The library clears the file padding per call and restores it; a restore bug would show up on the second call.
			if (!LoadMiniLM()) { return; }
			for (int32 Pass = 0; Pass < 3; ++Pass)
			{
				FTokenizedBatch Out;
				const bool bOk = Tok->EncodeBatch({ TEXT("Where is the blacksmith?"), TEXT("Hi") }, Out, true, 0, 0);
				CheckBatch(FString::Printf(TEXT("MiniLM batch pass %d"), Pass), bOk, Out, 2, 7,
					{ 101, 2073, 2003, 1996, 20987, 1029, 102,   101, 7632, 102, 0, 0, 0, 0 },
					{ 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 0, 0, 0, 0 });
				CheckIds(FString::Printf(TEXT("Encode pass %d"), Pass), Tok->Encode(TEXT("Where is the blacksmith?"), true),
					{ 101, 2073, 2003, 1996, 20987, 1029, 102 });
			}
		});
	});

	Describe("InlineBatchLongestMultipleOf8", [this]()
	{
		It("EncodeBatchSeqLenIsLongestRealRow", [this]()
		{
			Tok.Reset(NewObject<UTokenizerWrapper>(GetTransientPackage()));
			if (!TestTrue(TEXT("init padded inline tokenizer"),
				Tok->InitializeTokenizerFromJson(FString(UTF8_TO_TCHAR(TokenizerPaddingSpecPrivate::GPaddedJson)))))
			{
				AddError(Tok->GetLastError());
				return;
			}
			FTokenizedBatch Out;
			const bool bOk = Tok->EncodeBatch({ TEXT("hello world hello"), TEXT("hello") }, Out, true, 0, 0);
			CheckBatch(TEXT("inline BatchLongest/8"), bOk, Out, 2, 5,
				{ 3, 1, 2, 1, 4,   3, 1, 4, 0, 0 },
				{ 1, 1, 1, 1, 1,   1, 1, 1, 0, 0 });
			TestNotEqual(TEXT("SeqLen must not be rounded to a multiple of 8"), Out.SeqLen % 8, 0);
		});

		It("EncodeIsUnpadded", [this]()
		{
			Tok.Reset(NewObject<UTokenizerWrapper>(GetTransientPackage()));
			if (!TestTrue(TEXT("init padded inline tokenizer"),
				Tok->InitializeTokenizerFromJson(FString(UTF8_TO_TCHAR(TokenizerPaddingSpecPrivate::GPaddedJson)))))
			{
				AddError(Tok->GetLastError());
				return;
			}
			CheckIds(TEXT("inline Encode(specials=true)"), Tok->Encode(TEXT("hello world"), true), { 3, 1, 2, 4 });
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
