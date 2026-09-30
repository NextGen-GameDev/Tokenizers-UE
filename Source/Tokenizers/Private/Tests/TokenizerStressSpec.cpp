// WP0.5 stress tests: 8 threads for 10 s, output must stay equal to the Python goldens. Goldens are read on the game
// thread before any worker starts; workers only compare and never parse JSON or use `check`.
// Goldens: $TOKENIZERS_GOLDENS_DIR or <ProjectDir>/../.pipelines-dev/goldens/tokenizers (make with Tools/make_tokenizer_goldens.py).

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Async/Async.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/ThreadSafeCounter.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/StrongObjectPtr.h"
#include "TokenizerWrapper.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TokenizerStressSpecPrivate
{
	// Everything a worker needs, precomputed on the game thread. Immutable once workers start.
	struct FStressData
	{
		FString Name;
		FString TokenizerPath;
		TArray<FString> Texts;
		TArray<TArray<int32>> IdsWith;
		TArray<FString> DecodeSkip;
		// Golden batch b01
		TArray<FString> BatchTexts;
		bool bBatchSpecial = false;
		int32 BatchMaxLength = 0;
		int32 BatchPad = 0;
		int32 BatchRows = 0;
		int32 BatchSeqLen = 0;
		TArray<int32> BatchIds;
		TArray<int32> BatchMask;
	};

	struct FWorkerResult
	{
		int32 Iterations = 0;
		int32 Mismatches = 0;
		FString FirstMismatch;
	};

	struct FRunState
	{
		FThreadSafeBool bStop = false;
	};

	static FString GoldensDir()
	{
		const FString Env = FPlatformMisc::GetEnvironmentVariable(TEXT("TOKENIZERS_GOLDENS_DIR"));
		if (!Env.IsEmpty()) { return FPaths::ConvertRelativePathToFull(Env); }
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../.pipelines-dev/goldens/tokenizers"));
	}

	static bool ReadInts(const TSharedPtr<FJsonValue>& V, TArray<int32>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!V.IsValid() || !V->TryGetArray(Arr) || !Arr) { return false; }
		Out.Reset();
		for (const TSharedPtr<FJsonValue>& E : *Arr)
		{
			double D = 0.0;
			if (!E.IsValid() || !E->TryGetNumber(D)) { return false; }
			Out.Add((int32)D);
		}
		return true;
	}

	static bool LoadStressData(const FString& Name, FStressData& Out, FString& OutError)
	{
		Out.Name = Name;
		const FString GoldenPath = FPaths::ConvertRelativePathToFull(GoldensDir() / (Name + TEXT(".json")));
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *GoldenPath))
		{
			OutError = FString::Printf(TEXT("goldens not found at %s; run Tools/make_tokenizer_goldens.py"), *GoldenPath);
			return false;
		}
		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = FString::Printf(TEXT("goldens at %s are not valid JSON"), *GoldenPath);
			return false;
		}
		const TSharedPtr<FJsonObject>* TokObj = nullptr;
		FString RelFile;
		if (!Root->TryGetObjectField(TEXT("tokenizer"), TokObj) || !TokObj || !(*TokObj)->TryGetStringField(TEXT("file"), RelFile))
		{
			OutError = FString::Printf(TEXT("goldens at %s lack tokenizer.file"), *GoldenPath);
			return false;
		}
		Out.TokenizerPath = FPaths::ConvertRelativePathToFull(FPaths::GetPath(GoldenPath) / RelFile);

		TMap<FString, FString> TextById;
		const TArray<TSharedPtr<FJsonValue>>* Cases = nullptr;
		if (!Root->TryGetArrayField(TEXT("cases"), Cases) || !Cases)
		{
			OutError = FString::Printf(TEXT("goldens at %s lack 'cases'"), *GoldenPath);
			return false;
		}
		for (const TSharedPtr<FJsonValue>& V : *Cases)
		{
			const TSharedPtr<FJsonObject>* C = nullptr;
			FString Id, CaseText, Skip;
			TArray<int32> Ids;
			if (!V.IsValid() || !V->TryGetObject(C) || !C
				|| !(*C)->TryGetStringField(TEXT("id"), Id)
				|| !(*C)->TryGetStringField(TEXT("text"), CaseText)
				|| !(*C)->TryGetStringField(TEXT("decode_with_special_skip"), Skip)
				|| !(*C)->HasField(TEXT("ids_with_special"))
				|| !ReadInts((*C)->TryGetField(TEXT("ids_with_special")), Ids))
			{
				OutError = FString::Printf(TEXT("a case in %s is malformed"), *GoldenPath);
				return false;
			}
			TextById.Add(Id, CaseText);
			Out.Texts.Add(CaseText);
			Out.IdsWith.Add(MoveTemp(Ids));
			Out.DecodeSkip.Add(Skip);
		}

		const TArray<TSharedPtr<FJsonValue>>* Batches = nullptr;
		if (!Root->TryGetArrayField(TEXT("batches"), Batches) || !Batches)
		{
			OutError = FString::Printf(TEXT("goldens at %s lack 'batches'"), *GoldenPath);
			return false;
		}
		for (const TSharedPtr<FJsonValue>& V : *Batches)
		{
			const TSharedPtr<FJsonObject>* B = nullptr;
			FString Id;
			if (!V.IsValid() || !V->TryGetObject(B) || !B || !(*B)->TryGetStringField(TEXT("id"), Id) || Id != TEXT("b01")) { continue; }

			const TArray<TSharedPtr<FJsonValue>>* CaseIds = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* IdRows = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* MaskRows = nullptr;
			if (!(*B)->TryGetBoolField(TEXT("add_special_tokens"), Out.bBatchSpecial)
				|| !(*B)->TryGetNumberField(TEXT("pad_id"), Out.BatchPad)
				|| !(*B)->TryGetArrayField(TEXT("case_ids"), CaseIds) || !CaseIds
				|| !(*B)->TryGetArrayField(TEXT("input_ids"), IdRows) || !IdRows
				|| !(*B)->TryGetArrayField(TEXT("attention_mask"), MaskRows) || !MaskRows)
			{
				OutError = FString::Printf(TEXT("batch b01 in %s is malformed"), *GoldenPath);
				return false;
			}
			Out.BatchMaxLength = 0;
			if ((*B)->HasTypedField<EJson::Number>(TEXT("max_length"))) { (*B)->TryGetNumberField(TEXT("max_length"), Out.BatchMaxLength); }
			for (const TSharedPtr<FJsonValue>& CV : *CaseIds)
			{
				FString CId;
				const FString* T = (CV.IsValid() && CV->TryGetString(CId)) ? TextById.Find(CId) : nullptr;
				if (!T) { OutError = FString::Printf(TEXT("batch b01 in %s references an unknown case id"), *GoldenPath); return false; }
				Out.BatchTexts.Add(*T);
			}
			Out.BatchRows = IdRows->Num();
			for (int32 r = 0; r < IdRows->Num(); ++r)
			{
				TArray<int32> Row, MRow;
				if (!ReadInts((*IdRows)[r], Row) || !MaskRows->IsValidIndex(r) || !ReadInts((*MaskRows)[r], MRow))
				{
					OutError = FString::Printf(TEXT("batch b01 in %s has bad rows"), *GoldenPath);
					return false;
				}
				if (r == 0) { Out.BatchSeqLen = Row.Num(); }
				Out.BatchIds.Append(Row);
				Out.BatchMask.Append(MRow);
			}
			break;
		}
		if (Out.Texts.Num() < 20 || Out.BatchTexts.Num() == 0)
		{
			OutError = FString::Printf(TEXT("goldens at %s need >= 20 cases and a batch b01 (got %d cases, %d b01 rows)"),
				*GoldenPath, Out.Texts.Num(), Out.BatchTexts.Num());
			return false;
		}
		return true;
	}

	static FString Brief(const FString& S)
	{
		FString Out;
		for (TCHAR C : S)
		{
			if (Out.Len() > 60) { Out += TEXT("..."); break; }
			if (C >= 32 && C < 127) { Out.AppendChar(C); } else { Out += FString::Printf(TEXT("\\u%04X"), (uint32)C); }
		}
		return Out;
	}

	// Worker body. Touches the tokenizer only through `Tok` (kept alive by the game thread).
	static FWorkerResult WorkerLoop(UTokenizerWrapper* Tok, const FStressData& D, double Seconds, const FRunState& State)
	{
		FWorkerResult R;
		if (Tok == nullptr) { R.Mismatches = 1; R.FirstMismatch = TEXT("null tokenizer"); return R; }
		const int32 N = D.Texts.Num();
		if (N == 0) { R.Mismatches = 1; R.FirstMismatch = TEXT("no cases"); return R; }
		const double End = FPlatformTime::Seconds() + Seconds;
		int32 Iter = 0;
		auto Fail = [&R](const FString& Msg)
		{
			if (R.Mismatches == 0) { R.FirstMismatch = Msg; }
			++R.Mismatches;
		};
		while (FPlatformTime::Seconds() < End && !State.bStop)
		{
			const int32 i = Iter % N;
			const TArray<int32> Ids = Tok->Encode(D.Texts[i], true);
			if (Ids != D.IdsWith[i])
			{
				Fail(FString::Printf(TEXT("[%s] case #%d Encode(text '%s') returned %d ids, expected %d"), *D.Name, i, *Brief(D.Texts[i]), Ids.Num(), D.IdsWith[i].Num()));
			}
			const FString Dec = Tok->Decode(D.IdsWith[i], true);
			if (!Dec.Equals(D.DecodeSkip[i], ESearchCase::CaseSensitive))
			{
				Fail(FString::Printf(TEXT("[%s] case #%d Decode expected '%s' actual '%s'"), *D.Name, i, *Brief(D.DecodeSkip[i]), *Brief(Dec)));
			}
			if (Iter % 10 == 0)
			{
				FTokenizedBatch Out;
				const bool bOk = Tok->EncodeBatch(D.BatchTexts, Out, D.bBatchSpecial, D.BatchMaxLength, D.BatchPad);
				if (!bOk || Out.NumRows != D.BatchRows || Out.SeqLen != D.BatchSeqLen || Out.InputIds != D.BatchIds || Out.AttentionMask != D.BatchMask)
				{
					Fail(FString::Printf(TEXT("[%s] batch b01 mismatch (ok=%d rows=%d/%d seq=%d/%d)"), *D.Name, bOk ? 1 : 0,
						Out.NumRows, D.BatchRows, Out.SeqLen, D.BatchSeqLen));
				}
			}
			++Iter;
			++R.Iterations;
		}
		return R;
	}

	// Runs one worker per entry of Tokens (worker i uses Tokens[i] and Datas[i]).
	static void RunWorkers(FAutomationTestBase& Test, const TCHAR* CaseName, const TArray<UTokenizerWrapper*>& Tokens,
		const TArray<TSharedRef<FStressData, ESPMode::ThreadSafe>>& Datas, double Seconds, TArray<TStrongObjectPtr<UTokenizerWrapper>>& Owners)
	{
		TSharedRef<FRunState, ESPMode::ThreadSafe> State = MakeShared<FRunState, ESPMode::ThreadSafe>();
		TArray<TFuture<FWorkerResult>> Futures;
		const double Start = FPlatformTime::Seconds();
		for (int32 t = 0; t < Tokens.Num(); ++t)
		{
			UTokenizerWrapper* Tok = Tokens[t];
			TSharedRef<FStressData, ESPMode::ThreadSafe> Data = Datas[t];
			Futures.Add(Async(EAsyncExecution::Thread, [Tok, Data, State, Seconds]() -> FWorkerResult
			{
				return WorkerLoop(Tok, *Data, Seconds, *State);
			}));
		}

		int32 TotalIter = 0, TotalMismatch = 0;
		FString FirstMismatch;
		bool bTimedOut = false;
		const double Deadline = FPlatformTime::Seconds() + Seconds + 60.0;
		for (TFuture<FWorkerResult>& F : Futures)
		{
			const double Remaining = FMath::Max(0.0, Deadline - FPlatformTime::Seconds());
			if (!F.WaitFor(FTimespan::FromSeconds(Remaining))) { bTimedOut = true; State->bStop = true; break; }
			const FWorkerResult R = F.Get();
			TotalIter += R.Iterations;
			TotalMismatch += R.Mismatches;
			if (FirstMismatch.IsEmpty() && !R.FirstMismatch.IsEmpty()) { FirstMismatch = R.FirstMismatch; }
		}
		if (bTimedOut)
		{
			Test.AddError(FString::Printf(TEXT("Case '%s': workers did not finish within %.0f s (deadlock?)"), CaseName, Seconds + 60.0));
			// Never free objects under a possibly running worker: leak the strong refs on purpose.
			for (TStrongObjectPtr<UTokenizerWrapper>& O : Owners) { new TStrongObjectPtr<UTokenizerWrapper>(O); }
			FPlatformProcess::Sleep(1.0f);
			return;
		}
		const double Elapsed = FMath::Max(0.001, FPlatformTime::Seconds() - Start);
		Test.AddInfo(FString::Printf(TEXT("Case '%s': %d threads, %d iterations in %.1f s (%.0f iterations/s; each = Encode + Decode, + EncodeBatch every 10th), %d mismatches"),
			CaseName, Tokens.Num(), TotalIter, Elapsed, TotalIter / Elapsed, TotalMismatch));
		if (TotalMismatch > 0)
		{
			Test.AddError(FString::Printf(TEXT("Case '%s': %d mismatches, first: %s"), CaseName, TotalMismatch, *FirstMismatch));
		}
		Test.TestEqual(FString::Printf(TEXT("Case '%s': mismatch count"), CaseName), TotalMismatch, 0);
		Test.TestTrue(FString::Printf(TEXT("Case '%s': at least 1000 total iterations (got %d)"), CaseName, TotalIter), TotalIter >= 1000);
	}

	static TStrongObjectPtr<UTokenizerWrapper> MakeTok(FAutomationTestBase& Test, const FStressData& D)
	{
		TStrongObjectPtr<UTokenizerWrapper> Ptr(NewObject<UTokenizerWrapper>(GetTransientPackage()));
		if (!Ptr.IsValid() || !Ptr->InitializeTokenizerFromFile(D.TokenizerPath))
		{
			Test.AddError(FString::Printf(TEXT("[%s] cannot load tokenizer %s (error: %s). Fetch models with Tools/fetch_models.py (see CLAUDE.md)"),
				*D.Name, *D.TokenizerPath, Ptr.IsValid() ? *Ptr->GetLastError() : TEXT("object creation failed")));
			Ptr.Reset();
		}
		return Ptr;
	}
}

BEGIN_DEFINE_SPEC(FTokenizerStressSpec, "Tokenizers.Stress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FTokenizerStressSpec)

void FTokenizerStressSpec::Define()
{
	using namespace TokenizerStressSpecPrivate;

	It("EightThreadsTenSecondsSharedObject", [this]()
	{
		FString Err;
		TSharedRef<FStressData, ESPMode::ThreadSafe> Data = MakeShared<FStressData, ESPMode::ThreadSafe>();
		if (!LoadStressData(TEXT("bert-base-uncased"), *Data, Err)) { AddError(Err); return; }
		TStrongObjectPtr<UTokenizerWrapper> Tok = MakeTok(*this, *Data);
		if (!Tok.IsValid()) { return; }

		// One object, 8 threads: only the per-object lock keeps the shared Rust handle consistent.
		TArray<UTokenizerWrapper*> Tokens;
		TArray<TSharedRef<FStressData, ESPMode::ThreadSafe>> Datas;
		TArray<TStrongObjectPtr<UTokenizerWrapper>> Owners;
		Owners.Add(Tok);
		for (int32 i = 0; i < 8; ++i) { Tokens.Add(Tok.Get()); Datas.Add(Data); }
		RunWorkers(*this, TEXT("8 threads, 1 shared bert object"), Tokens, Datas, 10.0, Owners);
	});

	It("EightThreadsTenSecondsMixedTokenizers", [this]()
	{
		const TCHAR* Names[3] = { TEXT("bert-base-uncased"), TEXT("gpt2"), TEXT("t5-small") };
		TArray<TSharedRef<FStressData, ESPMode::ThreadSafe>> PerTokenizer;
		for (int32 n = 0; n < 3; ++n)
		{
			FString Err;
			TSharedRef<FStressData, ESPMode::ThreadSafe> D = MakeShared<FStressData, ESPMode::ThreadSafe>();
			if (!LoadStressData(Names[n], *D, Err)) { AddError(Err); return; }
			PerTokenizer.Add(D);
		}

		// Thread i gets its own object for tokenizer i % 3. Owners keep them alive until every future is done.
		TArray<TStrongObjectPtr<UTokenizerWrapper>> Owners;
		TArray<UTokenizerWrapper*> Tokens;
		TArray<TSharedRef<FStressData, ESPMode::ThreadSafe>> Datas;
		for (int32 i = 0; i < 8; ++i)
		{
			TStrongObjectPtr<UTokenizerWrapper> Tok = MakeTok(*this, *PerTokenizer[i % 3]);
			if (!Tok.IsValid()) { return; }
			Tokens.Add(Tok.Get());
			Datas.Add(PerTokenizer[i % 3]);
			Owners.Add(MoveTemp(Tok));
		}
		RunWorkers(*this, TEXT("8 threads, own object each, bert/gpt2/t5 round robin"), Tokens, Datas, 10.0, Owners);
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
