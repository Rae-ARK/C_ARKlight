/*
 * test_js_backend.c — docs/IMPLEMENTATION.md Stage 5b.
 *
 * Hand-ported from ARKlight-py's `tests/test_js_backend.py`
 * (Rae-ARK/ARKlight, cloned separately as reference), restricted to
 * the cases reachable through Stage 0-3's data model — see
 * backends/js/render.c and carklight.h's Stage 5b block comment for
 * why the State/Bind/Action-driven cases
 * (test_js_runtime_includes_state_core_and_used_actions_only,
 * test_action_ref_targets_survive_into_ir) have no port here: there
 * is no ArkIRNode equivalent of ActionRef or a page-level state
 * concept to build one from yet.
 *
 * Every other Python case ports directly, because the behavior
 * fragments' `data-ark-target`/`data-ark-on-click`/
 * `data-ark-toggle-class` assertions are checking the *generated JS
 * source text* those fragments contain, not whether this port's HTML
 * backend actually renders a matching attribute (it doesn't yet —
 * `ark_button` has no behavior_target/toggle_class params, see
 * carklight.h) — so `Button("Show", on_click="toggle",
 * behavior_target="#panel")` in Python ports to plain
 * `ark_button("Show", "toggle")` here and the assertions still hold.
 */

#include "carklight.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Builds a single-page ArkSite, runs the JS backend on it, and
 * returns the rendered "arklight.js" contents as a NUL-terminated
 * string (owned by `result`'s file entry — do not free it directly,
 * free `result`/`site` once done reading it). */
static const char* render_js(ArkNode* page, ArkSite** site_out, ArkBuildResult** result_out) {
    ArkSite* site = ark_site_new_from_root(page);
    ArkBuildResult* result = ark_build_result_new_empty();

    char* err = NULL;
    int rc = ark_js_render(NULL, site, result, &err);
    assert(rc == 0);
    assert(err == NULL);
    assert(ark_result_file_count(result) == 1);
    assert(strcmp(ark_result_file_path(result, 0), "arklight.js") == 0);

    *site_out = site;
    *result_out = result;
    return (const char*)ark_result_file_data(result, 0, NULL);
}

/* Mirrors test_js_backend_returns_script_path:
 *   output = JSBackend().render(_plain_ir())
 *   set(output.keys()) == {SCRIPT_PATH} */
static void test_js_backend_returns_script_path(void) {
    ArkNode* text = ark_text("hi");
    ArkNode* children[] = {text};
    ArkNode* page = ark_page(children, 1, NULL);

    ArkSite* site;
    ArkBuildResult* result;
    render_js(page, &site, &result); /* render_js itself asserts the path/count */

    ark_free_result(result);
    ark_free_site(site);
}

/* Mirrors test_js_runtime_ships_no_behaviors_when_none_are_used:
 *   a page with no on_click at all gets none of the behavior
 *   fragments, and no wireBehaviors either. */
static void test_js_runtime_ships_no_behaviors_when_none_are_used(void) {
    ArkNode* text = ark_text("hi");
    ArkNode* children[] = {text};
    ArkNode* page = ark_page(children, 1, NULL);

    ArkSite* site;
    ArkBuildResult* result;
    const char* js = render_js(page, &site, &result);

    assert(strstr(js, "toggle:") == NULL);
    assert(strstr(js, "\"scroll-to\":") == NULL);
    assert(strstr(js, "copy:") == NULL);
    assert(strstr(js, "dismiss:") == NULL);
    assert(strstr(js, "wireBehaviors") == NULL);

    ark_free_result(result);
    ark_free_site(site);
}

/* Mirrors test_js_runtime_includes_only_the_behavior_actually_used:
 *   Page(Button("Show", on_click="toggle", behavior_target="#panel"))
 *   "toggle:" / "data-ark-on-click" / "data-ark-target" /
 *   "data-ark-toggle-class" in js; the other three behaviors absent.
 * (behavior_target isn't a param this port's ark_button has yet —
 * see this file's header comment for why the assertions still hold
 * without it.) */
static void test_js_runtime_includes_only_the_behavior_actually_used(void) {
    ArkNode* button = ark_button("Show", "toggle");
    ArkNode* children[] = {button};
    ArkNode* page = ark_page(children, 1, NULL);

    ArkSite* site;
    ArkBuildResult* result;
    const char* js = render_js(page, &site, &result);

    assert(strstr(js, "toggle:") != NULL);
    assert(strstr(js, "data-ark-on-click") != NULL);
    assert(strstr(js, "data-ark-target") != NULL);
    assert(strstr(js, "data-ark-toggle-class") != NULL);
    assert(strstr(js, "\"scroll-to\":") == NULL);
    assert(strstr(js, "copy:") == NULL);
    assert(strstr(js, "dismiss:") == NULL);

    ark_free_result(result);
    ark_free_site(site);
}

/* Mirrors test_js_runtime_highlights_active_nav_link_unconditionally:
 *   highlightActiveNavLink/"is-active" present even with no behaviors
 *   used at all. */
static void test_js_runtime_highlights_active_nav_link_unconditionally(void) {
    ArkNode* text = ark_text("hi");
    ArkNode* children[] = {text};
    ArkNode* page = ark_page(children, 1, NULL);

    ArkSite* site;
    ArkBuildResult* result;
    const char* js = render_js(page, &site, &result);

    assert(strstr(js, "highlightActiveNavLink") != NULL);
    assert(strstr(js, "is-active") != NULL);

    ark_free_result(result);
    ark_free_site(site);
}

/* Mirrors test_js_runtime_has_no_eval_or_new_function: sanity check
 * that the shipped runtime never executes an arbitrary string. */
static void test_js_runtime_has_no_eval_or_new_function(void) {
    ArkNode* text = ark_text("hi");
    ArkNode* children[] = {text};
    ArkNode* page = ark_page(children, 1, NULL);

    ArkSite* site;
    ArkBuildResult* result;
    const char* js = render_js(page, &site, &result);

    assert(strstr(js, "eval(") == NULL);
    assert(strstr(js, "new Function(") == NULL);

    ark_free_result(result);
    ark_free_site(site);
}

/* Mirrors test_js_runtime_implements_copy_and_dismiss_when_used:
 *   two buttons, on_click="copy" and on_click="dismiss"
 *   "copy:" / "dismiss:" / "navigator.clipboard" present,
 *   "toggle:" absent. */
static void test_js_runtime_implements_copy_and_dismiss_when_used(void) {
    ArkNode* copy_button = ark_button("Copy", "copy");
    ArkNode* dismiss_button = ark_button("Close", "dismiss");
    ArkNode* children[] = {copy_button, dismiss_button};
    ArkNode* page = ark_page(children, 2, NULL);

    ArkSite* site;
    ArkBuildResult* result;
    const char* js = render_js(page, &site, &result);

    assert(strstr(js, "copy:") != NULL);
    assert(strstr(js, "dismiss:") != NULL);
    assert(strstr(js, "navigator.clipboard") != NULL);
    assert(strstr(js, "toggle:") == NULL);

    ark_free_result(result);
    ark_free_site(site);
}

/* Mirrors test_js_runtime_omits_state_core_when_no_page_declares_state.
 * In render.py this is conditional on has_state being false; in this
 * port there is no State concept at all yet (see carklight.h's Stage
 * 5b comment), so the assertion holds unconditionally — kept as its
 * own case anyway, the same structural-guarantee role
 * test_css_backend.c's no-@media case plays for that backend. */
static void test_js_runtime_omits_state_core_unconditionally(void) {
    ArkNode* text = ark_text("hi");
    ArkNode* children[] = {text};
    ArkNode* page = ark_page(children, 1, NULL);

    ArkSite* site;
    ArkBuildResult* result;
    const char* js = render_js(page, &site, &result);

    assert(strstr(js, "createState") == NULL);
    assert(strstr(js, "data-ark-state") == NULL);
    assert(strstr(js, "wireActions") == NULL);

    ark_free_result(result);
    ark_free_site(site);
}

/* Not in the Python fixture (ARKlight-py's JSBackend.render(ir)
 * always receives a real ir from its test setup). This port's own
 * null-handling convention, matching ark_html_render/
 * test_html_backend.c: a NULL site (or a site with a NULL root) is
 * "nothing to render," not an error. */
static void test_null_site_handling(void) {
    ArkBuildResult* result = ark_build_result_new_empty();
    char* err = NULL;

    int rc = ark_js_render(NULL, NULL, result, &err);
    assert(rc == 0);
    assert(err == NULL);
    assert(ark_result_file_count(result) == 0);

    ArkSite* empty_site = ark_site_new_from_root(NULL);
    rc = ark_js_render(NULL, empty_site, result, &err);
    assert(rc == 0);
    assert(err == NULL);
    assert(ark_result_file_count(result) == 0);

    ark_free_site(empty_site);
    ark_free_result(result);
}

/* out == NULL is the one case ark_js_render treats as an actual
 * error, same convention as ark_html_render/ark_css_render. */
static void test_null_out_is_an_error(void) {
    ArkNode* page = ark_page(NULL, 0, NULL);
    ArkSite* site = ark_site_new_from_root(page);
    char* err = NULL;

    int rc = ark_js_render(NULL, site, NULL, &err);
    assert(rc != 0);
    assert(err != NULL);

    free(err);
    ark_free_site(site);
}

int main(void) {
    test_js_backend_returns_script_path();
    test_js_runtime_ships_no_behaviors_when_none_are_used();
    test_js_runtime_includes_only_the_behavior_actually_used();
    test_js_runtime_highlights_active_nav_link_unconditionally();
    test_js_runtime_has_no_eval_or_new_function();
    test_js_runtime_implements_copy_and_dismiss_when_used();
    test_js_runtime_omits_state_core_unconditionally();
    test_null_site_handling();
    test_null_out_is_an_error();

    printf("stage5b: all JS backend cases passed\n");
    return 0;
}
