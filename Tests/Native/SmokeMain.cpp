// Copyright NextGen-GameDev. Licensed under the Apache License 2.0.
//
// Loads the smoke-test module (linked against it, like the editor loads the Tokenizers module)
// and runs it. Usage: TokenizersLibSmoke <path to tokenizer.json>

#include <cstdio>

extern "C" int TokenizersLibSmoke(const char* TokenizerJsonPath);

int main(int Argc, char** Argv)
{
	if (Argc != 2)
	{
		std::fprintf(stderr, "usage: %s <tokenizer.json>\n", Argv[0]);
		return 2;
	}
	return TokenizersLibSmoke(Argv[1]);
}
