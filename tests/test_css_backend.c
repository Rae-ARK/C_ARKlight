/*
 * test_css_backend.c — docs/IMPLEMENTATION.md Stage 5a.
 *
 * Hand-ported from ARKlight-py's `tests/test_css_backend.py`
 * (Rae-ARK/ARKlight, cloned separately as reference). Every case
 * there builds a page ({"/": Page(Text("hi"))}) purely to get an
 * `ir` to pass to `CSSBackend().render(ir)` — the CSS backend never
 * actually consults it (see backends/css/render.c's header comment).
 * This port keeps the same shape (build a trivial site, render,
 * inspect the stylesheet) for parity with the Python fixture, plus
 * two extra cases (NULL site, NULL out) matching the convention
 * test_html_backend.c already established for this port's own
 * null-handling guarantees.
 */

#include "carklight.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Builds a single-page ArkSite, runs the CSS backend on it, and
 * returns the rendered "styles.css" contents as a NUL-terminated
 * string (owned by `result`'s file entry — do not free it directly,
 * free `result`/`site` once done reading it). */
static const char* render_css(ArkNode* page, ArkSite** site_out, ArkBuildResult** result_out) {
    ArkSite* site = ark_site_new_from_root(page);
    ArkBuildResult* result = ark_build_result_new_empty();

    char* err = NULL;
    int rc = ark_css_render(NULL, site, result, &err);
    assert(rc == 0);
    assert(err == NULL);

    *site_out = site;
    *result_out = result;
    return (const char*)ark_result_file_data(result, 0, NULL);
}

/* Mirrors test_css_backend_returns_stylesheet_path:
 *   output = CSSBackend().render(ir)
 *   set(output.keys()) == {STYLESHEET_PATH} */
static void test_css_backend_returns_stylesheet_path(void) {
    ArkNode* text = ark_text("hi");
    ArkNode* children[] = {text};
    ArkNode* page = ark_page(children, 1, NULL);

    ArkSite* site;
    ArkBuildResult* result;
    render_css(page, &site, &result);

    assert(ark_result_file_count(result) == 1);
    assert(strcmp(ark_result_file_path(result, 0), "styles.css") == 0);

    ark_free_result(result);
    ark_free_site(site);
}

/* Mirrors test_css_backend_stylesheet_covers_core_tags:
 *   for selector in ("body", "h1", "p", "button", "a", ".nav", ".card"):
 *       f"{selector} {{" in css */
static void test_css_backend_stylesheet_covers_core_tags(void) {
    ArkNode* text = ark_text("hi");
    ArkNode* children[] = {text};
    ArkNode* page = ark_page(children, 1, NULL);

    ArkSite* site;
    ArkBuildResult* result;
    const char* css = render_css(page, &site, &result);

    static const char* const selectors[] = {
        "body {", "h1 {", "p {", "button {", "a {", ".nav {", ".card {",
    };
    for (size_t i = 0; i < sizeof(selectors) / sizeof(selectors[0]); i++) {
        assert(strstr(css, selectors[i]) != NULL);
    }

    ark_free_result(result);
    ark_free_site(site);
}

/* Mirrors test_css_backend_stylesheet_covers_v0_004_tags:
 *   for selector in ("details", "summary", "code", "pre", "blockquote",
 *                     "table", "th, td", "input, textarea, select",
 *                     "fieldset", "legend"):
 *       f"{selector} {{" in css */
static void test_css_backend_stylesheet_covers_v0_004_tags(void) {
    ArkNode* text = ark_text("hi");
    ArkNode* children[] = {text};
    ArkNode* page = ark_page(children, 1, NULL);

    ArkSite* site;
    ArkBuildResult* result;
    const char* css = render_css(page, &site, &result);

    static const char* const selectors[] = {
        "details {", "summary {", "code {", "pre {", "blockquote {",
        "table {", "th, td {", "input, textarea, select {", "fieldset {",
        "legend {",
    };
    for (size_t i = 0; i < sizeof(selectors) / sizeof(selectors[0]); i++) {
        assert(strstr(css, selectors[i]) != NULL);
    }

    ark_free_result(result);
    ark_free_site(site);
}

/* Mirrors test_css_backend_includes_intrinsic_layout_utilities:
 *   for utility_class in (".stack", ".cluster", ".sidebar", ".switcher",
 *                          ".grid", ".center", ".reel"):
 *       f"{utility_class} {{" in css or f"{utility_class} >" in css */
static void test_css_backend_includes_intrinsic_layout_utilities(void) {
    ArkNode* text = ark_text("hi");
    ArkNode* children[] = {text};
    ArkNode* page = ark_page(children, 1, NULL);

    ArkSite* site;
    ArkBuildResult* result;
    const char* css = render_css(page, &site, &result);

    static const char* const classes[] = {
        ".stack", ".cluster", ".sidebar", ".switcher", ".grid", ".center", ".reel",
    };
    char needle_brace[32];
    char needle_child[32];
    for (size_t i = 0; i < sizeof(classes) / sizeof(classes[0]); i++) {
        snprintf(needle_brace, sizeof(needle_brace), "%s {", classes[i]);
        snprintf(needle_child, sizeof(needle_child), "%s >", classes[i]);
        assert(strstr(css, needle_brace) != NULL || strstr(css, needle_child) != NULL);
    }

    ark_free_result(result);
    ark_free_site(site);
}

/* Mirrors test_css_backend_has_no_media_or_container_queries: strips
 * CSS comments, then asserts "@media" and "@container" are absent
 * from what remains — the structural "no breakpoints, ever"
 * constraint docs/DESIGN-NOTES.md documents (Page has no <head> hook
 * for one). No regex here; a linear comment-stripping scan does the
 * same job C doesn't need `re` for. */
static void test_css_backend_has_no_media_or_container_queries(void) {
    ArkNode* text = ark_text("hi");
    ArkNode* children[] = {text};
    ArkNode* page = ark_page(children, 1, NULL);

    ArkSite* site;
    ArkBuildResult* result;
    const char* css = render_css(page, &site, &result);

    size_t len = strlen(css);
    char* code_only = malloc(len + 1);
    assert(code_only != NULL);
    size_t out_len = 0;
    int in_comment = 0;
    for (size_t i = 0; i < len; i++) {
        if (!in_comment && css[i] == '/' && i + 1 < len && css[i + 1] == '*') {
            in_comment = 1;
            i++;
            continue;
        }
        if (in_comment) {
            if (css[i] == '*' && i + 1 < len && css[i + 1] == '/') {
                in_comment = 0;
                i++;
            }
            continue;
        }
        code_only[out_len++] = css[i];
    }
    code_only[out_len] = '\0';

    assert(strstr(code_only, "@media") == NULL);
    assert(strstr(code_only, "@container") == NULL);

    free(code_only);
    ark_free_result(result);
    ark_free_site(site);
}

/* Not in the Python fixture — ARKlight-py's CSSBackend always
 * receives a real `ir` from its test setup, so there is no NULL-site
 * equivalent there. This port's own convention (test_html_backend.c)
 * is that a NULL site is never an error; for the CSS backend
 * specifically, it doesn't even mean "nothing to render" the way it
 * does for HTML, since the stylesheet doesn't depend on the site at
 * all (see backends/css/render.c's header comment) — the file is
 * still produced. */
static void test_null_site_still_produces_stylesheet(void) {
    ArkBuildResult* result = ark_build_result_new_empty();
    char* err = NULL;

    int rc = ark_css_render(NULL, NULL, result, &err);
    assert(rc == 0);
    assert(err == NULL);
    assert(ark_result_file_count(result) == 1);
    assert(strcmp(ark_result_file_path(result, 0), "styles.css") == 0);

    ark_free_result(result);
}

/* out == NULL is the one case ark_css_render treats as an actual
 * error, same convention as ark_html_render. */
static void test_null_out_is_an_error(void) {
    ArkNode* page = ark_page(NULL, 0, NULL);
    ArkSite* site = ark_site_new_from_root(page);
    char* err = NULL;

    int rc = ark_css_render(NULL, site, NULL, &err);
    assert(rc != 0);
    assert(err != NULL);

    free(err);
    ark_free_site(site);
}

int main(void) {
    test_css_backend_returns_stylesheet_path();
    test_css_backend_stylesheet_covers_core_tags();
    test_css_backend_stylesheet_covers_v0_004_tags();
    test_css_backend_includes_intrinsic_layout_utilities();
    test_css_backend_has_no_media_or_container_queries();
    test_null_site_still_produces_stylesheet();
    test_null_out_is_an_error();

    printf("stage5a: all CSS backend cases passed\n");
    return 0;
}
