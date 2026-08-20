/*
 * test_ir_build.c — docs/IMPLEMENTATION.md Stage 3.
 *
 * Hand-ported from ARKlight-py's `tests/test_ir_build.py`
 * (Rae-ARK/ARKlight), cloned separately as reference. Each test below
 * names the Python case it mirrors; `test_build_website_ir_
 * multiple_routes` has no port here — see carklight.h's Stage 3 doc
 * comment for why (no `site_name`/route-wrapping layer exists yet in
 * this port's ArkSite).
 */

#include "carklight.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Mirrors test_build_website_ir_basic_shape:
 *   pages = {"/": Page(Heading("Hi"), Text("body"))}
 *   ir.pages[0].root.type == "Page"
 *   [c.type for c in page.root.children] == ["Heading", "Text"] */
static void test_basic_shape(void) {
    ArkNode* heading = ark_heading(1, "Hi");
    ArkNode* text = ark_text("body");
    ArkNode* children[] = {heading, text};
    ArkNode* page = ark_page(children, 2, NULL);

    ArkIRNode* ir = ark_ir_build(page);

    assert(ir != NULL);
    assert(strcmp(ark_ir_type(ir), "Page") == 0);
    assert(ark_ir_child_count(ir) == 2);
    assert(strcmp(ark_ir_type(ark_ir_child_at(ir, 0)), "Heading") == 0);
    assert(strcmp(ark_ir_type(ark_ir_child_at(ir, 1)), "Text") == 0);

    ark_ir_free(ir);
    ark_free_node(page);
}

/* Mirrors test_build_website_ir_preserves_text_children_as_strings:
 *   pages = {"/": Page(Heading("Title"))}
 *   heading.children == ["Title"]
 * carklight has no bare-string child slot (see carklight.h's Stage 3
 * doc comment) — ark_ir_text() is this port's equivalent. */
static void test_preserves_text_as_scalar(void) {
    ArkNode* heading = ark_heading(1, "Title");
    ArkNode* children[] = {heading};
    ArkNode* page = ark_page(children, 1, NULL);

    ArkIRNode* ir = ark_ir_build(page);
    const ArkIRNode* ir_heading = ark_ir_child_at(ir, 0);

    assert(strcmp(ark_ir_text(ir_heading), "Title") == 0);
    assert(ark_ir_child_count(ir_heading) == 0); /* text lives in ark_ir_text, not children */

    ark_ir_free(ir);
    ark_free_node(page);
}

/* Mirrors test_build_website_ir_nested_containers:
 *   pages = {"/": Page(Container(Text("a"), Container(Text("b"))))}
 *   outer_container.children[1].type == "Container"
 *   inner_container.children[0].type == "Text" */
static void test_nested_containers(void) {
    ArkNode* text_a = ark_text("a");
    ArkNode* text_b = ark_text("b");
    ArkNode* inner_children[] = {text_b};
    ArkNode* inner_container = ark_container_arr(inner_children, 1);
    ArkNode* outer_children[] = {text_a, inner_container};
    ArkNode* outer_container = ark_container_arr(outer_children, 2);
    ArkNode* page_children[] = {outer_container};
    ArkNode* page = ark_page(page_children, 1, NULL);

    ArkIRNode* ir = ark_ir_build(page);
    const ArkIRNode* ir_outer = ark_ir_child_at(ir, 0);
    const ArkIRNode* ir_inner = ark_ir_child_at(ir_outer, 1);

    assert(strcmp(ark_ir_type(ir_inner), "Container") == 0);
    assert(strcmp(ark_ir_type(ark_ir_child_at(ir_inner, 0)), "Text") == 0);

    ark_ir_free(ir);
    ark_free_node(page);
}

/* Not in the Python fixture (there, `level` is just another entry in
 * `props`) — exercises this port's dedicated ark_ir_level accessor,
 * including that it's 0/unused for non-Heading nodes. */
static void test_heading_level_prop(void) {
    ArkNode* heading = ark_heading(3, "Section");
    ArkIRNode* ir = ark_ir_build(heading);
    assert(ark_ir_level(ir) == 3);

    ArkNode* text = ark_text("plain");
    ArkIRNode* text_ir = ark_ir_build(text);
    assert(ark_ir_level(text_ir) == 0);

    ark_ir_free(ir);
    ark_ir_free(text_ir);
    ark_free_node(heading);
    ark_free_node(text);
}

/* Page's title and Button's on_click props round-trip, including the
 * NULL/absent case for each. */
static void test_title_and_on_click_props(void) {
    ArkNode* page_with_title = ark_page(NULL, 0, "My Page");
    ArkNode* page_without_title = ark_page(NULL, 0, NULL);
    ArkNode* button_with_handler = ark_button("Go", "toggle");
    ArkNode* button_without_handler = ark_button("Go", NULL);

    ArkIRNode* ir_page_with_title = ark_ir_build(page_with_title);
    ArkIRNode* ir_page_without_title = ark_ir_build(page_without_title);
    ArkIRNode* ir_button_with_handler = ark_ir_build(button_with_handler);
    ArkIRNode* ir_button_without_handler = ark_ir_build(button_without_handler);

    assert(strcmp(ark_ir_prop_title(ir_page_with_title), "My Page") == 0);
    assert(ark_ir_prop_title(ir_page_without_title) == NULL);
    assert(strcmp(ark_ir_prop_on_click(ir_button_with_handler), "toggle") == 0);
    assert(ark_ir_prop_on_click(ir_button_without_handler) == NULL);

    ark_ir_free(ir_page_with_title);
    ark_ir_free(ir_page_without_title);
    ark_ir_free(ir_button_with_handler);
    ark_ir_free(ir_button_without_handler);
    ark_free_node(page_with_title);
    ark_free_node(page_without_title);
    ark_free_node(button_with_handler);
    ark_free_node(button_without_handler);
}

/* NULL is documented as a no-op/empty-result convention throughout
 * this stage, same as ark_normalize/ark_validate. */
static void test_null_handling(void) {
    assert(ark_ir_build(NULL) == NULL);
    ark_ir_free(NULL); /* must not crash */
    assert(ark_ir_type(NULL) == NULL);
    assert(ark_ir_text(NULL) == NULL);
    assert(ark_ir_prop_title(NULL) == NULL);
    assert(ark_ir_prop_on_click(NULL) == NULL);
    assert(ark_ir_level(NULL) == 0);
    assert(ark_ir_child_count(NULL) == 0);
    assert(ark_ir_child_at(NULL, 0) == NULL);
}

/* Out-of-range child index returns NULL rather than reading past the
 * array. */
static void test_child_at_out_of_range(void) {
    ArkNode* container = ark_container_arr(NULL, 0);
    ArkIRNode* ir = ark_ir_build(container);

    assert(ark_ir_child_at(ir, 0) == NULL);

    ark_ir_free(ir);
    ark_free_node(container);
}

int main(void) {
    test_basic_shape();
    test_preserves_text_as_scalar();
    test_nested_containers();
    test_heading_level_prop();
    test_title_and_on_click_props();
    test_null_handling();
    test_child_at_out_of_range();

    printf("stage3: all IR build cases passed\n");
    return 0;
}
