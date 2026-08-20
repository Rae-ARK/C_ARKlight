/*
 * test_html_backend.c — docs/IMPLEMENTATION.md Stage 4.
 *
 * Hand-ported from ARKlight-py's `tests/test_html_backend.py`
 * (Rae-ARK/ARKlight, cloned separately as reference), restricted to
 * the cases reachable through Stage 0's five component types
 * (Page/Heading/Text/Button/Container) and single-root ArkSite — see
 * carklight.h's Stage 4 block comment for the full deferral list
 * (Link/Image + route rewriting, stylesheet/script tags, generic
 * props, state/Bind/Action all have no Python-side equivalent here
 * yet). Each test below names the Python case it mirrors, or says why
 * it doesn't have one.
 */

#include "carklight.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Builds a single-page ArkSite, runs the HTML backend on it, and
 * returns the rendered "index.html" contents as a NUL-terminated
 * string (owned by `result`'s file entry — do not free it directly,
 * free `result`/`site` once done reading it). */
static const char* render_page(ArkNode* page, ArkSite** site_out, ArkBuildResult** result_out) {
    ArkSite* site = ark_site_new_from_root(page);
    ArkBuildResult* result = ark_build_result_new_empty();

    char* err = NULL;
    int rc = ark_html_render(NULL, site, result, &err);
    assert(rc == 0);
    assert(err == NULL);
    assert(ark_result_file_count(result) == 1);
    assert(strcmp(ark_result_file_path(result, 0), "index.html") == 0);

    *site_out = site;
    *result_out = result;
    return (const char*)ark_result_file_data(result, 0, NULL);
}

/* Mirrors test_renders_basic_page_to_html:
 *   output = render({"/": Page(Heading("Hi"), Text("body"), title="My Page")})
 *   "<!DOCTYPE html>" / "<title>My Page</title>" / "<h1>Hi</h1>" / "<p>body</p>" in html
 * (no stylesheet-link assertion ported — Stage 5, see carklight.h's
 * Stage 4 comment.) */
static void test_renders_basic_page_to_html(void) {
    ArkNode* heading = ark_heading(1, "Hi");
    ArkNode* text = ark_text("body");
    ArkNode* children[] = {heading, text};
    ArkNode* page = ark_page(children, 2, "My Page");

    ArkSite* site;
    ArkBuildResult* result;
    const char* html = render_page(page, &site, &result);

    assert(strstr(html, "<!DOCTYPE html>") != NULL);
    assert(strstr(html, "<title>My Page</title>") != NULL);
    assert(strstr(html, "<h1>Hi</h1>") != NULL);
    assert(strstr(html, "<p>body</p>") != NULL);

    ark_free_result(result);
    ark_free_site(site);
}

/* Mirrors test_heading_level_prop_controls_tag:
 *   output = render({"/": Page(Heading("Sub", level=3))})
 *   "<h3>Sub</h3>" in html */
static void test_heading_level_prop_controls_tag(void) {
    ArkNode* heading = ark_heading(3, "Sub");
    ArkNode* children[] = {heading};
    ArkNode* page = ark_page(children, 1, NULL);

    ArkSite* site;
    ArkBuildResult* result;
    const char* html = render_page(page, &site, &result);

    assert(strstr(html, "<h3>Sub</h3>") != NULL);

    ark_free_result(result);
    ark_free_site(site);
}

/* Mirrors test_html_escaping_of_text_content:
 *   output = render({"/": Page(Text("<script>alert(1)</script>"))})
 *   "<script>alert(1)</script>" not in html
 *   "&lt;script&gt;" in html */
static void test_html_escaping_of_text_content(void) {
    ArkNode* text = ark_text("<script>alert(1)</script>");
    ArkNode* children[] = {text};
    ArkNode* page = ark_page(children, 1, NULL);

    ArkSite* site;
    ArkBuildResult* result;
    const char* html = render_page(page, &site, &result);

    assert(strstr(html, "<script>alert(1)</script>") == NULL);
    assert(strstr(html, "&lt;script&gt;") != NULL);

    ark_free_result(result);
    ark_free_site(site);
}

/* Mirrors test_button_renders_as_button_tag:
 *   output = render({"/": Page(Button("Click me"))})
 *   "<button>Click me</button>" in output */
static void test_button_renders_as_button_tag(void) {
    ArkNode* button = ark_button("Click me", NULL);
    ArkNode* children[] = {button};
    ArkNode* page = ark_page(children, 1, NULL);

    ArkSite* site;
    ArkBuildResult* result;
    const char* html = render_page(page, &site, &result);

    assert(strstr(html, "<button>Click me</button>") != NULL);

    ark_free_result(result);
    ark_free_site(site);
}

/* Mirrors the on_click half of test_behavior_props_render_as_data_ark_attributes:
 *   output = render({"/": Page(Button("Show", on_click="toggle"))})
 *   'data-ark-on-click="toggle"' in html
 * (behavior_target/toggle_class not ported — no such props exist on
 * this port's Button yet, see carklight.h's Stage 0 comment.) */
static void test_on_click_renders_as_data_attribute(void) {
    ArkNode* button = ark_button("Show", "toggle");
    ArkNode* children[] = {button};
    ArkNode* page = ark_page(children, 1, NULL);

    ArkSite* site;
    ArkBuildResult* result;
    const char* html = render_page(page, &site, &result);

    assert(strstr(html, "data-ark-on-click=\"toggle\"") != NULL);

    ark_free_result(result);
    ark_free_site(site);
}

/* Not in the Python fixture directly (Container has no dedicated
 * test there — it's exercised implicitly throughout), but Container
 * -> <div> is an explicit TAG_MAP entry this port shares, so it gets
 * its own case: nesting, and that a Container's children render in
 * order inside the <div>. */
static void test_container_renders_as_div(void) {
    ArkNode* text_a = ark_text("a");
    ArkNode* text_b = ark_text("b");
    ArkNode* inner_children[] = {text_b};
    ArkNode* inner = ark_container_arr(inner_children, 1);
    ArkNode* outer_children[] = {text_a, inner};
    ArkNode* outer = ark_container_arr(outer_children, 2);
    ArkNode* page_children[] = {outer};
    ArkNode* page = ark_page(page_children, 1, NULL);

    ArkSite* site;
    ArkBuildResult* result;
    const char* html = render_page(page, &site, &result);

    assert(strstr(html, "<div><p>a</p><div><p>b</p></div></div>") != NULL);

    ark_free_result(result);
    ark_free_site(site);
}

/* Not in the Python fixture (ARKlight-py always has a site_name to
 * fall back to — see carklight.h's Stage 4 comment for why this port
 * can't). An absent title renders as an empty, still well-formed
 * <title></title> rather than omitting the tag or crashing. */
static void test_absent_title_renders_empty(void) {
    ArkNode* page = ark_page(NULL, 0, NULL);

    ArkSite* site;
    ArkBuildResult* result;
    const char* html = render_page(page, &site, &result);

    assert(strstr(html, "<title></title>") != NULL);

    ark_free_result(result);
    ark_free_site(site);
}

/* NULL site (and a site with a NULL root) is documented as a
 * no-op/empty-result convention, same as ark_ir_build(NULL) — 0
 * files added, not an error. */
static void test_null_site_handling(void) {
    ArkBuildResult* result = ark_build_result_new_empty();
    char* err = NULL;

    int rc = ark_html_render(NULL, NULL, result, &err);
    assert(rc == 0);
    assert(err == NULL);
    assert(ark_result_file_count(result) == 0);

    ArkSite* empty_site = ark_site_new_from_root(NULL);
    rc = ark_html_render(NULL, empty_site, result, &err);
    assert(rc == 0);
    assert(err == NULL);
    assert(ark_result_file_count(result) == 0);

    ark_free_site(empty_site);
    ark_free_result(result);
}

/* out == NULL is the one case ark_html_render treats as an actual
 * error (there is nowhere to put the rendered file). */
static void test_null_out_is_an_error(void) {
    ArkNode* page = ark_page(NULL, 0, NULL);
    ArkSite* site = ark_site_new_from_root(page);
    char* err = NULL;

    int rc = ark_html_render(NULL, site, NULL, &err);
    assert(rc != 0);
    assert(err != NULL);

    free(err);
    ark_free_site(site);
}

int main(void) {
    test_renders_basic_page_to_html();
    test_heading_level_prop_controls_tag();
    test_html_escaping_of_text_content();
    test_button_renders_as_button_tag();
    test_on_click_renders_as_data_attribute();
    test_container_renders_as_div();
    test_absent_title_renders_empty();
    test_null_site_handling();
    test_null_out_is_an_error();

    printf("stage4: all HTML backend cases passed\n");
    return 0;
}
