// WP0.3 wrapper hardening tests. Written from the brief's Contract/Acceptance, not from the implementation.
// Expected values were computed with Python `tokenizers` 0.22.2 (see docs/briefs/WP0.3.md).

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/Package.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/StrongObjectPtr.h"
#include "TokenizerWrapper.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TokenizerWrapperSpecPrivate
{
	// Test tokenizer B: WordLevel, vocab [UNK]=0 hello=1 world=2 [CLS]=3 [SEP]=4 cafe(acute)=5 Tokyo(kanji)=6,
	// whitespace pre-tokenizer, template "[CLS] $A [SEP]".
	static const char* GTokenizerBJson = R"JSON({"version":"1.0","truncation":null,"padding":null,"added_tokens":[{"id":3,"content":"[CLS]","single_word":false,"lstrip":false,"rstrip":false,"normalized":false,"special":true},{"id":4,"content":"[SEP]","single_word":false,"lstrip":false,"rstrip":false,"normalized":false,"special":true}],"normalizer":null,"pre_tokenizer":{"type":"Whitespace"},"post_processor":{"type":"TemplateProcessing","single":[{"SpecialToken":{"id":"[CLS]","type_id":0}},{"Sequence":{"id":"A","type_id":0}},{"SpecialToken":{"id":"[SEP]","type_id":0}}],"pair":[{"SpecialToken":{"id":"[CLS]","type_id":0}},{"Sequence":{"id":"A","type_id":0}},{"SpecialToken":{"id":"[SEP]","type_id":0}},{"Sequence":{"id":"B","type_id":1}},{"SpecialToken":{"id":"[SEP]","type_id":1}}],"special_tokens":{"[CLS]":{"id":"[CLS]","ids":[3],"tokens":["[CLS]"]},"[SEP]":{"id":"[SEP]","ids":[4],"tokens":["[SEP]"]}}},"decoder":null,"model":{"type":"WordLevel","vocab":{"[UNK]":0,"hello":1,"world":2,"[CLS]":3,"[SEP]":4,"café":5,"東京":6},"unk_token":"[UNK]"}})JSON";

	static FString BJson()
	{
		return FString(UTF8_TO_TCHAR(GTokenizerBJson));
	}

	// "cafe-acute Tokyo-kanji" built from UTF-8 escapes so the source file encoding cannot matter.
	static FString CafeTokyo()
	{
		return FString(UTF8_TO_TCHAR("caf" "\xC3\xA9" " " "\xE6\x9D\xB1" "\xE4\xBA\xAC"));
	}

	// "h-acute llo w-umlaut rld Tokyo-kanji slightly-smiling-face (U+1F642)"
	static FString HelloWorldUnicode()
	{
		return FString(UTF8_TO_TCHAR(
			"h" "\xC3\xA9" "llo w" "\xC3\xB6" "rld " "\xE6\x9D\xB1" "\xE4\xBA\xAC" " " "\xF0\x9F\x99\x82"));
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

	// Escapes non-printable/non-ASCII characters as \uXXXX so failure messages are readable and encoding-safe.
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

	static FString TempRoot()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("TokenizersTests"));
	}

	static FString ContentTempRoot()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("TokenizersTests_tmp"));
	}
}

BEGIN_DEFINE_SPEC(FTokenizerWrapperSpec, "Tokenizers.Wrapper",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	TStrongObjectPtr<UTokenizerWrapper> Wrapper;

	void CheckIds(const TCHAR* CaseName, const TArray<int32>& Expected, const TArray<int32>& Actual)
	{
		TestTrue(
			FString::Printf(TEXT("Case '%s': ids differ. expected=%s actual=%s"),
				CaseName, *TokenizerWrapperSpecPrivate::IdsToString(Expected),
				*TokenizerWrapperSpecPrivate::IdsToString(Actual)),
			Expected == Actual);
	}

	void CheckString(const TCHAR* CaseName, const FString& Expected, const FString& Actual)
	{
		TestTrue(
			FString::Printf(TEXT("Case '%s': string differs. expected='%s' actual='%s'"),
				CaseName, *TokenizerWrapperSpecPrivate::Escape(Expected),
				*TokenizerWrapperSpecPrivate::Escape(Actual)),
			Expected.Equals(Actual, ESearchCase::CaseSensitive));
	}

	bool InitB(const TCHAR* CaseName)
	{
		return TestTrue(
			FString::Printf(TEXT("Case '%s': init with tokenizer B should succeed. error='%s'"),
				CaseName, *Wrapper->GetLastError()),
			Wrapper->InitializeTokenizerFromJson(TokenizerWrapperSpecPrivate::BJson()));
	}

	bool InitA(const TCHAR* CaseName)
	{
		return TestTrue(
			FString::Printf(TEXT("Case '%s': init with bundled tokenizer.json (A) should succeed. error='%s'"),
				CaseName, *Wrapper->GetLastError()),
			Wrapper->InitializeTokenizerFromFile(TEXT("tokenizer.json")));
	}
END_DEFINE_SPEC(FTokenizerWrapperSpec)

void FTokenizerWrapperSpec::Define()
{
	using namespace TokenizerWrapperSpecPrivate;

	BeforeEach([this]()
	{
		Wrapper.Reset(NewObject<UTokenizerWrapper>());
	});

	AfterEach([this]()
	{
		Wrapper.Reset();
		// Clean up even when an assertion failed.
		IFileManager::Get().DeleteDirectory(*TempRoot(), false, true);
		IFileManager::Get().DeleteDirectory(*ContentTempRoot(), false, true);
	});

	Describe("SpecialTokens", [this]()
	{
		It("EncodeWithoutSpecials", [this]()
		{
			if (!InitB(TEXT("NoSpecials"))) { return; }
			// Old code hard-coded specials off for every call; this is the control case.
			CheckIds(TEXT("hello world, no specials"), {1, 2}, Wrapper->Encode(TEXT("hello world"), false));
			CheckIds(TEXT("hello world, default arg"), {1, 2}, Wrapper->Encode(TEXT("hello world")));
		});

		It("EncodeWithSpecials", [this]()
		{
			if (!InitB(TEXT("WithSpecials"))) { return; }
			// A wrapper that still ignores bAddSpecialTokens returns [1,2] here.
			CheckIds(TEXT("hello world, specials"), {3, 1, 2, 4}, Wrapper->Encode(TEXT("hello world"), true));
		});

		It("EmptyTextWithSpecials", [this]()
		{
			if (!InitB(TEXT("EmptyWithSpecials"))) { return; }
			CheckIds(TEXT("empty, specials"), {3, 4}, Wrapper->Encode(FString(), true));
			CheckString(TEXT("empty, specials: error must be empty"), FString(), Wrapper->GetLastError());
		});

		It("UnknownWordMapsToUnk", [this]()
		{
			if (!InitB(TEXT("Unk"))) { return; }
			CheckIds(TEXT("hello zzz, specials"), {3, 1, 0, 4}, Wrapper->Encode(TEXT("hello zzz"), true));
		});
	});

	Describe("DecodeKeepSkip", [this]()
	{
		It("KeepsSpecialsByDefault", [this]()
		{
			if (!InitB(TEXT("DecodeKeep"))) { return; }
			CheckString(TEXT("Decode [3,1,2,4] keep"), TEXT("[CLS] hello world [SEP]"),
				Wrapper->Decode({3, 1, 2, 4}, false));
		});

		It("SkipsSpecialsWhenAsked", [this]()
		{
			if (!InitB(TEXT("DecodeSkip"))) { return; }
			CheckString(TEXT("Decode [3,1,2,4] skip"), TEXT("hello world"), Wrapper->Decode({3, 1, 2, 4}, true));
		});

		It("NonAsciiWithSkip", [this]()
		{
			if (!InitB(TEXT("DecodeNonAscii"))) { return; }
			CheckString(TEXT("Decode [3,5,6,4] skip"), CafeTokyo(), Wrapper->Decode({3, 5, 6, 4}, true));
		});
	});

	Describe("DecodeUsesLength", [this]()
	{
		It("NonAsciiAndEmojiRoundTrip", [this]()
		{
			if (!InitA(TEXT("DecodeUnicode"))) { return; }
			// Decode ignoring the byte length reads past the buffer (mojibake/garbage) or splits the
			// 4-byte emoji into a wrong UTF-16 surrogate pair.
			const FString Actual = Wrapper->Decode({73, 860, 48620, 259, 6592, 392, 209, 12676, 111, 5494, 107, 42908});
			CheckString(TEXT("decode unicode round trip"), HelloWorldUnicode(), Actual);
			// Explicitly check surrogate pair for U+1F642 at the end: D83D DE42.
			if (Actual.Len() >= 2)
			{
				TestEqual(TEXT("high surrogate of emoji should be 0xD83D"), (int32)Actual[Actual.Len() - 2], 0xD83D);
				TestEqual(TEXT("low surrogate of emoji should be 0xDE42"), (int32)Actual[Actual.Len() - 1], 0xDE42);
			}
			else
			{
				AddError(FString::Printf(TEXT("decoded string too short: '%s'"), *Escape(Actual)));
			}
		});

		It("EndOfTextKeptAndSkipped", [this]()
		{
			if (!InitA(TEXT("DecodeEot"))) { return; }
			CheckString(TEXT("decode [0,5801] keep"), TEXT("<|endoftext|>hi"), Wrapper->Decode({0, 5801}, false));
			CheckString(TEXT("decode [0,5801] skip"), TEXT("hi"), Wrapper->Decode({0, 5801}, true));
		});

		It("EmptyIdsGivesEmptyStringAndNoError", [this]()
		{
			if (!InitA(TEXT("DecodeEmpty"))) { return; }
			// Make the previous call fail so we know the empty decode really reset the error.
			Wrapper->Decode({-1});
			CheckString(TEXT("decode [] result"), FString(), Wrapper->Decode(TArray<int32>()));
			CheckString(TEXT("decode [] error must be empty"), FString(), Wrapper->GetLastError());
		});

		It("PartialUtf8ByteTokenGivesOneReplacementChar", [this]()
		{
			if (!InitA(TEXT("DecodePartial"))) { return; }
			FString Expected;
			Expected.AppendChar(0xFFFD);
			// Token 14931 + 225 are the first bytes of a multi-byte sequence only. One U+FFFD, no crash.
			CheckString(TEXT("decode [14931,225]"), Expected, Wrapper->Decode({14931, 225}));
		});

		It("RepeatedDecodesDoNotLeakPreviousResult", [this]()
		{
			if (!InitA(TEXT("DecodeRepeat"))) { return; }
			// The library buffer is only valid until the next decode; a stale/un-length'd read shows old tail bytes.
			const FString Long = Wrapper->Decode({73, 860, 48620, 259, 6592, 392, 209, 12676, 111, 5494, 107, 42908});
			CheckString(TEXT("long decode"), HelloWorldUnicode(), Long);
			CheckString(TEXT("short decode after long"), TEXT("hi"), Wrapper->Decode({5801}));
		});
	});

	Describe("NegativeId", [this]()
	{
		It("RejectedWithErrorAndNoCrash", [this]()
		{
			if (!InitB(TEXT("NegId"))) { return; }
			CheckString(TEXT("decode [1,-5] result"), FString(), Wrapper->Decode({1, -5}));
			TestFalse(TEXT("decode [1,-5]: GetLastError() must be non-empty"), Wrapper->GetLastError().IsEmpty());
		});

		It("NegativeAtFrontAlsoRejected", [this]()
		{
			if (!InitB(TEXT("NegIdFront"))) { return; }
			CheckString(TEXT("decode [-1,1] result"), FString(), Wrapper->Decode({-1, 1}));
			TestFalse(TEXT("decode [-1,1]: GetLastError() must be non-empty"), Wrapper->GetLastError().IsEmpty());
		});
	});

	Describe("NotInitialized", [this]()
	{
		It("FreshObjectIsNotInitialized", [this]()
		{
			TestFalse(TEXT("fresh object IsInitialized() must be false"), Wrapper->IsInitialized());
			TestTrue(TEXT("fresh object GetLastError() should be empty"), Wrapper->GetLastError().IsEmpty());
		});

		It("EncodeGivesEmptyArrayAndError", [this]()
		{
			CheckIds(TEXT("uninitialized encode"), {}, Wrapper->Encode(TEXT("hello")));
			TestFalse(TEXT("uninitialized encode: GetLastError() must be non-empty"), Wrapper->GetLastError().IsEmpty());
		});

		It("DecodeGivesEmptyStringAndError", [this]()
		{
			CheckString(TEXT("uninitialized decode"), FString(), Wrapper->Decode({1, 2}));
			TestFalse(TEXT("uninitialized decode: GetLastError() must be non-empty"), Wrapper->GetLastError().IsEmpty());
		});

		It("DecodeEmptyIdsAlsoErrors", [this]()
		{
			CheckString(TEXT("uninitialized decode []"), FString(), Wrapper->Decode(TArray<int32>()));
			TestFalse(TEXT("uninitialized decode []: GetLastError() must be non-empty"),
				Wrapper->GetLastError().IsEmpty());
		});
	});

	Describe("BadJson", [this]()
	{
		It("FailsOnFreshObject", [this]()
		{
			AddExpectedError(TEXT("InitializeTokenizerFromJson"), EAutomationExpectedErrorFlags::Contains, 0);
			TestFalse(TEXT("bad JSON must return false"), Wrapper->InitializeTokenizerFromJson(TEXT("{not json")));
			TestFalse(TEXT("IsInitialized() must stay false after bad JSON"), Wrapper->IsInitialized());
			TestFalse(TEXT("bad JSON: GetLastError() must be non-empty"), Wrapper->GetLastError().IsEmpty());
		});

		It("EmptyJsonStringFails", [this]()
		{
			AddExpectedError(TEXT("InitializeTokenizerFromJson"), EAutomationExpectedErrorFlags::Contains, 0);
			TestFalse(TEXT("empty JSON must return false"), Wrapper->InitializeTokenizerFromJson(FString()));
			TestFalse(TEXT("IsInitialized() must be false after empty JSON"), Wrapper->IsInitialized());
			TestFalse(TEXT("empty JSON: GetLastError() must be non-empty"), Wrapper->GetLastError().IsEmpty());
		});

		It("PreviousTokenizerSurvivesBadJson", [this]()
		{
			AddExpectedError(TEXT("InitializeTokenizerFromJson"), EAutomationExpectedErrorFlags::Contains, 0);
			if (!InitB(TEXT("SurviveBadJson"))) { return; }
			TestFalse(TEXT("bad JSON on initialized object must return false"),
				Wrapper->InitializeTokenizerFromJson(TEXT("{not json")));
			TestTrue(TEXT("IsInitialized() must stay true: the previous tokenizer is kept"), Wrapper->IsInitialized());
			TestFalse(TEXT("bad JSON: GetLastError() must be non-empty"), Wrapper->GetLastError().IsEmpty());
			// A wrapper that freed the old handle before parsing would return [] or crash here.
			CheckIds(TEXT("B still works after failed re-init"), {1, 2}, Wrapper->Encode(TEXT("hello world")));
		});
	});

	Describe("ReInit", [this]()
	{
		It("ReplacesTokenizerWithAAfterB", [this]()
		{
			if (!InitB(TEXT("ReInitB"))) { return; }
			CheckIds(TEXT("B before"), {1, 2}, Wrapper->Encode(TEXT("hello world")));
			if (!InitA(TEXT("ReInitA"))) { return; }
			// B would give [1,2]; in A "Hello world" is [12092, 1533].
			CheckIds(TEXT("A after re-init"), {12092, 1533}, Wrapper->Encode(TEXT("Hello world")));
		});

		It("ReplacesTokenizerWithBAfterA", [this]()
		{
			if (!InitA(TEXT("ReInitA2"))) { return; }
			if (!InitB(TEXT("ReInitB2"))) { return; }
			CheckIds(TEXT("B after A"), {3, 1, 2, 4}, Wrapper->Encode(TEXT("hello world"), true));
		});

		It("ManyReInitsStayUsable", [this]()
		{
			// Repeated replacement exercises free-old-handle; a double free or use-after-free would crash here.
			for (int32 i = 0; i < 20; ++i)
			{
				if (!InitB(TEXT("LoopB"))) { return; }
			}
			CheckIds(TEXT("after 20 re-inits"), {1, 2}, Wrapper->Encode(TEXT("hello world")));
		});
	});

	Describe("PathResolution", [this]()
	{
		It("AbsolutePath", [this]()
		{
			const FString Path = TempRoot() / TEXT("b_abs.json");
			if (!TestTrue(TEXT("test setup: write temp file"),
				FFileHelper::SaveStringToFile(BJson(), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)))
			{
				return;
			}
			TestTrue(FString::Printf(TEXT("absolute path '%s' should load. error='%s'"), *Path, *Wrapper->GetLastError()),
				Wrapper->InitializeTokenizerFromFile(Path));
			CheckIds(TEXT("abs path tokenizer works"), {3, 1, 2, 4}, Wrapper->Encode(TEXT("hello world"), true));
		});

		It("ProjectContentRelativePath", [this]()
		{
			const FString Path = ContentTempRoot() / TEXT("b.json");
			if (!TestTrue(TEXT("test setup: write temp file under Content"),
				FFileHelper::SaveStringToFile(BJson(), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)))
			{
				return;
			}
			TestTrue(FString::Printf(TEXT("relative 'TokenizersTests_tmp/b.json' should load from ProjectContentDir. error='%s'"),
				*Wrapper->GetLastError()),
				Wrapper->InitializeTokenizerFromFile(TEXT("TokenizersTests_tmp/b.json")));
			CheckIds(TEXT("content-relative tokenizer works"), {1, 2}, Wrapper->Encode(TEXT("hello world")));
		});

		It("PluginContentFallback", [this]()
		{
			TestTrue(FString::Printf(TEXT("'tokenizer.json' should resolve via plugin Content. error='%s'"),
				*Wrapper->GetLastError()),
				Wrapper->InitializeTokenizerFromFile(TEXT("tokenizer.json")));
			CheckIds(TEXT("plugin content tokenizer is A"), {12092, 1533}, Wrapper->Encode(TEXT("Hello world")));
		});

		It("MissingFileNamesPathInError", [this]()
		{
			AddExpectedError(TEXT("InitializeTokenizerFromFile"), EAutomationExpectedErrorFlags::Contains, 0);
			TestFalse(TEXT("missing file must return false"),
				Wrapper->InitializeTokenizerFromFile(TEXT("does/not/exist.json")));
			const FString Err = Wrapper->GetLastError();
			TestTrue(FString::Printf(TEXT("error must contain 'does/not/exist.json'. actual='%s'"), *Err),
				Err.Contains(TEXT("does/not/exist.json")));
			TestFalse(TEXT("IsInitialized() must stay false"), Wrapper->IsInitialized());
		});

		It("MissingFileKeepsPreviousTokenizer", [this]()
		{
			AddExpectedError(TEXT("InitializeTokenizerFromFile"), EAutomationExpectedErrorFlags::Contains, 0);
			if (!InitB(TEXT("KeepOnMissing"))) { return; }
			TestFalse(TEXT("missing file must return false"),
				Wrapper->InitializeTokenizerFromFile(TEXT("does/not/exist.json")));
			CheckIds(TEXT("B still works after missing-file re-init"), {1, 2}, Wrapper->Encode(TEXT("hello world")));
		});

		It("FileWithGarbageContentFails", [this]()
		{
			AddExpectedError(TEXT("InitializeTokenizerFromFile"), EAutomationExpectedErrorFlags::Contains, 0);
			const FString Path = TempRoot() / TEXT("garbage.json");
			if (!TestTrue(TEXT("test setup: write temp file"),
				FFileHelper::SaveStringToFile(TEXT("{not json"), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)))
			{
				return;
			}
			TestFalse(TEXT("file with invalid JSON must return false"), Wrapper->InitializeTokenizerFromFile(Path));
			TestFalse(TEXT("GetLastError() must be non-empty"), Wrapper->GetLastError().IsEmpty());
			TestFalse(TEXT("IsInitialized() must be false"), Wrapper->IsInitialized());
		});

		It("Utf8BomFileLoads", [this]()
		{
			// Acceptance 8(e) (ruling 2026-09-30): a leading EF BB BF is skipped, so the file loads.
			// A wrapper that passed the BOM to the Rust parser would fail here with a JSON error.
			const FString Path = TempRoot() / TEXT("b_bom.json");
			// Confirmed in FileHelper.h: ForceUTF8 writes a BOM. Verify the bytes anyway.
			if (!TestTrue(TEXT("test setup: write BOM file"),
				FFileHelper::SaveStringToFile(BJson(), *Path, FFileHelper::EEncodingOptions::ForceUTF8)))
			{
				return;
			}
			TArray<uint8> Bytes;
			const bool bLoaded = FFileHelper::LoadFileToArray(Bytes, *Path);
			if (!TestTrue(TEXT("test setup: read back BOM file"), bLoaded) ||
				!TestTrue(TEXT("test setup: file has at least 3 bytes"), Bytes.Num() >= 3))
			{
				return;
			}
			const bool bHasBom = Bytes[0] == 0xEF && Bytes[1] == 0xBB && Bytes[2] == 0xBF;
			if (!TestTrue(FString::Printf(TEXT("test setup: file must start with EF BB BF, actual %02X %02X %02X"),
				Bytes[0], Bytes[1], Bytes[2]), bHasBom))
			{
				return;
			}
			const bool bOk = Wrapper->InitializeTokenizerFromFile(Path);
			TestTrue(FString::Printf(TEXT("8(e) BOM file must load. error='%s'"), *Wrapper->GetLastError()), bOk);
			TestTrue(TEXT("BOM file: IsInitialized() must be true"), Wrapper->IsInitialized());
			CheckString(TEXT("BOM file: GetLastError() after success"), FString(), Wrapper->GetLastError());
			CheckIds(TEXT("BOM file tokenizer is B"), {1, 2}, Wrapper->Encode(TEXT("hello world")));
		});
	});

	Describe("ErrorReset", [this]()
	{
		It("SuccessfulEncodeClearsPreviousError", [this]()
		{
			if (!InitB(TEXT("ErrReset"))) { return; }
			AddExpectedError(TEXT("Decode"), EAutomationExpectedErrorFlags::Contains, 0);
			Wrapper->Decode({1, -5});
			TestFalse(TEXT("precondition: failing decode must set an error"), Wrapper->GetLastError().IsEmpty());
			CheckIds(TEXT("encode hello"), {1}, Wrapper->Encode(TEXT("hello")));
			CheckString(TEXT("GetLastError after successful Encode"), FString(), Wrapper->GetLastError());
		});

		It("SuccessfulInitClearsPreviousError", [this]()
		{
			AddExpectedError(TEXT("InitializeTokenizerFromJson"), EAutomationExpectedErrorFlags::Contains, 0);
			Wrapper->InitializeTokenizerFromJson(TEXT("{not json"));
			TestFalse(TEXT("precondition: bad JSON must set an error"), Wrapper->GetLastError().IsEmpty());
			if (!InitB(TEXT("InitClears"))) { return; }
			CheckString(TEXT("GetLastError after successful init"), FString(), Wrapper->GetLastError());
		});

		It("SuccessfulDecodeClearsPreviousError", [this]()
		{
			if (!InitB(TEXT("DecodeClears"))) { return; }
			AddExpectedError(TEXT("Decode"), EAutomationExpectedErrorFlags::Contains, 0);
			Wrapper->Decode({-1});
			TestFalse(TEXT("precondition: failing decode must set an error"), Wrapper->GetLastError().IsEmpty());
			CheckString(TEXT("decode hello"), TEXT("hello"), Wrapper->Decode({1}));
			CheckString(TEXT("GetLastError after successful Decode"), FString(), Wrapper->GetLastError());
		});
	});

	Describe("Lifetime", [this]()
	{
		It("InitializedObjectCollectedWithoutCrash", [this]()
		{
			UTokenizerWrapper* Obj = NewObject<UTokenizerWrapper>(GetTransientPackage());
			if (!TestNotNull(TEXT("object created"), Obj)) { return; }
			TestTrue(TEXT("init B"), Obj->InitializeTokenizerFromJson(BJson()));
			Obj->MarkAsGarbage();
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
			TestTrue(TEXT("reached end after GC of an initialized wrapper without crashing"), true);
		});

		It("NeverInitializedObjectCollectedWithoutCrash", [this]()
		{
			UTokenizerWrapper* Obj = NewObject<UTokenizerWrapper>(GetTransientPackage());
			if (!TestNotNull(TEXT("object created"), Obj)) { return; }
			Obj->MarkAsGarbage();
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
			TestTrue(TEXT("reached end after GC of an uninitialized wrapper without crashing"), true);
		});

		It("ReInitializedObjectCollectedWithoutCrash", [this]()
		{
			// A double free of the replaced handle would surface at BeginDestroy or at re-init.
			UTokenizerWrapper* Obj = NewObject<UTokenizerWrapper>(GetTransientPackage());
			if (!TestNotNull(TEXT("object created"), Obj)) { return; }
			TestTrue(TEXT("init B"), Obj->InitializeTokenizerFromJson(BJson()));
			TestTrue(TEXT("init A"), Obj->InitializeTokenizerFromFile(TEXT("tokenizer.json")));
			Obj->MarkAsGarbage();
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
			TestTrue(TEXT("reached end after GC of a re-initialized wrapper without crashing"), true);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
