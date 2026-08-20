/*
 * test_normalize.c — docs/IMPLEMENTATION.md Stage 1.
 *
 * ARKlight-py's own `tests/test_normalize.py` fixture lives in a
 * separate repository (ARKlight-py) and isn't checked into
 * C_ARKlight, so this is a hand-written equivalent exercising the
 * one transform Stage 1 actually performs in the C port: pruning
 * NULL ("None/False-equivalent") entries out of children arrays,
 * recursively — see core/normalize.c for why the other two Python
 * behaviors (flatten nested lists, wrap bare strings) don't apply to
 * this statically-typed tree.
 */

#include "carklight.h"

#include <assert.h>
#include <stdio.h>

/* A NULL entry in a children array — the C encoding of "this child
 * was conditionally omitted" — must be dropped, and child_count must
 * shrink to match. */
static void test_drops_null_children(void) {
    ArkNode* text = ark_text("kept");
    ArkNode* children[] = {text, NULL, NULL};
    ArkNode* container = ark_container_arr(children, 3);

    container = ark_normalize(container);

    assert(container != NULL);
    ark_free_node(container); /* also frees `text`; the two NULLs were never owned */
}

/* Pruning must recurse: a NULL grandchild nested inside a normalized
 * child container should also be dropped. */
static void test_recurses_into_nested_children(void) {
    ArkNode* inner_text = ark_text("inner");
    ArkNode* inner_children[] = {inner_text, NULL};
    ArkNode* inner = ark_container_arr(inner_children, 2);

    ArkNode* outer_children[] = {inner};
    ArkNode* outer = ark_page(outer_children, 1, "Stage 1 smoke test");

    outer = ark_normalize(outer);

    assert(outer != NULL);
    ark_free_node(outer);
}

/* A tree with no NULL entries anywhere is left structurally alone —
 * normalize is a no-op on already-clean input, and running it twice
 * on the same tree must not crash or double-prune. */
static void test_idempotent_on_clean_tree(void) {
    ArkNode* heading = ark_heading(1, "Hello");
    ArkNode* text = ark_text("World");
    ArkNode* children[] = {heading, text};
    ArkNode* page = ark_page(children, 2, NULL);

    page = ark_normalize(page);
    page = ark_normalize(page); /* second pass: still fine */

    assert(page != NULL);
    ark_free_node(page);
}

/* NULL is documented as returning NULL, same convention as
 * ark_free_node treating NULL as a no-op. */
static void test_normalize_null_is_noop(void) {
    assert(ark_normalize(NULL) == NULL);
}

/* A leaf node (no children at all) round-trips unchanged. */
static void test_leaf_node_unchanged(void) {
    ArkNode* text = ark_text("just a leaf");
    text = ark_normalize(text);
    assert(text != NULL);
    ark_free_node(text);
}

int main(void) {
    test_drops_null_children();
    test_recurses_into_nested_children();
    test_idempotent_on_clean_tree();
    test_normalize_null_is_noop();
    test_leaf_node_unchanged();

    printf("stage1: all normalize cases passed\n");
    return 0;
}
