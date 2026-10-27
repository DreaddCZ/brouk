/* C interface of src/adblock (Rust, wrapping Brave's adblock-rust; specs/007).
 * Use it only through src/core/adblock_engine.h, which owns every pointer
 * returned here. See src/adblock/src/lib.rs for each function's contract. */
#ifndef BROUK_ADBLOCK_H_
#define BROUK_ADBLOCK_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BroukAdblockEngine BroukAdblockEngine;

typedef struct BroukAdblockList {
  const uint8_t* text;
  size_t len;
  uint8_t format; /* 0: ABP/uBO syntax, 1: hosts or plain domains */
  uint8_t trusted;
} BroukAdblockList;

typedef struct BroukAdblockMatch {
  uint8_t block;
  uint8_t important;
  uint8_t has_filter;
  uint8_t has_exception;
  int32_t filter_list;
  int32_t filter_line;
  int32_t exception_list;
  int32_t exception_line;
  char* filter_text;
  char* exception_text;
  char* redirect;
  char* rewritten_url;
} BroukAdblockMatch;

BroukAdblockEngine* brouk_adblock_engine_build(const BroukAdblockList* lists,
                                               size_t count);
BroukAdblockEngine* brouk_adblock_engine_deserialize(const uint8_t* data,
                                                     size_t len);
uint8_t* brouk_adblock_engine_serialize(const BroukAdblockEngine* engine,
                                        size_t* len);
void brouk_adblock_engine_free(BroukAdblockEngine* engine);
uint8_t brouk_adblock_use_resources(BroukAdblockEngine* engine,
                                    const uint8_t* json,
                                    size_t len);
void brouk_adblock_check(const BroukAdblockEngine* engine,
                         const char* url,
                         const char* source_url,
                         const char* request_type,
                         BroukAdblockMatch* out);
void brouk_adblock_match_clear(BroukAdblockMatch* match);
char* brouk_adblock_url_cosmetic_resources(const BroukAdblockEngine* engine,
                                           const char* url);
char* brouk_adblock_hidden_class_id_selectors(const BroukAdblockEngine* engine,
                                              const uint8_t* json,
                                              size_t len);
uint8_t brouk_adblock_check_filter(const char* line, char** error);
char* brouk_registrable_domain(const char* host);
uint8_t brouk_regex_is_match(const char* pattern, const char* text);
void brouk_adblock_free_string(char* text);
void brouk_adblock_free_bytes(uint8_t* data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* BROUK_ADBLOCK_H_ */
