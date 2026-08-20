/*
 * core/alloc.c — Stage 5e (undocumented): core_handler, the
 * centralized allocation layer proposed in
 * `docs/Think different, life easy/CORE_HANDLER.md`. See that doc,
 * and the "Stage 5e" block comment in include/carklight.h, for the
 * rationale — this file is the actual implementation of the public
 * ark_alloc/ark_calloc/ark_realloc/ark_dealloc/ark_buf_* surface
 * declared there.
 *
 * Deliberately still just malloc/realloc/calloc/free underneath —
 * this is job #1 only (a memory-handling utility layer), not a new
 * allocation *strategy*. A future arena/pool allocator, or
 * allocation-failure injection for tests, changes only this file.
 *
 * Lives in core/ (not a new top-level directory), per the proposal
 * doc's §2 reasoning: this is exactly the kind of foundational,
 * "every later stage inherits this" decision docs/IMPLEMENTATION.md
 * says belongs as early as possible, even though it lands after
 * Stage 5b chronologically.
 */

#include "carklight.h"

#include <stdlib.h>
#include <string.h>

void* ark_alloc(size_t size) {
    return malloc(size);
}

void* ark_calloc(size_t nmemb, size_t size) {
    return calloc(nmemb, size);
}

void* ark_realloc(void* ptr, size_t size) {
    return realloc(ptr, size);
}

void ark_dealloc(void* ptr) {
    free(ptr);
}

/* --- ArkBuf: shared growable byte buffer --------------------------------
 * Same reserve/grow/append shape both backends/html/render.c and
 * backends/js/render.c independently reimplemented as a private
 * strbuf_t before this stage — see CORE_HANDLER.md §1. Now defined
 * once, here, and used by both (and by anything else in core/ that
 * wants a growable buffer).
 */

int ark_buf_init(ArkBuf* buf) {
    if (buf == NULL) {
        return 1;
    }
    buf->cap = 256;
    buf->len = 0;
    buf->data = ark_alloc(buf->cap);
    if (buf->data == NULL) {
        buf->cap = 0;
        return 1;
    }
    buf->data[0] = '\0';
    return 0;
}

void ark_buf_free(ArkBuf* buf) {
    if (buf == NULL) {
        return;
    }
    ark_dealloc(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

int ark_buf_reserve(ArkBuf* buf, size_t extra) {
    if (buf == NULL) {
        return 1;
    }
    size_t needed = buf->len + extra + 1;
    if (needed <= buf->cap) {
        return 0;
    }
    size_t new_cap = buf->cap == 0 ? 256 : buf->cap;
    while (new_cap < needed) {
        new_cap *= 2;
    }
    char* grown = ark_realloc(buf->data, new_cap);
    if (grown == NULL) {
        return 1; /* buf left unchanged, still valid/freeable */
    }
    buf->data = grown;
    buf->cap = new_cap;
    return 0;
}

int ark_buf_append_n(ArkBuf* buf, const char* s, size_t n) {
    if (buf == NULL) {
        return 1;
    }
    if (n == 0) {
        return 0;
    }
    if (ark_buf_reserve(buf, n) != 0) {
        return 1;
    }
    memcpy(buf->data + buf->len, s, n);
    buf->len += n;
    buf->data[buf->len] = '\0';
    return 0;
}

int ark_buf_append(ArkBuf* buf, const char* s) {
    return ark_buf_append_n(buf, s, strlen(s));
}

int ark_buf_append_char(ArkBuf* buf, char c) {
    return ark_buf_append_n(buf, &c, 1);
}
