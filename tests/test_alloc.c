/*
 * test_alloc.c — Stage 5e (undocumented): exercises core/alloc.c's
 * public surface (ark_alloc/ark_calloc/ark_realloc/ark_dealloc and
 * ArkBuf) directly, on top of the existing per-stage tests that now
 * exercise it indirectly through every core/backend call site it
 * replaced. Run with -DCARKLIGHT_ENABLE_ASAN=ON for the same
 * leak/double-free bar test_stage0_alloc.c documents.
 */

#include "carklight.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_alloc_dealloc_roundtrip(void) {
    char* p = ark_alloc(16);
    assert(p != NULL);
    memcpy(p, "hello", 6);
    assert(strcmp(p, "hello") == 0);
    ark_dealloc(p);
}

static void test_calloc_zeroes(void) {
    int* p = ark_calloc(4, sizeof(int));
    assert(p != NULL);
    for (int i = 0; i < 4; i++) {
        assert(p[i] == 0);
    }
    ark_dealloc(p);
}

static void test_realloc_grows_and_preserves(void) {
    char* p = ark_alloc(4);
    assert(p != NULL);
    memcpy(p, "abc", 4);
    char* grown = ark_realloc(p, 64);
    assert(grown != NULL);
    assert(strcmp(grown, "abc") == 0);
    ark_dealloc(grown);
}

static void test_dealloc_null_is_noop(void) {
    ark_dealloc(NULL);
}

static void test_buf_basic_append(void) {
    ArkBuf buf;
    assert(ark_buf_init(&buf) == 0);
    assert(ark_buf_append(&buf, "hello, ") == 0);
    assert(ark_buf_append(&buf, "world") == 0);
    assert(ark_buf_append_char(&buf, '!') == 0);
    assert(buf.len == strlen("hello, world!"));
    assert(strcmp(buf.data, "hello, world!") == 0);
    ark_buf_free(&buf);
}

/* Forces several geometric growth steps past the 256-byte starting
 * capacity, confirming content survives each realloc. */
static void test_buf_grows_past_initial_capacity(void) {
    ArkBuf buf;
    assert(ark_buf_init(&buf) == 0);
    for (int i = 0; i < 100; i++) {
        assert(ark_buf_append(&buf, "0123456789") == 0);
    }
    assert(buf.len == 1000);
    assert(buf.cap > 256);
    for (int i = 0; i < 100; i++) {
        assert(memcmp(buf.data + (i * 10), "0123456789", 10) == 0);
    }
    ark_buf_free(&buf);
}

static void test_buf_append_n_partial(void) {
    ArkBuf buf;
    assert(ark_buf_init(&buf) == 0);
    assert(ark_buf_append_n(&buf, "abcdef", 3) == 0);
    assert(buf.len == 3);
    assert(strcmp(buf.data, "abc") == 0);
    ark_buf_free(&buf);
}

/* ark_buf_free must be safe to call on a zero-initialized ArkBuf
 * (never ark_buf_init'd) and safe to call twice, matching ark_free_*
 * NULL-is-noop conventions elsewhere in this codebase. */
static void test_buf_free_is_idempotent_and_safe_on_zeroed(void) {
    ArkBuf buf = {0};
    ark_buf_free(&buf); /* no-op: data is already NULL */

    assert(ark_buf_init(&buf) == 0);
    ark_buf_free(&buf);
    ark_buf_free(&buf); /* second free: must not double-free */
}

int main(void) {
    test_alloc_dealloc_roundtrip();
    test_calloc_zeroes();
    test_realloc_grows_and_preserves();
    test_dealloc_null_is_noop();
    test_buf_basic_append();
    test_buf_grows_past_initial_capacity();
    test_buf_append_n_partial();
    test_buf_free_is_idempotent_and_safe_on_zeroed();

    printf("stage5e: all core_handler alloc/ArkBuf cases passed\n");
    return 0;
}
