// WP0.1 smoke tests: encode through the real tokenizers_c lib must match Python `tokenizers` ids.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "TokenizerWrapper.h"

#if WITH_DEV_AUTOMATION_TESTS

BEGIN_DEFINE_SPEC(FTokenizersLibSmokeSpec, "Tokenizers.Lib",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
	TStrongObjectPtr<UTokenizerWrapper> Wrapper;

	static FString IdsToString(const TArray<int32>& Ids)
	{
		FString Out = TEXT("[");
		for (int32 i = 0; i < Ids.Num(); ++i)
		{
			Out += FString::Printf(TEXT("%s%d"), i ? TEXT(", ") : TEXT(""), Ids[i]);
		}
		return Out + TEXT("]");
	}

	// Compares ids and reports case name, expected and actual in the failure message.
	void CheckIds(const TCHAR* CaseName, const TArray<int32>& Expected, const TArray<int32>& Actual)
	{
		TestTrue(
			FString::Printf(TEXT("Case '%s': ids differ. expected=%s actual=%s"),
				CaseName, *IdsToString(Expected), *IdsToString(Actual)),
			Expected == Actual);
	}
END_DEFINE_SPEC(FTokenizersLibSmokeSpec)

void FTokenizersLibSmokeSpec::Define()
{
	Describe("Encode", [this]()
	{
		BeforeEach([this]()
		{
			Wrapper.Reset(NewObject<UTokenizerWrapper>());
		});

		AfterEach([this]()
		{
			Wrapper.Reset();
		});

		It("InitializesFromBundledFile", [this]()
		{
			TestNotNull(TEXT("wrapper created"), Wrapper.Get());
			TestTrue(TEXT("InitializeTokenizerFromFile(tokenizer.json) should return true"),
				Wrapper->InitializeTokenizerFromFile(TEXT("tokenizer.json")));
		});

		It("HelloWorld", [this]()
		{
			if (!TestTrue(TEXT("init"), Wrapper->InitializeTokenizerFromFile(TEXT("tokenizer.json")))) { return; }
			CheckIds(TEXT("HelloWorld"), {12092, 1533}, Wrapper->Encode(TEXT("Hello world")));
		});

		It("QuickBrownFox", [this]()
		{
			if (!TestTrue(TEXT("init"), Wrapper->InitializeTokenizerFromFile(TEXT("tokenizer.json")))) { return; }
			CheckIds(TEXT("QuickBrownFox"), {510, 3158, 8516, 30013, 15},
				Wrapper->Encode(TEXT("The quick brown fox.")));
		});

		It("NonAsciiAndEmoji", [this]()
		{
			if (!TestTrue(TEXT("init"), Wrapper->InitializeTokenizerFromFile(TEXT("tokenizer.json")))) { return; }
			// Escapes keep the source encoding-independent. Strings are split so hex escapes never swallow
			// following characters. U+00E9, U+00F6, U+6771 U+4EAC, U+1F642 (surrogate pair in UTF-16).
			const FString Text(UTF8_TO_TCHAR(
				"h" "\xC3\xA9" "llo w" "\xC3\xB6" "rld " "\xE6\x9D\xB1" "\xE4\xBA\xAC" " " "\xF0\x9F\x99\x82"));
			CheckIds(TEXT("NonAsciiAndEmoji"),
				{73, 860, 48620, 259, 6592, 392, 209, 12676, 111, 5494, 107, 42908},
				Wrapper->Encode(Text));
		});

		It("EmptyStringGivesEmptyArray", [this]()
		{
			if (!TestTrue(TEXT("init"), Wrapper->InitializeTokenizerFromFile(TEXT("tokenizer.json")))) { return; }
			CheckIds(TEXT("EmptyString"), {}, Wrapper->Encode(FString()));
			CheckIds(TEXT("EmptyLiteral"), {}, Wrapper->Encode(TEXT("")));
		});

		It("UninitializedWrapperGivesEmptyArray", [this]()
		{
			// A fresh wrapper with a null handle must not dereference it.
			CheckIds(TEXT("Uninitialized"), {}, Wrapper->Encode(TEXT("Hello")));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
