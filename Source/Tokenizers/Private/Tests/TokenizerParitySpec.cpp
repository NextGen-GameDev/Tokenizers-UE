// WP0.5 parity tests: encode/decode/batch output must equal Python `tokenizers` exactly (ids element by element,
// strings code unit by code unit). Expected values are read at runtime from the goldens JSON; nothing is copied here.
// Goldens: $TOKENIZERS_GOLDENS_DIR or <ProjectDir>/../.pipelines-dev/goldens/tokenizers (make with Tools/make_tokenizer_goldens.py).

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformMisc.h"
#include "Containers/StringConv.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/StrongObjectPtr.h"
#include "TokenizerWrapper.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TokenizerParitySpecPrivate
{
	struct FGoldenCase
	{
		FString Id;
		FString Text;
		FString TextUtf8Hex;
		TArray<int32> IdsWith;
		TArray<int32> IdsWithout;
		FString DecodeKeep;
		FString DecodeSkip;
	};

	struct FGoldenBatch
	{
		FString Id;
		TArray<FString> CaseIds;
		bool bAddSpecial = false;
		int32 MaxLength = 0;
		int32 PadId = 0;
		int32 Rows = 0;
		int32 SeqLen = 0;
		TArray<int32> InputIds;
		TArray<int32> Mask;
	};

	struct FGoldenSet
	{
		FString Name;
		FString GoldenPath;
		FString TokenizerPath;
		FString Schema;
		TArray<FGoldenCase> Cases;
		TArray<FGoldenBatch> Batches;
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

	static bool ReadIntArray(const TSharedPtr<FJsonValue>& Value, TArray<int32>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Value.IsValid() || !Value->TryGetArray(Arr) || !Arr) { return false; }
		Out.Reset();
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			double D = 0.0;
			if (!V.IsValid() || !V->TryGetNumber(D)) { return false; }
			Out.Add((int32)D);
		}
		return true;
	}

	static bool ReadIntArrayField(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, TArray<int32>& Out)
	{
		return Obj->HasField(Field) && ReadIntArray(Obj->TryGetField(Field), Out);
	}

	// Reads a rows x cols matrix, flattened row-major. All rows must have the same length.
	static bool ReadMatrix(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, int32& OutRows, int32& OutCols, TArray<int32>& OutFlat)
	{
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (!Obj->TryGetArrayField(Field, Rows) || !Rows) { return false; }
		OutRows = Rows->Num();
		OutCols = 0;
		OutFlat.Reset();
		for (int32 r = 0; r < Rows->Num(); ++r)
		{
			TArray<int32> Row;
			if (!ReadIntArray((*Rows)[r], Row)) { return false; }
			if (r == 0) { OutCols = Row.Num(); }
			else if (Row.Num() != OutCols) { return false; }
			OutFlat.Append(Row);
		}
		return true;
	}

	// Loads goldens for `Name`. On failure returns false and OutError says why (path included).
	static bool LoadGoldens(const FString& Name, FGoldenSet& Out, FString& OutError)
	{
		const FString Dir = GoldensDir();
		Out.Name = Name;
		Out.GoldenPath = FPaths::ConvertRelativePathToFull(Dir / (Name + TEXT(".json")));
		const FString NotFound = FString::Printf(TEXT("goldens not found at %s; run Tools/make_tokenizer_goldens.py"), *Out.GoldenPath);

		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Out.GoldenPath))
		{
			OutError = NotFound;
			return false;
		}
		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = FString::Printf(TEXT("goldens at %s are not valid JSON"), *Out.GoldenPath);
			return false;
		}
		Root->TryGetStringField(TEXT("schema"), Out.Schema);

		const TSharedPtr<FJsonObject>* TokObj = nullptr;
		FString RelFile;
		if (!Root->TryGetObjectField(TEXT("tokenizer"), TokObj) || !TokObj || !(*TokObj)->TryGetStringField(TEXT("file"), RelFile))
		{
			OutError = FString::Printf(TEXT("goldens at %s lack tokenizer.file"), *Out.GoldenPath);
			return false;
		}
		Out.TokenizerPath = FPaths::ConvertRelativePathToFull(FPaths::GetPath(Out.GoldenPath) / RelFile);

		const TArray<TSharedPtr<FJsonValue>>* CasesArr = nullptr;
		if (!Root->TryGetArrayField(TEXT("cases"), CasesArr) || !CasesArr)
		{
			OutError = FString::Printf(TEXT("goldens at %s lack 'cases'"), *Out.GoldenPath);
			return false;
		}
		for (const TSharedPtr<FJsonValue>& V : *CasesArr)
		{
			const TSharedPtr<FJsonObject>* CObj = nullptr;
			if (!V.IsValid() || !V->TryGetObject(CObj) || !CObj) { OutError = TEXT("a case is not an object"); return false; }
			FGoldenCase C;
			if (!(*CObj)->TryGetStringField(TEXT("id"), C.Id)
				|| !(*CObj)->TryGetStringField(TEXT("text"), C.Text)
				|| !(*CObj)->TryGetStringField(TEXT("text_utf8_hex"), C.TextUtf8Hex)
				|| !(*CObj)->TryGetStringField(TEXT("decode_with_special_keep"), C.DecodeKeep)
				|| !(*CObj)->TryGetStringField(TEXT("decode_with_special_skip"), C.DecodeSkip)
				|| !ReadIntArrayField(*CObj, TEXT("ids_with_special"), C.IdsWith)
				|| !ReadIntArrayField(*CObj, TEXT("ids_without_special"), C.IdsWithout))
			{
				OutError = FString::Printf(TEXT("case #%d in %s is missing a field"), Out.Cases.Num(), *Out.GoldenPath);
				return false;
			}
			Out.Cases.Add(MoveTemp(C));
		}

		const TArray<TSharedPtr<FJsonValue>>* BatchArr = nullptr;
		if (!Root->TryGetArrayField(TEXT("batches"), BatchArr) || !BatchArr)
		{
			OutError = FString::Printf(TEXT("goldens at %s lack 'batches'"), *Out.GoldenPath);
			return false;
		}
		for (const TSharedPtr<FJsonValue>& V : *BatchArr)
		{
			const TSharedPtr<FJsonObject>* BObj = nullptr;
			if (!V.IsValid() || !V->TryGetObject(BObj) || !BObj) { OutError = TEXT("a batch is not an object"); return false; }
			FGoldenBatch B;
			int32 MaskRows = 0, MaskCols = 0;
			const TArray<TSharedPtr<FJsonValue>>* Ids = nullptr;
			if (!(*BObj)->TryGetStringField(TEXT("id"), B.Id)
				|| !(*BObj)->TryGetBoolField(TEXT("add_special_tokens"), B.bAddSpecial)
				|| !(*BObj)->TryGetNumberField(TEXT("pad_id"), B.PadId)
				|| !(*BObj)->TryGetArrayField(TEXT("case_ids"), Ids) || !Ids
				|| !ReadMatrix(*BObj, TEXT("input_ids"), B.Rows, B.SeqLen, B.InputIds)
				|| !ReadMatrix(*BObj, TEXT("attention_mask"), MaskRows, MaskCols, B.Mask)
				|| MaskRows != B.Rows || MaskCols != B.SeqLen)
			{
				OutError = FString::Printf(TEXT("batch #%d in %s is malformed"), Out.Batches.Num(), *Out.GoldenPath);
				return false;
			}
			// max_length: JSON null means "no truncation" = 0.
			B.MaxLength = 0;
			if ((*BObj)->HasTypedField<EJson::Number>(TEXT("max_length")))
			{
				(*BObj)->TryGetNumberField(TEXT("max_length"), B.MaxLength);
			}
			for (const TSharedPtr<FJsonValue>& IdV : *Ids)
			{
				FString S;
				if (!IdV.IsValid() || !IdV->TryGetString(S)) { OutError = TEXT("batch case_ids entry is not a string"); return false; }
				B.CaseIds.Add(S);
			}
			Out.Batches.Add(MoveTemp(B));
		}
		return true;
	}

	static const FGoldenCase* FindCase(const FGoldenSet& Set, const FString& Id)
	{
		return Set.Cases.FindByPredicate([&Id](const FGoldenCase& C) { return C.Id == Id; });
	}

	static bool HasNonAscii(const FString& S)
	{
		for (TCHAR C : S) { if (C > 127) { return true; } }
		return false;
	}

	static FString Utf8Hex(const FString& S)
	{
		FTCHARToUTF8 Conv(*S, S.Len());
		FString Out;
		const uint8* Bytes = reinterpret_cast<const uint8*>(Conv.Get());
		for (int32 i = 0; i < Conv.Length(); ++i) { Out += FString::Printf(TEXT("%02x"), Bytes[i]); }
		return Out;
	}

	static TStrongObjectPtr<UTokenizerWrapper> MakeTokenizer(const FGoldenSet& Set, FString& OutError)
	{
		TStrongObjectPtr<UTokenizerWrapper> Ptr(NewObject<UTokenizerWrapper>(GetTransientPackage()));
		if (!Ptr.IsValid()) { OutError = TEXT("could not create UTokenizerWrapper"); return Ptr; }
		if (!Ptr->InitializeTokenizerFromFile(Set.TokenizerPath))
		{
			OutError = FString::Printf(TEXT("InitializeTokenizerFromFile(%s) failed: %s. Fetch models with Tools/fetch_models.py (see CLAUDE.md)"),
				*Set.TokenizerPath, *Ptr->GetLastError());
			Ptr.Reset();
		}
		return Ptr;
	}
}

BEGIN_DEFINE_SPEC(FTokenizerParitySpec, "Tokenizers.Parity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	// Loads goldens + tokenizer; AddError and return false on any problem.
	bool Prepare(const FString& Name, TokenizerParitySpecPrivate::FGoldenSet& Set, TStrongObjectPtr<UTokenizerWrapper>& Tok)
	{
		using namespace TokenizerParitySpecPrivate;
		FString Err;
		if (!LoadGoldens(Name, Set, Err)) { AddError(FString::Printf(TEXT("[%s] %s"), *Name, *Err)); return false; }
		Tok = MakeTokenizer(Set, Err);
		if (!Tok.IsValid()) { AddError(FString::Printf(TEXT("[%s] %s"), *Name, *Err)); return false; }
		return true;
	}

	void Summary(const FString& Name, const TCHAR* What, int32 Matched, int32 Total)
	{
		AddInfo(FString::Printf(TEXT("[%s] %s: %d/%d cases matched"), *Name, What, Matched, Total));
	}
END_DEFINE_SPEC(FTokenizerParitySpec)

void FTokenizerParitySpec::Define()
{
	using namespace TokenizerParitySpecPrivate;

	const TArray<FString> Names = { TEXT("bert-base-uncased"), TEXT("gpt2"), TEXT("t5-small") };
	for (const FString& NameRef : Names)
	{
		const FString Name = NameRef;
		Describe(Name, [this, Name]()
		{
			It("Loads", [this, Name]()
			{
				FGoldenSet Set;
				FString Err;
				if (!LoadGoldens(Name, Set, Err)) { AddError(FString::Printf(TEXT("[%s] %s"), *Name, *Err)); return; }
				TestEqual(FString::Printf(TEXT("[%s] schema"), *Name), Set.Schema, FString(TEXT("pipelines-tokenizer-goldens/1")));
				TestTrue(FString::Printf(TEXT("[%s] at least 20 cases (got %d)"), *Name, Set.Cases.Num()), Set.Cases.Num() >= 20);
				int32 NonAscii = 0;
				for (const FGoldenCase& C : Set.Cases) { if (HasNonAscii(C.Text)) { ++NonAscii; } }
				TestTrue(FString::Printf(TEXT("[%s] at least 3 non-ASCII cases (got %d)"), *Name, NonAscii), NonAscii >= 3);
				TestTrue(FString::Printf(TEXT("[%s] at least 1 batch (got %d)"), *Name, Set.Batches.Num()), Set.Batches.Num() >= 1);
				MakeTokenizer(Set, Err);
				if (!Err.IsEmpty()) { AddError(FString::Printf(TEXT("[%s] %s"), *Name, *Err)); }
				AddInfo(FString::Printf(TEXT("[%s] %d cases (%d non-ASCII), %d batches, tokenizer %s"), *Name, Set.Cases.Num(), NonAscii,
					Set.Batches.Num(), *Set.TokenizerPath));
			});

			It("TextRoundTrip", [this, Name]()
			{
				FGoldenSet Set;
				FString Err;
				if (!LoadGoldens(Name, Set, Err)) { AddError(FString::Printf(TEXT("[%s] %s"), *Name, *Err)); return; }
				int32 Ok = 0;
				for (const FGoldenCase& C : Set.Cases)
				{
					// A JSON parse that altered the text (surrogates, escapes) would make every later mismatch meaningless.
					const FString Actual = Utf8Hex(C.Text);
					if (Actual.Equals(C.TextUtf8Hex, ESearchCase::IgnoreCase)) { ++Ok; }
					else { AddError(FString::Printf(TEXT("[%s] %s: UTF-8 of parsed text differs. expected hex=%s actual hex=%s"), *Name, *C.Id, *C.TextUtf8Hex, *Actual)); }
				}
				Summary(Name, TEXT("TextRoundTrip"), Ok, Set.Cases.Num());
			});

			It("EncodeWithSpecial", [this, Name]()
			{
				FGoldenSet Set;
				TStrongObjectPtr<UTokenizerWrapper> Tok;
				if (!Prepare(Name, Set, Tok)) { return; }
				int32 Ok = 0;
				for (const FGoldenCase& C : Set.Cases)
				{
					const TArray<int32> Actual = Tok->Encode(C.Text, true);
					if (Actual == C.IdsWith) { ++Ok; }
					else { AddError(FString::Printf(TEXT("[%s] %s: Encode(text, true) text='%s' expected=%s actual=%s"), *Name, *C.Id, *Escape(C.Text), *IdsToString(C.IdsWith), *IdsToString(Actual))); }
				}
				Summary(Name, TEXT("EncodeWithSpecial"), Ok, Set.Cases.Num());
			});

			It("EncodeWithoutSpecial", [this, Name]()
			{
				FGoldenSet Set;
				TStrongObjectPtr<UTokenizerWrapper> Tok;
				if (!Prepare(Name, Set, Tok)) { return; }
				int32 Ok = 0;
				for (const FGoldenCase& C : Set.Cases)
				{
					const TArray<int32> Actual = Tok->Encode(C.Text, false);
					if (Actual == C.IdsWithout) { ++Ok; }
					else { AddError(FString::Printf(TEXT("[%s] %s: Encode(text, false) text='%s' expected=%s actual=%s"), *Name, *C.Id, *Escape(C.Text), *IdsToString(C.IdsWithout), *IdsToString(Actual))); }
				}
				Summary(Name, TEXT("EncodeWithoutSpecial"), Ok, Set.Cases.Num());
			});

			It("DecodeKeep", [this, Name]()
			{
				FGoldenSet Set;
				TStrongObjectPtr<UTokenizerWrapper> Tok;
				if (!Prepare(Name, Set, Tok)) { return; }
				int32 Ok = 0;
				for (const FGoldenCase& C : Set.Cases)
				{
					const FString Actual = Tok->Decode(C.IdsWith, false);
					if (Actual.Equals(C.DecodeKeep, ESearchCase::CaseSensitive)) { ++Ok; }
					else { AddError(FString::Printf(TEXT("[%s] %s: Decode(ids_with_special, false) expected='%s' actual='%s'"), *Name, *C.Id, *Escape(C.DecodeKeep), *Escape(Actual))); }
				}
				Summary(Name, TEXT("DecodeKeep"), Ok, Set.Cases.Num());
			});

			It("DecodeSkip", [this, Name]()
			{
				FGoldenSet Set;
				TStrongObjectPtr<UTokenizerWrapper> Tok;
				if (!Prepare(Name, Set, Tok)) { return; }
				int32 Ok = 0;
				for (const FGoldenCase& C : Set.Cases)
				{
					const FString Actual = Tok->Decode(C.IdsWith, true);
					if (Actual.Equals(C.DecodeSkip, ESearchCase::CaseSensitive)) { ++Ok; }
					else { AddError(FString::Printf(TEXT("[%s] %s: Decode(ids_with_special, true) expected='%s' actual='%s'"), *Name, *C.Id, *Escape(C.DecodeSkip), *Escape(Actual))); }
				}
				Summary(Name, TEXT("DecodeSkip"), Ok, Set.Cases.Num());
			});

			It("Batches", [this, Name]()
			{
				FGoldenSet Set;
				TStrongObjectPtr<UTokenizerWrapper> Tok;
				if (!Prepare(Name, Set, Tok)) { return; }
				int32 Ok = 0;
				for (const FGoldenBatch& B : Set.Batches)
				{
					TArray<FString> Texts;
					bool bMissing = false;
					for (const FString& CaseId : B.CaseIds)
					{
						const FGoldenCase* C = FindCase(Set, CaseId);
						if (!C) { AddError(FString::Printf(TEXT("[%s] %s: case id %s not found in goldens"), *Name, *B.Id, *CaseId)); bMissing = true; break; }
						Texts.Add(C->Text);
					}
					if (bMissing) { continue; }

					FTokenizedBatch Out;
					const bool bRet = Tok->EncodeBatch(Texts, Out, B.bAddSpecial, B.MaxLength, B.PadId);
					bool bGood = true;
					if (!bRet)
					{
						AddError(FString::Printf(TEXT("[%s] %s: EncodeBatch returned false: %s"), *Name, *B.Id, *Tok->GetLastError()));
						bGood = false;
					}
					if (Out.NumRows != B.Rows || Out.SeqLen != B.SeqLen)
					{
						AddError(FString::Printf(TEXT("[%s] %s: shape expected rows=%d seq=%d actual rows=%d seq=%d"), *Name, *B.Id, B.Rows, B.SeqLen, Out.NumRows, Out.SeqLen));
						bGood = false;
					}
					if (Out.InputIds != B.InputIds)
					{
						AddError(FString::Printf(TEXT("[%s] %s: InputIds (special=%d max_length=%d pad=%d) expected=%s actual=%s"), *Name, *B.Id,
							B.bAddSpecial ? 1 : 0, B.MaxLength, B.PadId, *IdsToString(B.InputIds), *IdsToString(Out.InputIds)));
						bGood = false;
					}
					if (Out.AttentionMask != B.Mask)
					{
						AddError(FString::Printf(TEXT("[%s] %s: AttentionMask expected=%s actual=%s"), *Name, *B.Id, *IdsToString(B.Mask), *IdsToString(Out.AttentionMask)));
						bGood = false;
					}
					if (bGood) { ++Ok; }
				}
				AddInfo(FString::Printf(TEXT("[%s] Batches: %d/%d batches matched"), *Name, Ok, Set.Batches.Num()));
				TestTrue(FString::Printf(TEXT("[%s] goldens contain batches"), *Name), Set.Batches.Num() > 0);
			});
		});
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
