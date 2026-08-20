/*
 * test_validate.c — docs/IMPLEMENTATION.md Stage 2.
 *
 * ARKlight-py's own `tests/test_validate.py` fixture lives in a
 * separate repository (ARKlight-py) and isn't checked into
 * C_ARKlight, so this is a hand-written equivalent scoped to what
 * core/validate.c actually checks in this port: required-text
 * presence, Heading's level range, and recursive schema membership —
 * see core/validate.c for why the text-only-children rule and
 * out-of-range schema membership have no reachable rejection case
 * through the public constructors. Both passing and rejection cases
 * are covered, per the stage's own note that "a validator that only
 * tests the happy path isn't tested."
 */

#include "carklight.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A full, well-formed tree (one of every Stage 0 constructor) must
 * pass, and err_out must come back untouched (NULL). */
static void test_valid_tree_passes(void) {
    ArkNode* heading = ark_heading(1, "Hello, carklight");
    ArkNode* text = ark_text("Stage 2 is validate.");
    ArkNode* button = ark_button("Click me", "toggle");

    ArkNode* inner_children[] = {text, button};
    ArkNode* container = ark_container_arr(inner_children, 2);

    ArkNode* page_children[] = {heading, container};
    ArkNode* page = ark_page(page_children, 2, "Stage 2 smoke test");

    char* err = (char*)0x1; /* poison, to confirm it gets reset to NULL */
    int rc = ark_validate(page, &err);

    assert(rc == 0);
    assert(err == NULL);

    ark_free_node(page);
}

/* Heading/Text/Button all require non-empty text; NULL text (no
 * allocation ever happened) must be rejected with a message naming
 * the component. */
static void test_rejects_null_text(void) {
    ArkNode* heading = ark_heading(2, NULL);

    char* err = NULL;
    int rc = ark_validate(heading, &err);

    assert(rc != 0);
    assert(err != NULL);
    assert(strstr(err, "Heading") != NULL);

    free(err);
    ark_free_node(heading);
}

/* Empty string ("") is a distinct case from NULL — dup_or_null copies
 * it rather than turning it into NULL — and must be rejected the same
 * way. */
static void test_rejects_empty_text(void) {
    ArkNode* text = ark_text("");

    char* err = NULL;
    int rc = ark_validate(text, &err);

    assert(rc != 0);
    assert(err != NULL);
    assert(strstr(err, "Text") != NULL);

    free(err);
    ark_free_node(text);
}

/* Heading level is documented in carklight.h as "not validated at
 * [Stage 0]" — Stage 2's job. Both below (0) and above (7) the valid
 * 1-6 range must be rejected. */
static void test_rejects_out_of_range_heading_level(void) {
    ArkNode* too_low = ark_heading(0, "valid text");
    ArkNode* too_high = ark_heading(7, "valid text");

    char* err_low = NULL;
    char* err_high = NULL;
    int rc_low = ark_validate(too_low, &err_low);
    int rc_high = ark_validate(too_high, &err_high);

    assert(rc_low != 0);
    assert(err_low != NULL);
    assert(rc_high != 0);
    assert(err_high != NULL);

    free(err_low);
    free(err_high);
    ark_free_node(too_low);
    ark_free_node(too_high);
}

/* The boundary values 1 and 6 are valid, not off-by-one rejected. */
static void test_accepts_boundary_heading_levels(void) {
    ArkNode* lo = ark_heading(1, "valid text");
    ArkNode* hi = ark_heading(6, "valid text");

    assert(ark_validate(lo, NULL) == 0);
    assert(ark_validate(hi, NULL) == 0);

    ark_free_node(lo);
    ark_free_node(hi);
}

/* A single invalid node buried inside an otherwise-valid tree must
 * still fail validation for the whole tree (depth-first recursion). */
static void test_rejects_invalid_node_nested_deep(void) {
    ArkNode* bad_button = ark_button(NULL, "toggle"); /* NULL text: invalid */
    ArkNode* inner_children[] = {bad_button};
    ArkNode* container = ark_container_arr(inner_children, 1);
    ArkNode* page_children[] = {container};
    ArkNode* page = ark_page(page_children, 1, "wraps a bad button");

    char* err = NULL;
    int rc = ark_validate(page, &err);

    assert(rc != 0);
    assert(err != NULL);
    assert(strstr(err, "Button") != NULL);

    free(err);
    ark_free_node(page);
}

/* Page and Container have no required scalar props of their own — an
 * empty container and a page with no title/children must both pass. */
static void test_page_and_container_have_no_required_props(void) {
    ArkNode* container = ark_container_arr(NULL, 0);
    ArkNode* page = ark_page(NULL, 0, NULL);

    assert(ark_validate(container, NULL) == 0);
    assert(ark_validate(page, NULL) == 0);

    ark_free_node(container);
    ark_free_node(page);
}

/* err_out is documented as optional — passing NULL must not crash,
 * whether the tree is valid or not. */
static void test_null_err_out_is_safe(void) {
    ArkNode* good = ark_text("fine");
    ArkNode* bad = ark_text("");

    assert(ark_validate(good, NULL) == 0);
    assert(ark_validate(bad, NULL) != 0);

    ark_free_node(good);
    ark_free_node(bad);
}

/* NULL node is documented as valid (vacuously) — same convention as
 * ark_free_node/ark_normalize treating NULL as a no-op. */
static void test_null_node_is_valid(void) {
    char* err = NULL;
    assert(ark_validate(NULL, &err) == 0);
    assert(err == NULL);
}

int main(void) {
    test_valid_tree_passes();
    test_rejects_null_text();
    test_rejects_empty_text();
    test_rejects_out_of_range_heading_level();
    test_accepts_boundary_heading_levels();
    test_rejects_invalid_node_nested_deep();
    test_page_and_container_have_no_required_props();
    test_null_err_out_is_safe();
    test_null_node_is_valid();

    printf("stage2: all validate cases passed\n");
    return 0;
}
