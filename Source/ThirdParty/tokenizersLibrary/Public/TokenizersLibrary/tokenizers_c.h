/*!
 *  Copyright (c) 2023 by Contributors
 * \file tokenizers_c.h
 * \brief C binding to tokenizers rust library
 *
 * Since v0.1.3 no entry point panics or aborts on bad input: functions return a status code
 * (or NULL for constructors) and the reason is available via tokenizers_get_last_error.
 *
 * Strings are UTF-8 with explicit lengths and are never null-terminated. (NULL, 0) is the
 * empty string everywhere; NULL with len > 0 is TOKENIZERS_ERR_NULL_ARG.
 *
 * Memory ownership:
 *  - TokenizerEncodeResult.token_ids is allocated by the library and must be freed with
 *    tokenizers_free_encode_results.
 *  - Strings returned by tokenizers_get_decode_str / tokenizers_id_to_token are owned by the
 *    handle and valid until the next call on that handle (or tokenizers_free).
 *  - The string returned by tokenizers_get_last_error is owned by the library (thread-local).
 *  - A handle must be freed with tokenizers_free.
 * A handle is not thread-safe; callers serialize calls per handle.
 */
#ifndef TOKENIZERS_C_H_
#define TOKENIZERS_C_H_

// The C API
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/* Status codes (v0.1.3+) */
#define TOKENIZERS_OK               0
#define TOKENIZERS_ERR_NULL_ARG    (-1) /* null handle, null out-pointer, or null data with len > 0 */
#define TOKENIZERS_ERR_INVALID_UTF8 (-2) /* text input not valid UTF-8 */
#define TOKENIZERS_ERR_TOKENIZER   (-3) /* the tokenizers crate returned Err (bad JSON, encode/decode error) */
#define TOKENIZERS_ERR_PANIC       (-4) /* a Rust panic was caught at the boundary */

typedef void* TokenizerHandle;

typedef struct {
    int* token_ids;
    size_t len;
} TokenizerEncodeResult;

/* NULL on any failure (invalid JSON, bad vocab/merges, panic); reason via tokenizers_get_last_error. */
TokenizerHandle tokenizers_new_from_str(const char* json, size_t len);

/* NULL on any failure (invalid JSON, bad vocab/merges, panic); reason via tokenizers_get_last_error. */
TokenizerHandle byte_level_bpe_tokenizers_new_from_str(const char* vocab, size_t vocab_len,
                                                       const char* merges, size_t merges_len,
                                                       const char* added_tokens,
                                                       size_t added_tokens_len);

/* On failure *result = {NULL, 0}. An empty encoding is {NULL, 0} with TOKENIZERS_OK. */
int tokenizers_encode(TokenizerHandle handle, const char* data, size_t len, int add_special_token,
                      TokenizerEncodeResult* result);

/* A NULL handle is TOKENIZERS_ERR_NULL_ARG, even when num_seqs == 0. With a valid handle,
   num_seqs == 0 is OK and writes nothing (data, len and results may be NULL).
   An empty encoding is {NULL, 0}.
   On failure every results[i] = {NULL, 0}; nothing is left allocated. */
int tokenizers_encode_batch(TokenizerHandle handle, const char** data, size_t* len, size_t num_seqs,
                            int add_special_token, TokenizerEncodeResult* results);

/* NULL results, or entries with token_ids == NULL, are skipped. After freeing an entry it
   sets token_ids = NULL, len = 0 (so a double free is harmless). */
void tokenizers_free_encode_results(TokenizerEncodeResult* results, size_t num_seqs);

/* On failure the handle's decode string is emptied. */
int tokenizers_decode(TokenizerHandle handle, const uint32_t* data, size_t len,
                      int skip_special_token);

/* *data points into the handle; valid until the next call on the handle. */
int tokenizers_get_decode_str(TokenizerHandle handle, const char** data, size_t* len);

int tokenizers_get_vocab_size(TokenizerHandle handle, size_t* size);

/* *data points into the handle; valid until the next call on the handle. len == 0 if the id
   is not in the vocab (still TOKENIZERS_OK). */
int tokenizers_id_to_token(TokenizerHandle handle, uint32_t id, const char** data, size_t* len);

/* stores -1 to *id if the token is not in the vocab (still TOKENIZERS_OK). An id that does
   not fit int32_t (>= 2^31) is TOKENIZERS_ERR_TOKENIZER; *id is left unchanged. */
int tokenizers_token_to_id(TokenizerHandle handle, const char* token, size_t len, int32_t* id);

/* NULL is a no-op. */
void tokenizers_free(TokenizerHandle handle);

/* Message of the most recent failure on the calling thread (thread-local). UTF-8, not
   null-terminated, valid until the next failing call on the same thread. len == 0 if none.
   Never fails; null out-pointers are ignored. */
void tokenizers_get_last_error(const char** data, size_t* len);

#ifdef __cplusplus
}
#endif
#endif  // TOKENIZERS_C_H_
