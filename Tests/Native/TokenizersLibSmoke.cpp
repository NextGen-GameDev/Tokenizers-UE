// Copyright NextGen-GameDev. Licensed under the Apache License 2.0.
//
// Engine-free smoke test for the native tokenizers library on any platform (run by
// Scripts/TestTokenizersLib.sh). Built as a shared module (.so / .dylib, -fPIC, hidden
// visibility), as Unreal links the lib into the Tokenizers module, then loaded by a small
// executable. Uses the plugin's Content/tokenizer.json; the expected ids match the
// Tokenizers.* automation specs.

#include <TokenizersLibrary/tokenizers_c.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define SMOKE_EXPORT __declspec(dllexport)
#else
#define SMOKE_EXPORT __attribute__((visibility("default")))
#endif

namespace
{
	int GFailures = 0;
	int GChecks = 0;

	void Check(bool bOk, const std::string& What)
	{
		++GChecks;
		if (!bOk)
		{
			++GFailures;
			std::printf("  FAIL: %s\n", What.c_str());
		}
		else
		{
			std::printf("  ok:   %s\n", What.c_str());
		}
	}

	std::string LastError()
	{
		const char* Data = nullptr;
		size_t Len = 0;
		tokenizers_get_last_error(&Data, &Len);
		return Data != nullptr ? std::string(Data, Len) : std::string();
	}

	std::string IdsToString(const std::vector<int>& Ids)
	{
		std::string Out = "[";
		for (size_t I = 0; I < Ids.size(); ++I)
		{
			Out += (I ? ", " : "") + std::to_string(Ids[I]);
		}
		return Out + "]";
	}

	bool Encode(TokenizerHandle Handle, const std::string& Text, bool bSpecial, std::vector<int>& Out)
	{
		TokenizerEncodeResult Result{};
		const int Status = tokenizers_encode(Handle, Text.empty() ? nullptr : Text.data(), Text.size(), bSpecial ? 1 : 0, &Result);
		Out.assign(Result.token_ids, Result.token_ids + Result.len);
		tokenizers_free_encode_results(&Result, 1);
		return Status == TOKENIZERS_OK;
	}

	bool Decode(TokenizerHandle Handle, const std::vector<int>& Ids, bool bSkipSpecial, std::string& Out)
	{
		if (tokenizers_decode(Handle, reinterpret_cast<const uint32_t*>(Ids.data()), Ids.size(), bSkipSpecial ? 1 : 0) != TOKENIZERS_OK)
		{
			return false;
		}
		const char* Data = nullptr;
		size_t Len = 0;
		if (tokenizers_get_decode_str(Handle, &Data, &Len) != TOKENIZERS_OK)
		{
			return false;
		}
		Out.assign(Data != nullptr ? Data : "", Len);
		return true;
	}
}

extern "C" SMOKE_EXPORT int TokenizersLibSmoke(const char* TokenizerJsonPath)
{
	std::ifstream File(TokenizerJsonPath, std::ios::binary);
	std::stringstream Buffer;
	Buffer << File.rdbuf();
	const std::string Json = Buffer.str();
	Check(!Json.empty(), std::string("read ") + TokenizerJsonPath);

	TokenizerHandle Tok = tokenizers_new_from_str(Json.data(), Json.size());
	Check(Tok != nullptr, "tokenizers_new_from_str(tokenizer.json) error='" + LastError() + "'");
	if (Tok == nullptr)
	{
		return 1;
	}

	size_t VocabSize = 0;
	// Each result is computed before its message: argument evaluation order is unspecified.
	bool bOk = tokenizers_get_vocab_size(Tok, &VocabSize) == TOKENIZERS_OK && VocabSize > 0;
	Check(bOk, "tokenizers_get_vocab_size > 0 (got " + std::to_string(VocabSize) + ")");

	std::vector<int> Ids;
	bOk = Encode(Tok, "Hello", false, Ids) && Ids == std::vector<int>({ 12092 });
	Check(bOk, "Encode(\"Hello\") == [12092], got " + IdsToString(Ids));
	bOk = Encode(Tok, std::string("Hello\0", 6), false, Ids) && Ids == std::vector<int>({ 12092, 177 });
	Check(bOk, "Encode(\"Hello\\0\") == [12092, 177], got " + IdsToString(Ids));
	bOk = Encode(Tok, std::string(), false, Ids) && Ids.empty();
	Check(bOk, "Encode(\"\") is empty, got " + IdsToString(Ids));

	const std::string Text = "Hello world, h\xC3\xA9llo \xE4\xB8\x96\xE7\x95\x8C \xF0\x9F\x98\x80";
	std::string Decoded;
	bOk = Encode(Tok, Text, false, Ids) && !Ids.empty() && Decode(Tok, Ids, true, Decoded) && Decoded == Text;
	Check(bOk, "non-ASCII round trip (" + std::to_string(Ids.size()) + " ids), decoded='" + Decoded + "'");

	const char* BadUtf8 = "\xFF\xFE";
	TokenizerEncodeResult BadResult{};
	bOk = tokenizers_encode(Tok, BadUtf8, 2, 0, &BadResult) == TOKENIZERS_ERR_INVALID_UTF8 && BadResult.token_ids == nullptr;
	Check(bOk, "invalid UTF-8 -> TOKENIZERS_ERR_INVALID_UTF8 (no abort)");

	// Truncated batch: rows keep their own lengths, at most MaxLength, never padded.
	const char* Texts[] = { "Hello", "Hello world, this is a longer sentence." };
	size_t Lens[] = { std::strlen(Texts[0]), std::strlen(Texts[1]) };
	TokenizerEncodeResult Batch[2] = {};
	const int BatchStatus = tokenizers_encode_batch_truncated(Tok, Texts, Lens, 2, 0, 4, Batch);
	Check(BatchStatus == TOKENIZERS_OK && Batch[0].len == 1 && Batch[0].token_ids[0] == 12092 && Batch[1].len == 4,
		"encode_batch_truncated(max 4) row lengths 1 and 4, got " + std::to_string(Batch[0].len) + " and " + std::to_string(Batch[1].len));
	tokenizers_free_encode_results(Batch, 2);

	// Bad JSON is an error, never an abort.
	TokenizerHandle Bad = tokenizers_new_from_str("{", 1);
	const std::string BadError = LastError();
	Check(Bad == nullptr && !BadError.empty(), "bad JSON -> NULL handle, error='" + BadError + "'");

	// One handle per thread, in parallel (the thread-local error state and the allocator).
	std::vector<int> Expected;
	Encode(Tok, Text, true, Expected);
	std::vector<int> Mismatches(4, 0);
	std::vector<std::thread> Threads;
	for (int T = 0; T < 4; ++T)
	{
		Threads.emplace_back([&, T]()
		{
			TokenizerHandle Own = tokenizers_new_from_str(Json.data(), Json.size());
			for (int I = 0; I < 200; ++I)
			{
				std::vector<int> Got;
				if (Own == nullptr || !Encode(Own, Text, true, Got) || Got != Expected)
				{
					++Mismatches[T];
				}
			}
			tokenizers_free(Own);
		});
	}
	for (std::thread& Thread : Threads)
	{
		Thread.join();
	}
	int TotalMismatches = 0;
	for (int M : Mismatches)
	{
		TotalMismatches += M;
	}
	Check(TotalMismatches == 0, "4 threads x 200 encodes, mismatches " + std::to_string(TotalMismatches));

	tokenizers_free(Tok);
	tokenizers_free(nullptr);

	std::printf("%d/%d checks passed\n", GChecks - GFailures, GChecks);
	return GFailures == 0 ? 0 : 1;
}
