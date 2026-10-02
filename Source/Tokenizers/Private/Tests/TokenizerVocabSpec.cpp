// WP3.6a tests for UTokenizerWrapper::GetVocabSize. Written from the brief's Contract/Acceptance, not from the implementation.
// Expected sizes are read at runtime from the goldens' tokenizer.vocab_size_with_added (Python `tokenizers` get_vocab_size(True)).
// Goldens: $TOKENIZERS_GOLDENS_DIR or <ProjectDir>/../.pipelines-dev/goldens/tokenizers (make with Tools/make_tokenizer_goldens.py).

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformMisc.h"
#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/StrongObjectPtr.h"
#include "TokenizerWrapper.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TokenizerVocabSpecPrivate
{
	struct FVocabGolden
	{
		FString Name;
		FString GoldenPath;
		FString TokenizerPath;
		int32 Expected = -1;
	};

	static FString GoldensDir()
	{
		const FString Env = FPlatformMisc::GetEnvironmentVariable(TEXT("TOKENIZERS_GOLDENS_DIR"));
		if (!Env.IsEmpty())
		{
			return FPaths::ConvertRelativePathToFull(Env);
		}
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../.pipelines-dev/goldens/tokenizers"));
	}

	static bool LoadGolden(const FString& Name, FVocabGolden& Out, FString& OutError)
	{
		Out.Name = Name;
		Out.GoldenPath = GoldensDir() / (Name + TEXT(".json"));
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Out.GoldenPath))
		{
			OutError = FString::Printf(TEXT("golden file missing: %s. Generate with Tools/make_tokenizer_goldens.py and fetch models with Tools/fetch_models.py (see CLAUDE.md)"), *Out.GoldenPath);
			return false;
		}
		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = FString::Printf(TEXT("golden %s is not valid JSON"), *Out.GoldenPath);
			return false;
		}
		const TSharedPtr<FJsonObject>* TokObj = nullptr;
		FString RelFile;
		double Size = -1.0;
		if (!Root->TryGetObjectField(TEXT("tokenizer"), TokObj) || !TokObj
			|| !(*TokObj)->TryGetStringField(TEXT("file"), RelFile)
			|| !(*TokObj)->TryGetNumberField(TEXT("vocab_size_with_added"), Size))
		{
			OutError = FString::Printf(TEXT("golden %s lacks tokenizer.file or tokenizer.vocab_size_with_added"), *Out.GoldenPath);
			return false;
		}
		Out.TokenizerPath = FPaths::ConvertRelativePathToFull(FPaths::GetPath(Out.GoldenPath) / RelFile);
		Out.Expected = (int32)Size;
		return true;
	}

	static UTokenizerWrapper* LoadTokenizer(const FVocabGolden& G, TStrongObjectPtr<UTokenizerWrapper>& Holder, FString& OutError)
	{
		Holder.Reset(NewObject<UTokenizerWrapper>(GetTransientPackage()));
		if (!Holder->InitializeTokenizerFromFile(G.TokenizerPath))
		{
			OutError = FString::Printf(TEXT("InitializeTokenizerFromFile(%s) failed: %s. Fetch models with Tools/fetch_models.py (see CLAUDE.md)"),
				*G.TokenizerPath, *Holder->GetLastError());
			return nullptr;
		}
		return Holder.Get();
	}

	// WordLevel with 2 model tokens ([UNK], a) plus 1 added token ([PAD], id 2, not in the model vocab): size with added = 3.
	static const char* GTinyJson = R"JSON({"version":"1.0","truncation":null,"padding":null,"added_tokens":[{"id":2,"content":"[PAD]","single_word":false,"lstrip":false,"rstrip":false,"normalized":false,"special":true}],"normalizer":null,"pre_tokenizer":{"type":"Whitespace"},"post_processor":null,"decoder":null,"model":{"type":"WordLevel","vocab":{"[UNK]":0,"a":1},"unk_token":"[UNK]"}})JSON";
}

BEGIN_DEFINE_SPEC(FTokenizerVocabSpec, "Tokenizers.Vocab",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	TStrongObjectPtr<UTokenizerWrapper> Wrapper;

	void CheckGoldenSize(const TCHAR* Name)
	{
		using namespace TokenizerVocabSpecPrivate;
		FVocabGolden G;
		FString Err;
		if (!LoadGolden(Name, G, Err)) { AddError(Err); return; }
		UTokenizerWrapper* Tok = LoadTokenizer(G, Wrapper, Err);
		if (!Tok) { AddError(Err); return; }
		const int32 Size = Tok->GetVocabSize();
		TestEqual(*FString::Printf(TEXT("%s GetVocabSize (golden %s, expected %d)"), Name, *G.GoldenPath, G.Expected), Size, G.Expected);
		TestEqual(*FString::Printf(TEXT("%s GetLastError after success"), Name), Tok->GetLastError(), FString());
	}
END_DEFINE_SPEC(FTokenizerVocabSpec)

void FTokenizerVocabSpec::Define()
{
	using namespace TokenizerVocabSpecPrivate;

	AfterEach([this]()
	{
		Wrapper.Reset();
	});

	Describe("Goldens", [this]()
	{
		// A getter that returned the model vocab only (get_vocab_size(false)) or the highest id would miss added tokens;
		// t5-small has 100 extra_id sentinels in added tokens, gpt2 has <|endoftext|>.
		It("bert-base-uncased matches golden vocab_size_with_added (30522)", [this]() { CheckGoldenSize(TEXT("bert-base-uncased")); });
		It("gpt2 matches golden vocab_size_with_added (50257)", [this]() { CheckGoldenSize(TEXT("gpt2")); });
		It("t5-small matches golden vocab_size_with_added (32100)", [this]() { CheckGoldenSize(TEXT("t5-small")); });

		It("golden values are the ones the brief states (guards a stale golden)", [this]()
		{
			const TPair<const TCHAR*, int32> Known[] = { {TEXT("bert-base-uncased"), 30522}, {TEXT("gpt2"), 50257}, {TEXT("t5-small"), 32100} };
			for (const TPair<const TCHAR*, int32>& K : Known)
			{
				FVocabGolden G;
				FString Err;
				if (!LoadGolden(K.Key, G, Err)) { AddError(Err); continue; }
				TestEqual(*FString::Printf(TEXT("%s golden vocab_size_with_added"), K.Key), G.Expected, K.Value);
			}
		});
	});

	Describe("Inline tokenizer", [this]()
	{
		It("counts added tokens outside the model vocab (2 model + 1 added = 3)", [this]()
		{
			// Catches a getter that reports only model tokens (2) or max id (2).
			Wrapper.Reset(NewObject<UTokenizerWrapper>(GetTransientPackage()));
			TestTrue(TEXT("init from json"), Wrapper->InitializeTokenizerFromJson(FString(UTF8_TO_TCHAR(GTinyJson))));
			TestEqual(TEXT("GetVocabSize"), Wrapper->GetVocabSize(), 3);
			TestEqual(TEXT("GetLastError"), Wrapper->GetLastError(), FString());
		});
	});

	Describe("Errors", [this]()
	{
		It("not initialized returns -1, sets GetLastError and logs one warning", [this]()
		{
			// The log text is not specified by the brief; the warning is expected to mention the function name.
			AddExpectedError(TEXT("GetVocabSize"), EAutomationExpectedErrorFlags::Contains, 1);
			Wrapper.Reset(NewObject<UTokenizerWrapper>(GetTransientPackage()));
			TestEqual(TEXT("GetVocabSize on fresh object"), Wrapper->GetVocabSize(), -1);
			TestTrue(TEXT("GetLastError non-empty"), !Wrapper->GetLastError().IsEmpty());
		});

		It("a later successful call clears the error", [this]()
		{
			AddExpectedError(TEXT("GetVocabSize"), EAutomationExpectedErrorFlags::Contains, 1);
			Wrapper.Reset(NewObject<UTokenizerWrapper>(GetTransientPackage()));
			TestEqual(TEXT("first call -1"), Wrapper->GetVocabSize(), -1);
			TestTrue(TEXT("error set"), !Wrapper->GetLastError().IsEmpty());
			TestTrue(TEXT("init"), Wrapper->InitializeTokenizerFromJson(FString(UTF8_TO_TCHAR(GTinyJson))));
			TestEqual(TEXT("GetVocabSize after init"), Wrapper->GetVocabSize(), 3);
			TestEqual(TEXT("GetLastError reset by successful call"), Wrapper->GetLastError(), FString());
		});

		It("failed re-init keeps the previous tokenizer and its size", [this]()
		{
			AddExpectedError(TEXT("InitializeTokenizerFromJson"), EAutomationExpectedErrorFlags::Contains, 0);
			Wrapper.Reset(NewObject<UTokenizerWrapper>(GetTransientPackage()));
			TestTrue(TEXT("init"), Wrapper->InitializeTokenizerFromJson(FString(UTF8_TO_TCHAR(GTinyJson))));
			TestFalse(TEXT("bad json rejected"), Wrapper->InitializeTokenizerFromJson(TEXT("{not json")));
			TestEqual(TEXT("size unchanged after failed re-init"), Wrapper->GetVocabSize(), 3);
			TestEqual(TEXT("GetLastError after successful GetVocabSize"), Wrapper->GetLastError(), FString());
		});
	});

	Describe("Re-initialization", [this]()
	{
		It("returns the new size after loading another tokenizer (bert then gpt2 then t5)", [this]()
		{
			// Catches a cached size from the first Initialize.
			FVocabGolden Bert, Gpt2, T5;
			FString Err;
			if (!LoadGolden(TEXT("bert-base-uncased"), Bert, Err)) { AddError(Err); return; }
			if (!LoadGolden(TEXT("gpt2"), Gpt2, Err)) { AddError(Err); return; }
			if (!LoadGolden(TEXT("t5-small"), T5, Err)) { AddError(Err); return; }
			UTokenizerWrapper* Tok = LoadTokenizer(Bert, Wrapper, Err);
			if (!Tok) { AddError(Err); return; }
			TestEqual(TEXT("bert size"), Tok->GetVocabSize(), Bert.Expected);
			if (!Tok->InitializeTokenizerFromFile(Gpt2.TokenizerPath)) { AddError(Tok->GetLastError()); return; }
			TestEqual(TEXT("gpt2 size after re-init"), Tok->GetVocabSize(), Gpt2.Expected);
			if (!Tok->InitializeTokenizerFromFile(T5.TokenizerPath)) { AddError(Tok->GetLastError()); return; }
			TestEqual(TEXT("t5 size after re-init"), Tok->GetVocabSize(), T5.Expected);
			if (!Tok->InitializeTokenizerFromJson(FString(UTF8_TO_TCHAR(GTinyJson)))) { AddError(Tok->GetLastError()); return; }
			TestEqual(TEXT("tiny size after re-init from json"), Tok->GetVocabSize(), 3);
		});
	});

	Describe("Threads", [this]()
	{
		LatentIt("8 threads call GetVocabSize and Encode on one object: no crash, all sizes equal", FTimespan::FromSeconds(120), [this](const FDoneDelegate& Done)
		{
			FVocabGolden G;
			FString Err;
			if (!LoadGolden(TEXT("bert-base-uncased"), G, Err)) { AddError(Err); Done.Execute(); return; }
			UTokenizerWrapper* Tok = LoadTokenizer(G, Wrapper, Err);
			if (!Tok) { AddError(Err); Done.Execute(); return; }

			const int32 Expected = G.Expected;
			const int32 NumThreads = 8;
			const int32 Iterations = 200;
			TSharedRef<TArray<int32>, ESPMode::ThreadSafe> Bad = MakeShared<TArray<int32>, ESPMode::ThreadSafe>();
			Bad->Init(0, NumThreads);
			TSharedRef<TArray<int32>, ESPMode::ThreadSafe> EncodeEmpty = MakeShared<TArray<int32>, ESPMode::ThreadSafe>();
			EncodeEmpty->Init(0, NumThreads);

			TArray<TFuture<void>> Futures;
			for (int32 T = 0; T < NumThreads; ++T)
			{
				Futures.Add(Async(EAsyncExecution::Thread, [Tok, Expected, Iterations, T, Bad, EncodeEmpty]()
				{
					for (int32 i = 0; i < Iterations; ++i)
					{
						if (Tok->GetVocabSize() != Expected) { (*Bad)[T]++; }
						if (Tok->Encode(TEXT("hello world"), true).Num() == 0) { (*EncodeEmpty)[T]++; }
					}
				}));
			}

			// Wait on a background thread so the game thread is never blocked; fail rather than hang.
			Async(EAsyncExecution::Thread, [this, Futures = MoveTemp(Futures), Bad, EncodeEmpty, Expected, Done]() mutable
			{
				bool bAllDone = true;
				const double Deadline = FPlatformTime::Seconds() + 90.0;
				for (TFuture<void>& F : Futures)
				{
					const double Remaining = FMath::Max(0.0, Deadline - FPlatformTime::Seconds());
					if (!F.WaitFor(FTimespan::FromSeconds(Remaining))) { bAllDone = false; break; }
				}
				AsyncTask(ENamedThreads::GameThread, [this, bAllDone, Bad, EncodeEmpty, Expected, Done]()
				{
					if (!bAllDone)
					{
						AddError(TEXT("worker threads did not finish within 90 s (deadlock in GetVocabSize/Encode?)"));
					}
					else
					{
						int32 TotalBad = 0, TotalEmpty = 0;
						for (int32 N : *Bad) { TotalBad += N; }
						for (int32 N : *EncodeEmpty) { TotalEmpty += N; }
						TestEqual(*FString::Printf(TEXT("calls returning a size != %d"), Expected), TotalBad, 0);
						TestEqual(TEXT("Encode calls that returned empty"), TotalEmpty, 0);
					}
					Done.Execute();
				});
			});
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
