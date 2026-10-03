// WP0.4 EncodeBatch + thread-safety tests. Written from the brief's Contract/Acceptance, not from the implementation.
// Expected values were computed with Python `tokenizers` 0.22.2 (see docs/briefs/WP0.4.md).

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Async/Async.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/ThreadSafeCounter.h"
#include "UObject/Package.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/StrongObjectPtr.h"
#include "TokenizerWrapper.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TokenizerBatchSpecPrivate
{
	// Same inline tokenizer B as TokenizerWrapperSpec: WordLevel, [UNK]=0 hello=1 world=2 [CLS]=3 [SEP]=4
	// cafe-acute=5 Tokyo-kanji=6, whitespace pre-tokenizer, template "[CLS] $A [SEP]".
	static const char* GTokenizerBJson = R"JSON({"version":"1.0","truncation":null,"padding":null,"added_tokens":[{"id":3,"content":"[CLS]","single_word":false,"lstrip":false,"rstrip":false,"normalized":false,"special":true},{"id":4,"content":"[SEP]","single_word":false,"lstrip":false,"rstrip":false,"normalized":false,"special":true}],"normalizer":null,"pre_tokenizer":{"type":"Whitespace"},"post_processor":{"type":"TemplateProcessing","single":[{"SpecialToken":{"id":"[CLS]","type_id":0}},{"Sequence":{"id":"A","type_id":0}},{"SpecialToken":{"id":"[SEP]","type_id":0}}],"pair":[{"SpecialToken":{"id":"[CLS]","type_id":0}},{"Sequence":{"id":"A","type_id":0}},{"SpecialToken":{"id":"[SEP]","type_id":0}},{"Sequence":{"id":"B","type_id":1}},{"SpecialToken":{"id":"[SEP]","type_id":1}}],"special_tokens":{"[CLS]":{"id":"[CLS]","ids":[3],"tokens":["[CLS]"]},"[SEP]":{"id":"[SEP]","ids":[4],"tokens":["[SEP]"]}}},"decoder":null,"model":{"type":"WordLevel","vocab":{"[UNK]":0,"hello":1,"world":2,"[CLS]":3,"[SEP]":4,"café":5,"東京":6},"unk_token":"[UNK]"}})JSON";

	static FString BJson()
	{
		return FString(UTF8_TO_TCHAR(GTokenizerBJson));
	}

	// T = ["hello world hello world", "hello", "", "world world world world world"]
	static TArray<FString> TextsT()
	{
		return { TEXT("hello world hello world"), TEXT("hello"), FString(), TEXT("world world world world world") };
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

	static FString Escape(const FString& S)
	{
		FString Out;
		for (TCHAR C : S)
		{
			if (C >= 32 && C < 127) { Out.AppendChar(C); }
			else { Out += FString::Printf(TEXT("\\u%04X"), (uint32)C); }
		}
		return Out;
	}

	static TStrongObjectPtr<UTokenizerWrapper> MakeB()
	{
		TStrongObjectPtr<UTokenizerWrapper> Ptr(NewObject<UTokenizerWrapper>(GetTransientPackage()));
		if (Ptr.IsValid())
		{
			Ptr->InitializeTokenizerFromJson(BJson());
		}
		return Ptr;
	}

	// Flattens rows into row-major arrays.
	static TArray<int32> Flat(const TArray<TArray<int32>>& Rows)
	{
		TArray<int32> Out;
		for (const TArray<int32>& R : Rows) { Out.Append(R); }
		return Out;
	}

	// Expected values from acceptance item 2 (MaxLength 4, specials on, pad 0).
	static TArray<int32> ExpectedTruncIds() { return Flat({ {3,1,2,4}, {3,1,4,0}, {3,4,0,0}, {3,2,2,4} }); }
	static TArray<int32> ExpectedTruncMask() { return Flat({ {1,1,1,1}, {1,1,1,0}, {1,1,0,0}, {1,1,1,1} }); }

	static bool BatchEquals(const FTokenizedBatch& B, int32 Rows, int32 Len, const TArray<int32>& Ids, const TArray<int32>& Mask)
	{
		return B.NumRows == Rows && B.SeqLen == Len && B.InputIds == Ids && B.AttentionMask == Mask;
	}

	static const TArray<int32>& HelloWorldSpecials()
	{
		static const TArray<int32> V = { 3, 1, 2, 4 };
		return V;
	}

	// State shared between the test and worker threads via TSharedRef, so a timed-out wait cannot leave
	// workers writing to a dead stack frame.
	struct FWorkerState
	{
		FThreadSafeCounter Iterations;
		FThreadSafeBool bStop = false;
	};

	// One round of checks on `Tok`; returns the number of mismatches.
	static int32 RunOneRound(UTokenizerWrapper* Tok, const TArray<FString>& Texts,
		const TArray<int32>& ExpIds, const TArray<int32>& ExpMask)
	{
		int32 Mismatches = 0;
		if (Tok->Encode(TEXT("hello world"), true) != HelloWorldSpecials()) { ++Mismatches; }
		if (!Tok->Decode(HelloWorldSpecials(), true).Equals(TEXT("hello world"), ESearchCase::CaseSensitive)) { ++Mismatches; }
		FTokenizedBatch Out;
		if (!Tok->EncodeBatch(Texts, Out, true, 4, 0) || !BatchEquals(Out, 4, 4, ExpIds, ExpMask)) { ++Mismatches; }
		return Mismatches;
	}

	// Runs `Workers` threads for `Seconds`. `Objects[i]` is the tokenizer for worker i (raw pointers: the game
	// thread's TStrongObjectPtrs keep them alive until all futures are done).
	static void RunThreads(FAutomationTestBase& Test, const TCHAR* CaseName, const TArray<UTokenizerWrapper*>& Objects,
		double Seconds)
	{
		const TArray<FString> Texts = TextsT();
		const TArray<int32> ExpIds = ExpectedTruncIds();
		const TArray<int32> ExpMask = ExpectedTruncMask();

		TSharedRef<FWorkerState, ESPMode::ThreadSafe> State = MakeShared<FWorkerState, ESPMode::ThreadSafe>();
		TArray<TFuture<int32>> Futures;
		for (UTokenizerWrapper* Tok : Objects)
		{
			Futures.Add(Async(EAsyncExecution::Thread, [Tok, State, Texts, ExpIds, ExpMask, Seconds]() -> int32
			{
				int32 Mismatches = 0;
				const double End = FPlatformTime::Seconds() + Seconds;
				while (FPlatformTime::Seconds() < End && !State->bStop)
				{
					Mismatches += RunOneRound(Tok, Texts, ExpIds, ExpMask);
					State->Iterations.Increment();
				}
				return Mismatches;
			}));
		}

		int32 TotalMismatches = 0;
		bool bTimedOut = false;
		const double Deadline = FPlatformTime::Seconds() + Seconds + 30.0;
		for (TFuture<int32>& F : Futures)
		{
			const double Remaining = FMath::Max(0.0, Deadline - FPlatformTime::Seconds());
			if (!F.WaitFor(FTimespan::FromSeconds(Remaining)))
			{
				bTimedOut = true;
				State->bStop = true;
				break;
			}
			TotalMismatches += F.Get();
		}
		if (bTimedOut)
		{
			Test.AddError(FString::Printf(TEXT("Case '%s': worker threads did not finish within %.0f s (deadlock?)"),
				CaseName, Seconds + 30.0));
			// Workers hold only TSharedRef state + raw pointers that stay alive: the caller's strong ptrs outlive this
			// function. Give them a moment to observe bStop before the caller frees the objects.
			FPlatformProcess::Sleep(1.0f);
			return;
		}
		Test.AddInfo(FString::Printf(TEXT("Case '%s': %d threads, %d total iterations in %.1f s, %d mismatches"),
			CaseName, Objects.Num(), State->Iterations.GetValue(), Seconds, TotalMismatches));
		Test.TestEqual(FString::Printf(TEXT("Case '%s': mismatch count across all threads"), CaseName), TotalMismatches, 0);
		Test.TestTrue(FString::Printf(TEXT("Case '%s': threads must have done work (iterations=%d)"), CaseName,
			State->Iterations.GetValue()), State->Iterations.GetValue() > 0);
	}
}

BEGIN_DEFINE_SPEC(FTokenizerBatchSpec, "Tokenizers.Batch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	void CheckBatch(const TCHAR* CaseName, bool bReturned, const FTokenizedBatch& Out, int32 Rows, int32 Len,
		const TArray<int32>& Ids, const TArray<int32>& Mask)
	{
		TestTrue(FString::Printf(TEXT("Case '%s': EncodeBatch should return true"), CaseName), bReturned);
		TestEqual(FString::Printf(TEXT("Case '%s': NumRows"), CaseName), Out.NumRows, Rows);
		TestEqual(FString::Printf(TEXT("Case '%s': SeqLen"), CaseName), Out.SeqLen, Len);
		TestTrue(FString::Printf(TEXT("Case '%s': InputIds differ. expected=%s actual=%s"), CaseName,
			*TokenizerBatchSpecPrivate::IdsToString(Ids), *TokenizerBatchSpecPrivate::IdsToString(Out.InputIds)),
			Out.InputIds == Ids);
		TestTrue(FString::Printf(TEXT("Case '%s': AttentionMask differ. expected=%s actual=%s"), CaseName,
			*TokenizerBatchSpecPrivate::IdsToString(Mask), *TokenizerBatchSpecPrivate::IdsToString(Out.AttentionMask)),
			Out.AttentionMask == Mask);
	}

	void CheckEmpty(const TCHAR* CaseName, const FTokenizedBatch& Out)
	{
		TestEqual(FString::Printf(TEXT("Case '%s': NumRows must be 0"), CaseName), Out.NumRows, 0);
		TestEqual(FString::Printf(TEXT("Case '%s': SeqLen must be 0"), CaseName), Out.SeqLen, 0);
		TestEqual(FString::Printf(TEXT("Case '%s': InputIds.Num() must be 0"), CaseName), Out.InputIds.Num(), 0);
		TestEqual(FString::Printf(TEXT("Case '%s': AttentionMask.Num() must be 0"), CaseName), Out.AttentionMask.Num(), 0);
	}
END_DEFINE_SPEC(FTokenizerBatchSpec)

void FTokenizerBatchSpec::Define()
{
	using namespace TokenizerBatchSpecPrivate;

	Describe("EncodeBatch", [this]()
	{
		It("NoTruncationPadsToLongestRow", [this]()
		{
			TStrongObjectPtr<UTokenizerWrapper> Tok = MakeB();
			if (!TestTrue(TEXT("init B"), Tok.IsValid() && Tok->IsInitialized())) { return; }
			FTokenizedBatch Out;
			// A batcher that pads to a fixed length or drops the pad id shows up in row 2/3 ([3,1,4,9,...]).
			const bool bOk = Tok->EncodeBatch(TextsT(), Out, true, 0, 9);
			CheckBatch(TEXT("no truncation, pad 9"), bOk, Out, 4, 7,
				Flat({ {3,1,2,1,2,4,9}, {3,1,4,9,9,9,9}, {3,4,9,9,9,9,9}, {3,2,2,2,2,2,4} }),
				Flat({ {1,1,1,1,1,1,0}, {1,1,1,0,0,0,0}, {1,1,0,0,0,0,0}, {1,1,1,1,1,1,1} }));
			TestTrue(TEXT("GetLastError() empty after success"), Tok->GetLastError().IsEmpty());
		});

		It("TruncatedWithSpecialsKeepsSep", [this]()
		{
			TStrongObjectPtr<UTokenizerWrapper> Tok = MakeB();
			if (!TestTrue(TEXT("init B"), Tok.IsValid() && Tok->IsInitialized())) { return; }
			FTokenizedBatch Out;
			// Truncating after adding specials would leave row 0 as [3,1,2,1] with no [SEP]; HF gives [3,1,2,4].
			const bool bOk = Tok->EncodeBatch(TextsT(), Out, true, 4, 0);
			CheckBatch(TEXT("MaxLength 4 specials"), bOk, Out, 4, 4, ExpectedTruncIds(), ExpectedTruncMask());
			// Every row ends with [SEP]=4 right before its padding.
			for (int32 Row = 0; Row < Out.NumRows && Out.SeqLen > 0; ++Row)
			{
				int32 Last = -1;
				for (int32 c = 0; c < Out.SeqLen; ++c)
				{
					if (Out.AttentionMask.IsValidIndex(Row * Out.SeqLen + c) && Out.AttentionMask[Row * Out.SeqLen + c] == 1) { Last = c; }
				}
				const int32 Id = (Last >= 0 && Out.InputIds.IsValidIndex(Row * Out.SeqLen + Last)) ? Out.InputIds[Row * Out.SeqLen + Last] : -1;
				TestEqual(FString::Printf(TEXT("row %d last real token must be [SEP]=4"), Row), Id, 4);
			}
		});

		It("TruncatedWithoutSpecials", [this]()
		{
			TStrongObjectPtr<UTokenizerWrapper> Tok = MakeB();
			if (!TestTrue(TEXT("init B"), Tok.IsValid() && Tok->IsInitialized())) { return; }
			FTokenizedBatch Out;
			const bool bOk = Tok->EncodeBatch(TextsT(), Out, false, 2, 0);
			CheckBatch(TEXT("MaxLength 2 no specials"), bOk, Out, 4, 2,
				Flat({ {1,2}, {1,0}, {0,0}, {2,2} }), Flat({ {1,1}, {1,0}, {0,0}, {1,1} }));
		});

		It("MaxLengthBelowSpecialCountLeavesRowsUntruncated", [this]()
		{
			TStrongObjectPtr<UTokenizerWrapper> Tok = MakeB();
			if (!TestTrue(TEXT("init B"), Tok.IsValid() && Tok->IsInitialized())) { return; }
			FTokenizedBatch Out;
			// MaxLength 1 < 2 added specials: HF returns rows untruncated. Cutting to 1 column would give SeqLen 1.
			const bool bOk = Tok->EncodeBatch(TextsT(), Out, true, 1, 0);
			CheckBatch(TEXT("MaxLength 1 specials"), bOk, Out, 4, 7,
				Flat({ {3,1,2,1,2,4,0}, {3,1,4,0,0,0,0}, {3,4,0,0,0,0,0}, {3,2,2,2,2,2,4} }),
				Flat({ {1,1,1,1,1,1,0}, {1,1,1,0,0,0,0}, {1,1,0,0,0,0,0}, {1,1,1,1,1,1,1} }));
		});

		It("EmptyTextsGivesEmptySuccess", [this]()
		{
			TStrongObjectPtr<UTokenizerWrapper> Tok = MakeB();
			if (!TestTrue(TEXT("init B"), Tok.IsValid() && Tok->IsInitialized())) { return; }
			FTokenizedBatch Out;
			// Pre-fill: a success path that forgets to reset OutBatch would keep this data.
			Out.InputIds = { 1, 2, 3 }; Out.AttentionMask = { 1, 1, 1 }; Out.NumRows = 1; Out.SeqLen = 3;
			const bool bOk = Tok->EncodeBatch(TArray<FString>(), Out, true, 0, 0);
			TestTrue(TEXT("empty Texts should return true"), bOk);
			CheckEmpty(TEXT("empty Texts"), Out);
			TestTrue(TEXT("GetLastError() empty after empty batch"), Tok->GetLastError().IsEmpty());
		});

		It("NegativeMaxLengthFailsAndClearsOutput", [this]()
		{
			TStrongObjectPtr<UTokenizerWrapper> Tok = MakeB();
			if (!TestTrue(TEXT("init B"), Tok.IsValid() && Tok->IsInitialized())) { return; }
			AddExpectedError(TEXT("EncodeBatch"), EAutomationExpectedErrorFlags::Contains, 0);
			FTokenizedBatch Out;
			Out.InputIds = { 7, 7 }; Out.AttentionMask = { 1, 1 }; Out.NumRows = 1; Out.SeqLen = 2;
			TestFalse(TEXT("MaxLength -1 must return false"), Tok->EncodeBatch(TextsT(), Out, true, -1, 0));
			CheckEmpty(TEXT("MaxLength -1"), Out);
			TestFalse(TEXT("MaxLength -1: GetLastError() must be non-empty"), Tok->GetLastError().IsEmpty());
		});

		It("NegativePadTokenIdFails", [this]()
		{
			TStrongObjectPtr<UTokenizerWrapper> Tok = MakeB();
			if (!TestTrue(TEXT("init B"), Tok.IsValid() && Tok->IsInitialized())) { return; }
			AddExpectedError(TEXT("EncodeBatch"), EAutomationExpectedErrorFlags::Contains, 0);
			FTokenizedBatch Out;
			Out.InputIds = { 7 }; Out.AttentionMask = { 1 }; Out.NumRows = 1; Out.SeqLen = 1;
			TestFalse(TEXT("PadTokenId -3 must return false"), Tok->EncodeBatch(TextsT(), Out, true, 0, -3));
			CheckEmpty(TEXT("PadTokenId -3"), Out);
			TestFalse(TEXT("PadTokenId -3: GetLastError() must be non-empty"), Tok->GetLastError().IsEmpty());
		});

		It("NotInitializedFails", [this]()
		{
			AddExpectedError(TEXT("EncodeBatch"), EAutomationExpectedErrorFlags::Contains, 0);
			TStrongObjectPtr<UTokenizerWrapper> Tok(NewObject<UTokenizerWrapper>(GetTransientPackage()));
			if (!TestTrue(TEXT("object created"), Tok.IsValid())) { return; }
			FTokenizedBatch Out;
			Out.InputIds = { 5 }; Out.AttentionMask = { 1 }; Out.NumRows = 1; Out.SeqLen = 1;
			TestFalse(TEXT("uninitialized EncodeBatch must return false"), Tok->EncodeBatch(TextsT(), Out, true, 0, 0));
			CheckEmpty(TEXT("uninitialized"), Out);
			TestFalse(TEXT("uninitialized: GetLastError() must be non-empty"), Tok->GetLastError().IsEmpty());
		});

		It("SingleEmptyStringWithoutSpecialsIsOneRowZeroLength", [this]()
		{
			TStrongObjectPtr<UTokenizerWrapper> Tok = MakeB();
			if (!TestTrue(TEXT("init B"), Tok.IsValid() && Tok->IsInitialized())) { return; }
			FTokenizedBatch Out;
			const bool bOk = Tok->EncodeBatch({ FString() }, Out, false, 0, 0);
			CheckBatch(TEXT("[\"\"] no specials"), bOk, Out, 1, 0, TArray<int32>(), TArray<int32>());
		});

		It("SingleEmptyStringWithSpecialsIsClsSep", [this]()
		{
			TStrongObjectPtr<UTokenizerWrapper> Tok = MakeB();
			if (!TestTrue(TEXT("init B"), Tok.IsValid() && Tok->IsInitialized())) { return; }
			FTokenizedBatch Out;
			const bool bOk = Tok->EncodeBatch({ FString() }, Out, true, 0, 0);
			CheckBatch(TEXT("[\"\"] specials"), bOk, Out, 1, 2, { 3, 4 }, { 1, 1 });
		});

		It("ResultOverwritesPreviousBatch", [this]()
		{
			TStrongObjectPtr<UTokenizerWrapper> Tok = MakeB();
			if (!TestTrue(TEXT("init B"), Tok.IsValid() && Tok->IsInitialized())) { return; }
			FTokenizedBatch Out;
			TestTrue(TEXT("first call"), Tok->EncodeBatch(TextsT(), Out, true, 0, 9));
			// Appending instead of replacing would leave 28 + 4 ids.
			const bool bOk = Tok->EncodeBatch({ TEXT("hello") }, Out, true, 0, 0);
			CheckBatch(TEXT("second call reuses Out"), bOk, Out, 1, 3, { 3, 1, 4 }, { 1, 1, 1 });
		});

		It("PadIdIsWrittenOnlyWhereMaskIsZero", [this]()
		{
			TStrongObjectPtr<UTokenizerWrapper> Tok = MakeB();
			if (!TestTrue(TEXT("init B"), Tok.IsValid() && Tok->IsInitialized())) { return; }
			FTokenizedBatch Out;
			// Non-zero pad id that is also a real token (2 = world): mask must still tell real from padding.
			const bool bOk = Tok->EncodeBatch({ TEXT("hello"), TEXT("hello world") }, Out, false, 0, 2);
			CheckBatch(TEXT("pad id 2 (= world)"), bOk, Out, 2, 2, { 1, 2, 1, 2 }, { 1, 0, 1, 1 });
		});

		It("ConsistentWithEncode", [this]()
		{
			TStrongObjectPtr<UTokenizerWrapper> Tok = MakeB();
			if (!TestTrue(TEXT("init B"), Tok.IsValid() && Tok->IsInitialized())) { return; }
			const TArray<FString> Texts = TextsT();
			FTokenizedBatch Out;
			if (!TestTrue(TEXT("EncodeBatch"), Tok->EncodeBatch(Texts, Out, true, 0, 9))) { return; }
			if (!TestEqual(TEXT("NumRows"), Out.NumRows, Texts.Num())) { return; }
			if (!TestEqual(TEXT("InputIds size = NumRows*SeqLen"), Out.InputIds.Num(), Out.NumRows * Out.SeqLen)) { return; }
			if (!TestEqual(TEXT("Mask size = NumRows*SeqLen"), Out.AttentionMask.Num(), Out.NumRows * Out.SeqLen)) { return; }
			for (int32 Row = 0; Row < Texts.Num(); ++Row)
			{
				TArray<int32> Real;
				for (int32 c = 0; c < Out.SeqLen; ++c)
				{
					if (Out.AttentionMask[Row * Out.SeqLen + c] == 1) { Real.Add(Out.InputIds[Row * Out.SeqLen + c]); }
				}
				const TArray<int32> Single = Tok->Encode(Texts[Row], true);
				TestTrue(FString::Printf(TEXT("row %d ('%s'): masked row %s must equal Encode(text,true) %s"), Row,
					*Escape(Texts[Row]), *IdsToString(Real), *IdsToString(Single)), Real == Single);
			}
		});

		It("NonAsciiAndEmojiRows", [this]()
		{
			TStrongObjectPtr<UTokenizerWrapper> Tok = MakeB();
			if (!TestTrue(TEXT("init B"), Tok.IsValid() && Tok->IsInitialized())) { return; }
			// "cafe-acute Tokyo-kanji" and a 4-byte emoji (unknown -> [UNK]=0) built from UTF-8 escapes.
			const FString CafeTokyo(UTF8_TO_TCHAR("caf" "\xC3\xA9" " " "\xE6\x9D\xB1" "\xE4\xBA\xAC"));
			const FString Emoji(UTF8_TO_TCHAR("hello \xF0\x9F\x99\x82"));
			FTokenizedBatch Out;
			// Wrong per-text lengths (e.g. one shared length or strlen on the wrong buffer) corrupt later rows.
			const bool bOk = Tok->EncodeBatch({ CafeTokyo, Emoji }, Out, true, 0, 0);
			CheckBatch(TEXT("non-ascii batch"), bOk, Out, 2, 4, Flat({ {3,5,6,4}, {3,1,0,4} }), Flat({ {1,1,1,1}, {1,1,1,1} }));
		});
	});

	Describe("Threads", [this]()
	{
		It("EightThreadsOneSharedObject", [this]()
		{
			TStrongObjectPtr<UTokenizerWrapper> Tok = MakeB();
			if (!TestTrue(TEXT("init B"), Tok.IsValid() && Tok->IsInitialized())) { return; }
			// Single-threaded expected values first, so a wrong expectation is not blamed on threading.
			if (!TestTrue(TEXT("single-thread Encode baseline"), Tok->Encode(TEXT("hello world"), true) == HelloWorldSpecials())) { return; }
			FTokenizedBatch Base;
			if (!TestTrue(TEXT("single-thread EncodeBatch baseline"),
				Tok->EncodeBatch(TextsT(), Base, true, 4, 0) && BatchEquals(Base, 4, 4, ExpectedTruncIds(), ExpectedTruncMask())))
			{
				return;
			}
			// Unsynchronized use of one Rust handle corrupts results or crashes; the mutex makes this 0.
			TArray<UTokenizerWrapper*> Objects;
			for (int32 i = 0; i < 8; ++i) { Objects.Add(Tok.Get()); }
			RunThreads(*this, TEXT("8 threads, 1 shared object"), Objects, 2.0);
		});

		It("EightThreadsSeparateObjects", [this]()
		{
			TArray<TStrongObjectPtr<UTokenizerWrapper>> Owners;
			TArray<UTokenizerWrapper*> Objects;
			for (int32 i = 0; i < 8; ++i)
			{
				TStrongObjectPtr<UTokenizerWrapper> Tok = MakeB();
				if (!TestTrue(TEXT("init B"), Tok.IsValid() && Tok->IsInitialized())) { return; }
				Objects.Add(Tok.Get());
				Owners.Add(MoveTemp(Tok));
			}
			RunThreads(*this, TEXT("8 threads, 8 objects"), Objects, 2.0);
		});
	});

	Describe("Lifetime", [this]()
	{
		It("DestroyWhileWorkerBusy", [this]()
		{
			// Strong refs are created and released only on the game thread; the worker uses a raw pointer that
			// the game thread's strong ref keeps alive until the worker's future has completed.
			TStrongObjectPtr<UTokenizerWrapper> Tok = MakeB();
			if (!TestTrue(TEXT("init B"), Tok.IsValid() && Tok->IsInitialized())) { return; }
			UTokenizerWrapper* Raw = Tok.Get();
			const TArray<FString> Texts = TextsT();
			const TArray<int32> ExpIds = ExpectedTruncIds();
			const TArray<int32> ExpMask = ExpectedTruncMask();
			TSharedRef<FWorkerState, ESPMode::ThreadSafe> State = MakeShared<FWorkerState, ESPMode::ThreadSafe>();

			TFuture<int32> Future = Async(EAsyncExecution::Thread, [Raw, State, Texts, ExpIds, ExpMask]() -> int32
			{
				int32 Mismatches = 0;
				while (!State->bStop)
				{
					Mismatches += RunOneRound(Raw, Texts, ExpIds, ExpMask);
					State->Iterations.Increment();
				}
				return Mismatches;
			});

			// Let the worker get going, then run a GC while it is busy: the strong ref must protect the object.
			FPlatformProcess::Sleep(0.2f);
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
			FPlatformProcess::Sleep(0.1f);

			State->bStop = true;
			const bool bDone = Future.WaitFor(FTimespan::FromSeconds(30.0));
			if (!TestTrue(TEXT("worker thread must finish within 30 s after stop was requested"), bDone))
			{
				// Keep the object alive (leak the ref on purpose) rather than free it under a running worker.
				new TStrongObjectPtr<UTokenizerWrapper>(Tok);
				return;
			}
			const int32 Mismatches = Future.Get();
			AddInfo(FString::Printf(TEXT("worker ran %d iterations, %d mismatches"), State->Iterations.GetValue(), Mismatches));
			TestEqual(TEXT("worker mismatches while GC ran"), Mismatches, 0);
			TestTrue(TEXT("worker must have done work"), State->Iterations.GetValue() > 0);

			// Now drop the last reference and collect: destruction of an object that was used from another thread.
			Tok.Reset();
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
			TestTrue(TEXT("reached end after destroying a wrapper used by a background thread"), true);
		});
	});

	Describe("NulByte", [this]()
	{
		It("DecodeKeepsTrailingNul", [this]()
		{
			TStrongObjectPtr<UTokenizerWrapper> Tok(NewObject<UTokenizerWrapper>(GetTransientPackage()));
			if (!TestTrue(TEXT("object created"), Tok.IsValid())) { return; }
			if (!TestTrue(FString::Printf(TEXT("init bundled tokenizer.json. error='%s'"), *Tok->GetLastError()),
				Tok->InitializeTokenizerFromFile(TEXT("tokenizer.json")))) { return; }
			// Id 177 is the byte 0x00 in GPT-NeoX. A length-based conversion that treats a trailing 0 as a
			// terminator returns "Hello" (Len 5).
			const FString Actual = Tok->Decode({ 12092, 177 });
			TestEqual(FString::Printf(TEXT("Decode([12092,177]).Len() actual='%s'"), *Escape(Actual)), Actual.Len(), 6);
			if (Actual.Len() == 6)
			{
				TestEqual(TEXT("last char must be U+0000"), (int32)Actual[5], 0);
				TestTrue(TEXT("first five chars must be 'Hello'"), Actual.Left(5).Equals(TEXT("Hello"), ESearchCase::CaseSensitive));
			}
		});

		It("EncodeKeepsTrailingNul", [this]()
		{
			TStrongObjectPtr<UTokenizerWrapper> Tok(NewObject<UTokenizerWrapper>(GetTransientPackage()));
			if (!TestTrue(TEXT("object created"), Tok.IsValid())) { return; }
			if (!TestTrue(FString::Printf(TEXT("init bundled tokenizer.json. error='%s'"), *Tok->GetLastError()),
				Tok->InitializeTokenizerFromFile(TEXT("tokenizer.json")))) { return; }
			// AppendChar(0) is a no-op in UE 5.8, so build the string from a sized buffer.
			const FString Text = FString::ConstructFromPtrSize(TEXT("Hello\0"), 6);
			if (!TestEqual(TEXT("setup: input length must be 6"), Text.Len(), 6)
				|| !TestEqual(TEXT("setup: Text[5] must be U+0000"), (int32)Text[5], 0)) { return; }
			// Dropping the NUL would give [12092].
			const TArray<int32> Ids = Tok->Encode(Text);
			TestTrue(FString::Printf(TEXT("Encode(\"Hello\\u0000\") expected [12092, 177] actual %s"), *IdsToString(Ids)),
				Ids == TArray<int32>({ 12092, 177 }));
		});

		It("EncodeBatchKeepsTrailingNul", [this]()
		{
			TStrongObjectPtr<UTokenizerWrapper> Tok(NewObject<UTokenizerWrapper>(GetTransientPackage()));
			if (!TestTrue(TEXT("object created"), Tok.IsValid())) { return; }
			if (!TestTrue(FString::Printf(TEXT("init bundled tokenizer.json. error='%s'"), *Tok->GetLastError()),
				Tok->InitializeTokenizerFromFile(TEXT("tokenizer.json")))) { return; }
			// AppendChar(0) is a no-op in UE 5.8, so build the string from a sized buffer.
			const FString Text = FString::ConstructFromPtrSize(TEXT("Hello\0"), 6);
			if (!TestEqual(TEXT("setup: input length must be 6"), Text.Len(), 6)
				|| !TestEqual(TEXT("setup: Text[5] must be U+0000"), (int32)Text[5], 0)) { return; }
			FTokenizedBatch Out;
			const bool bOk = Tok->EncodeBatch({ Text }, Out, false, 0, 0);
			CheckBatch(TEXT("batch with trailing NUL"), bOk, Out, 1, 2, { 12092, 177 }, { 1, 1 });
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
