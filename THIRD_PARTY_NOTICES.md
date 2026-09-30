# Third-party notices

`tokenizers_c.lib` is built from the fork https://github.com/P1ayer-1/tokenizers-cpp (tag `v0.1.4`) and links the components below. The plugin also ships one tokenizer file.

The full text of the Apache License 2.0 is in `LICENSE-APACHE` in this folder. It applies to every component below that lists Apache-2.0.

## tokenizers-cpp

- Origin: mlc-ai/tokenizers-cpp, https://github.com/mlc-ai/tokenizers-cpp. Used through the fork by P1ayer-1, https://github.com/P1ayer-1/tokenizers-cpp.
- SPDX: Apache-2.0
- License text: `LICENSE-APACHE`.

## Hugging Face tokenizers (Rust crate 0.21.4)

- Source: https://github.com/huggingface/tokenizers
- SPDX: Apache-2.0
- License text: `LICENSE-APACHE`.

## Oniguruma (bundled C code in the `onig_sys` 69.9.3 crate)

- License: BSD-2-Clause
- Source: https://github.com/kkos/oniguruma (vendored by https://github.com/rust-onig/rust-onig)

Verbatim from `oniguruma/COPYING` in onig_sys 69.9.3:

```
Oniguruma LICENSE
-----------------

Copyright (c) 2002-2021  K.Kosako  <kkosako0@gmail.com>
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
SUCH DAMAGE.
```

## esaxx-rs 0.1.10 (bundles C++ esaxx)

- SPDX: Apache-2.0
- License text: `LICENSE-APACHE`.

## Other Rust crates

The other 72 Rust crates linked into `tokenizers_c.lib` are under permissive licenses. By count:

| License (SPDX expression as listed by the crate) | Crates |
|---|---|
| MIT OR Apache-2.0 | 49 |
| MIT | 10 |
| MIT/Apache-2.0 | 7 |
| Apache-2.0 | 3 |
| Unlicense OR MIT | 2 |
| Apache-2.0 OR MIT OR Zlib | 2 |
| Apache-2.0 OR BSL-1.0 | 1 |
| (MIT OR Apache-2.0) AND Unicode-3.0 | 1 |
| BSD-2-Clause OR Apache-2.0 OR MIT | 1 |

Regenerate the full list with `cargo metadata --locked` in the fork's `rust/` folder.

## Content/tokenizer.json

- The GPT-NeoX-20B tokenizer by EleutherAI.
- SPDX: Apache-2.0
- License text: `LICENSE-APACHE`.
