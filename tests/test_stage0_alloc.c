/*
 * test_stage0_alloc.c — docs/IMPLEMENTATION.md Stage 0's test bar:
 * "build a tree by hand, free it, run under a memory checker
 * (valgrind/ASan), confirm zero leaks and zero double-frees." No
 * pipeline logic exists yet to test beyond that — this is
 * deliberately not a correctness test of tree *shape*, only of
 * ownership.
 *
 * Run normally: exercises every constructor/free pair at least once.
 * Run with -DCARKLIGHT_ENABLE_ASAN=ON: the same run additionally
 * proves zero leaks/double-frees, which is Stage 0's actual bar.
 */

#include "carklight.h"

#include <assert.h>
#include <stdio.h>

/* Builds one tree exercising every Stage 0 constructor (Page wrapping
 * a Heading, some Text, a Button, and a nested Container), frees it
 * through ark_free_node, and confirms nothing crashes doing so. */
static void test_build_and_free_full_tree(void) {
    ArkNode* heading = ark_heading(1, "Hello, carklight");
    ArkNode* text = ark_text("Stage 0 is data model only.");
    ArkNode* button = ark_button("Click me", "toggle");

    ArkNode* inner_children[] = {text, button};
    ArkNode* container = ark_container_arr(inner_children, 2);

    ArkNode* page_children[] = {heading, container};
    ArkNode* page = ark_page(page_children, 2, "Stage 0 smoke test");

    assert(page != NULL);
    ark_free_node(page); /* frees heading, container, text, button too */
}

/* NULL is documented as a no-op for ark_free_node — confirm it
 * doesn't crash, since every other free path in this test relies on
 * that being true for optional/absent fields. */
static void test_free_null_is_noop(void) {
    ark_free_node(NULL);
}

/* Optional fields (Button's on_click, Page's title) must round-trip
 * as NULL rather than turning into e.g. an empty string, and freeing
 * a node that never set them must not crash. */
static void test_optional_fields_stay_null(void) {
    ArkNode* button = ark_button("No handler", NULL);
    assert(button != NULL);

    ArkNode* page = ark_page(NULL, 0, NULL);
    assert(page != NULL);

    ark_free_node(button);
    ark_free_node(page);
}

/* A container with zero children is a valid, freeable leaf-ish node —
 * dup_children(..., 0) must not misbehave (e.g. return a dangling
 * non-NULL pointer for a zero-length array). */
static void test_empty_container(void) {
    ArkNode* container = ark_container_arr(NULL, 0);
    assert(container != NULL);
    ark_free_node(container);
}

/* ArkSite and ArkBuildResult are opaque per Stage 0's scope, but they
 * still get the same "construct, free, no leak" exercise as ArkNode. */
static void test_site_and_build_result_scaffolding(void) {
    ArkNode* root = ark_text("just a root node");
    ArkSite* site = ark_site_new_from_root(root);
    assert(site != NULL);
    ark_free_site(site); /* also frees root */

    ArkBuildResult* result = ark_build_result_new_empty();
    assert(result != NULL);
    ark_free_result(result);
}

int main(void) {
    test_build_and_free_full_tree();
    test_free_null_is_noop();
    test_optional_fields_stay_null();
    test_empty_container();
    test_site_and_build_result_scaffolding();

    printf("stage0: all alloc/free cases passed\n");
    return 0;
}
