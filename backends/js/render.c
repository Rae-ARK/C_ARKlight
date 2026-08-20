/*
 * backends/js/render.c — Stage 5b (docs/IMPLEMENTATION.md): mirrors
 * `arklight.backend.js.render` (Rae-ARK/ARKlight, cloned separately
 * as reference), scoped to what Stages 0-3 actually carry — same
 * "port what's structurally reachable" discipline Stage 4 and Stage
 * 5a already applied.
 *
 * ARKlight-py's JS backend ships two closed vocabularies: named
 * behaviors (`toggle`/`scroll-to`/`copy`/`dismiss`, keyed off a
 * Button's `on_click` string prop) and a reactive-state runtime keyed
 * off `State`/`Bind`/`Action.*` (an `ActionRef` on `on_click` instead
 * of a plain string). carklight's IR (Stage 3) only carries the
 * former: `on_click` is always `const char*` or NULL
 * (ark_ir_prop_on_click) — there is no `ArkIRNode` equivalent of
 * `ActionRef`, and no page-level `state` concept for `ArkSite` to
 * carry yet. So this port ships the four named-behavior fragments and
 * the unconditional nav-link highlighter, and nothing from
 * `_STATE_CORE_JS`/`ACTION_FRAGMENTS`/`_actions_block` — that half of
 * upstream's runtime has nowhere to attach until some earlier stage's
 * data model grows a State/Bind/Action shape, the same ordering every
 * prior stage's scope note already establishes for its own gaps.
 *
 * One more gap worth naming explicitly: the behavior fragments below
 * read `data-ark-target`/`data-ark-toggle-class` off the clicked
 * element at runtime (verbatim from upstream), but carklight's
 * `ark_button` (Stage 0) has no `behavior_target`/`toggle_class`
 * params yet, and the HTML backend (Stage 4) therefore never emits
 * those attributes. Shipping the fragment text is still correct
 * ARKlight-py doesn't gate fragment *inclusion* on whether the
 * attribute is renderable, and a future ark_button growing those
 * params only has to teach Stage 4 to emit the attribute — this file
 * doesn't change.
 *
 * Per ADDENDUM.md §4.1, this file only ever reaches ArkSite/
 * ArkBuildResult/ArkIRNode through carklight.h — never core/
 * internal.h. Previously used a private growable strbuf duplicated
 * from backends/html/render.c's own copy; Stage 5e (undocumented —
 * see docs/Think different, life easy/CORE_HANDLER.md) replaced both
 * private copies with the one shared ArkBuf type carklight.h now
 * exposes, since "duplicated per backend instead of shared" was
 * exactly the growable-buffer problem that doc's §1 called out. This
 * doesn't reintroduce cross-backend coupling — ArkBuf is a
 * general-purpose core/ utility exposed through the same public
 * header this file already only ever includes, not a dependency on
 * another backend.
 */

#include "carklight.h"

#include <string.h>

/* Where the HTML backend expects to find the generated runtime,
 * relative to the output directory root — mirrors render.py's
 * SCRIPT_PATH constant. */
#define ARK_JS_SCRIPT_PATH "arklight.js"

/* --- Growable byte buffer (same shape as backends/html/render.c's;
 * see that file for the rationale — duplicated per-backend rather
 * than shared, per this file's header comment). --------------------- */

typedef ArkBuf strbuf_t;

static int sb_init(strbuf_t* sb) {
    return ark_buf_init(sb);
}

static void sb_free(strbuf_t* sb) {
    ark_buf_free(sb);
}

static int sb_append_n(strbuf_t* sb, const char* s, size_t n) {
    return ark_buf_append_n(sb, s, n);
}

static int sb_append(strbuf_t* sb, const char* s) {
    return ark_buf_append(sb, s);
}

/* --- Fixed runtime fragments -------------------------------------------
 * Verbatim ports of render.py's _NOTIFY_JS / _NAV_HIGHLIGHT_JS module
 * constants and the four behaviors/ *.py JS_FRAGMENT strings
 * (arklight.backend.js.behaviors, imported there as
 * BEHAVIOR_FRAGMENTS). No eval, no new Function — every fragment here
 * is a small, statically-readable JS function, same discipline
 * render.py's own header comment documents.
 */

static const char* const ARK_JS_NOTIFY =
    "  function arkNotify(message) {\n"
    "    // Self-contained on-page notice for runtime edge cases the fixed\n"
    "    // behavior/action vocabulary didn't anticipate -- deliberately\n"
    "    // inline-styled (not a `.stack`/`.card`/etc. class) so it renders\n"
    "    // correctly even on a page whose stylesheet this failure might\n"
    "    // itself be related to, and wrapped in its own try/catch so a\n"
    "    // notification failure can never become a second, worse error.\n"
    "    try {\n"
    "      var el = document.getElementById(\"ark-notify\");\n"
    "      if (!el) {\n"
    "        el = document.createElement(\"div\");\n"
    "        el.id = \"ark-notify\";\n"
    "        el.setAttribute(\"role\", \"alert\");\n"
    "        el.style.cssText =\n"
    "          \"position:fixed;bottom:1rem;right:1rem;left:auto;max-width:22rem;\" +\n"
    "          \"background:#111827;color:#f9fafb;padding:0.75rem 1rem;\" +\n"
    "          \"border-radius:0.5rem;font:14px/1.4 system-ui,sans-serif;\" +\n"
    "          \"box-shadow:0 4px 12px rgba(0,0,0,.35);z-index:2147483647;\";\n"
    "        document.body.appendChild(el);\n"
    "      }\n"
    "      el.textContent = message;\n"
    "      el.style.display = \"block\";\n"
    "      clearTimeout(el._arkNotifyTimer);\n"
    "      el._arkNotifyTimer = setTimeout(function () {\n"
    "        el.style.display = \"none\";\n"
    "      }, 6000);\n"
    "    } catch (notifyErr) {\n"
    "      /* notification is best-effort; never let it throw */\n"
    "    }\n"
    "  }";

static const char* const ARK_JS_NAV_HIGHLIGHT =
    "  function highlightActiveNavLink() {\n"
    "    document.querySelectorAll(\".nav a\").forEach(function (link) {\n"
    "      var here = location.href.replace(/#.*$/, \"\");\n"
    "      var there = link.href.replace(/#.*$/, \"\");\n"
    "      if (there === here) {\n"
    "        link.classList.add(\"is-active\");\n"
    "      }\n"
    "    });\n"
    "  }";

static const char* const ARK_JS_FRAGMENT_TOGGLE =
    "    toggle: function (el) {\n"
    "      var selector = el.getAttribute(\"data-ark-target\");\n"
    "      var className = el.getAttribute(\"data-ark-toggle-class\") || \"is-open\";\n"
    "      if (!selector) return;\n"
    "      document.querySelectorAll(selector).forEach(function (target) {\n"
    "        target.classList.toggle(className);\n"
    "      });\n"
    "    }";

static const char* const ARK_JS_FRAGMENT_SCROLL_TO =
    "    \"scroll-to\": function (el) {\n"
    "      var selector = el.getAttribute(\"data-ark-target\");\n"
    "      if (!selector) return;\n"
    "      var target = document.querySelector(selector);\n"
    "      if (target && target.scrollIntoView) {\n"
    "        target.scrollIntoView({ behavior: \"smooth\" });\n"
    "      }\n"
    "    }";

static const char* const ARK_JS_FRAGMENT_COPY =
    "    copy: function (el) {\n"
    "      var selector = el.getAttribute(\"data-ark-target\");\n"
    "      if (!selector) return;\n"
    "      var target = document.querySelector(selector);\n"
    "      if (!target || !navigator.clipboard) {\n"
    "        arkNotify(\"Copy isn't available in this browser or context.\");\n"
    "        return;\n"
    "      }\n"
    "      var text = target.value !== undefined && target.tagName === \"TEXTAREA\"\n"
    "        ? target.value\n"
    "        : target.textContent;\n"
    "      navigator.clipboard.writeText(text.trim()).then(function () {\n"
    "        var original = el.textContent;\n"
    "        el.textContent = \"Copied!\";\n"
    "        setTimeout(function () {\n"
    "          el.textContent = original;\n"
    "        }, 1500);\n"
    "      }).catch(function () {\n"
    "        arkNotify(\"Couldn't copy to clipboard -- try selecting and copying the text manually.\");\n"
    "      });\n"
    "    }";

static const char* const ARK_JS_FRAGMENT_DISMISS =
    "    dismiss: function (el) {\n"
    "      var selector = el.getAttribute(\"data-ark-target\");\n"
    "      var className = el.getAttribute(\"data-ark-toggle-class\") || \"hidden\";\n"
    "      if (!selector) return;\n"
    "      document.querySelectorAll(selector).forEach(function (target) {\n"
    "        target.classList.add(className);\n"
    "      });\n"
    "    }";

static const char* const ARK_JS_WIRE_BEHAVIORS =
    "  function wireBehaviors() {\n"
    "    document.querySelectorAll(\"[data-ark-on-click]\").forEach(function (el) {\n"
    "      try {\n"
    "        var name = el.getAttribute(\"data-ark-on-click\");\n"
    "        var behavior = behaviors[name];\n"
    "        if (!behavior) return;\n"
    "        el.addEventListener(\"click\", function (event) {\n"
    "          event.preventDefault();\n"
    "          try {\n"
    "            behavior(el);\n"
    "          } catch (err) {\n"
    "            arkNotify(\"Something went wrong running this action -- an unsupported or unexpected case was hit.\");\n"
    "          }\n"
    "        });\n"
    "      } catch (err) {\n"
    "        // One malformed element must not abort wiring for\n"
    "        // every other behavior-tagged element on the page.\n"
    "        arkNotify(\"One of this page's interactive elements couldn't be set up.\");\n"
    "      }\n"
    "    });\n"
    "  }";

/* --- Usage collection ---------------------------------------------------
 * Mirrors render.py's _collect_usage, restricted to the behavior half
 * of what it tracks (see this file's header comment for why the
 * action/state half has no ArkIRNode shape to walk yet). The
 * vocabulary is closed and small (four names), so this is four
 * booleans rather than a general string set — same information
 * render.py's `used_behaviors: set[str]` carries, just sized to what
 * BEHAVIOR_FRAGMENTS actually holds in this port instead of an
 * open-ended collection.
 */

typedef struct {
    int used_toggle;
    int used_scroll_to;
    int used_copy;
    int used_dismiss;
} ark_behavior_usage_t;

static void collect_usage(const ArkIRNode* node, ark_behavior_usage_t* usage) {
    if (node == NULL) {
        return;
    }
    /* ark_ir_prop_on_click returns NULL for anything but a Button
     * node, so no type check is needed before calling it — mirrors
     * render.py's `node.props.get("on_click")`, which is similarly
     * just absent (None) on every other IRNode type. */
    const char* on_click = ark_ir_prop_on_click(node);
    if (on_click != NULL) {
        if (strcmp(on_click, "toggle") == 0) {
            usage->used_toggle = 1;
        } else if (strcmp(on_click, "scroll-to") == 0) {
            usage->used_scroll_to = 1;
        } else if (strcmp(on_click, "copy") == 0) {
            usage->used_copy = 1;
        } else if (strcmp(on_click, "dismiss") == 0) {
            usage->used_dismiss = 1;
        }
        /* Anything else (a typo, or a name from a vocabulary this
         * port hasn't grown yet) is silently not wired at runtime,
         * same as render.py's `if name in BEHAVIOR_FRAGMENTS` filter
         * — schema-level rejection of an unknown behavior name is
         * Stage 2's job, not this backend's (and isn't implemented
         * for behavior names yet either — see core/validate.c). */
    }

    size_t n = ark_ir_child_count(node);
    for (size_t i = 0; i < n; i++) {
        collect_usage(ark_ir_child_at(node, i), usage);
    }
}

/* --- Runtime assembly ---------------------------------------------------
 * Mirrors render.py's _behaviors_block + _build_runtime_js, minus the
 * _actions_block/_STATE_CORE_JS half (see this file's header
 * comment). Appends fragments in the same order render.py's
 * `sorted(used_behaviors)` would produce for this closed vocabulary
 * ("copy" < "dismiss" < "scroll-to" < "toggle"), so a site using
 * several behaviors gets the same fragment ordering upstream would.
 */

/* Appends the `var behaviors = {...}` object and the wireBehaviors()
 * function to `sb`, in that order, matching _behaviors_block's
 * returned string shape. Only called when `usage` has at least one
 * flag set. */
static int append_behaviors_block(strbuf_t* sb, const ark_behavior_usage_t* usage) {
    if (sb_append(sb, "  var behaviors = {\n") != 0) {
        return 1;
    }
    int first = 1;
    if (usage->used_copy) {
        if (!first && sb_append(sb, ",\n") != 0) return 1;
        if (sb_append(sb, ARK_JS_FRAGMENT_COPY) != 0) return 1;
        first = 0;
    }
    if (usage->used_dismiss) {
        if (!first && sb_append(sb, ",\n") != 0) return 1;
        if (sb_append(sb, ARK_JS_FRAGMENT_DISMISS) != 0) return 1;
        first = 0;
    }
    if (usage->used_scroll_to) {
        if (!first && sb_append(sb, ",\n") != 0) return 1;
        if (sb_append(sb, ARK_JS_FRAGMENT_SCROLL_TO) != 0) return 1;
        first = 0;
    }
    if (usage->used_toggle) {
        if (!first && sb_append(sb, ",\n") != 0) return 1;
        if (sb_append(sb, ARK_JS_FRAGMENT_TOGGLE) != 0) return 1;
        first = 0;
    }
    if (sb_append(sb, "\n  };\n\n") != 0) {
        return 1;
    }
    return sb_append(sb, ARK_JS_WIRE_BEHAVIORS);
}

/* Mirrors _build_runtime_js: assembles the full arklight.js text for
 * `usage`, IIFE-wrapped, with the notify helper included only when
 * the behaviors block needs it (matching needs_notify = bool(
 * behaviors_block) in render.py, minus the "or has_state" half this
 * port never has). Returns 0 on success. */
static int build_runtime_js(strbuf_t* sb, const ark_behavior_usage_t* usage) {
    int has_any_behavior = usage->used_toggle || usage->used_scroll_to ||
                            usage->used_copy || usage->used_dismiss;

    if (sb_append(sb,
            "// Generated by ARKlight -- v0.0035 runtime.\n"
            "// Implements only the named behaviors this site actually uses --\n"
            "// see arklight.ir.schema.BEHAVIOR_REGISTRY. No other JavaScript\n"
            "// runs on this site.\n"
            "(function () {\n"
            "  \"use strict\";\n"
            "\n") != 0) {
        return 1;
    }

    if (has_any_behavior) {
        if (sb_append(sb, ARK_JS_NOTIFY) != 0 || sb_append(sb, "\n\n") != 0) {
            return 1;
        }
        if (append_behaviors_block(sb, usage) != 0 || sb_append(sb, "\n\n") != 0) {
            return 1;
        }
    }

    if (sb_append(sb, ARK_JS_NAV_HIGHLIGHT) != 0 || sb_append(sb, "\n\n") != 0) {
        return 1;
    }

    if (sb_append(sb, "  document.addEventListener(\"DOMContentLoaded\", function () {\n") != 0) {
        return 1;
    }
    if (has_any_behavior && sb_append(sb, "    wireBehaviors();\n") != 0) {
        return 1;
    }
    if (sb_append(sb, "    highlightActiveNavLink();\n") != 0) {
        return 1;
    }
    return sb_append(sb, "  });\n})();\n");
}

/* --- Public entry points ------------------------------------------------ */

/* Allocation goes through core/alloc.c's public ark_alloc as of
 * Stage 5e — see carklight.h's "Stage 5e" block comment. */
static char* err_dup(const char* msg) {
    size_t len = strlen(msg) + 1;
    char* copy = ark_alloc(len);
    if (copy != NULL) {
        memcpy(copy, msg, len);
    }
    return copy;
}

int ark_js_render(ArkBackend* self, const ArkSite* site,
                   ArkBuildResult* out, char** err_out) {
    (void)self; /* no per-instance state (matches ark_html_render/ark_css_render) */

    if (err_out != NULL) {
        *err_out = NULL;
    }
    if (out == NULL) {
        if (err_out != NULL) {
            *err_out = err_dup("ark_js_render: out is NULL");
        }
        return 1;
    }

    const ArkNode* root = ark_site_root(site);
    if (root == NULL) {
        return 0; /* nothing to render — same convention as ark_html_render */
    }

    ArkIRNode* ir = ark_ir_build(root);
    if (ir == NULL) {
        if (err_out != NULL) {
            *err_out = err_dup("ark_js_render: ark_ir_build failed");
        }
        return 1;
    }

    ark_behavior_usage_t usage = {0, 0, 0, 0};
    collect_usage(ir, &usage);

    strbuf_t sb;
    if (sb_init(&sb) != 0) {
        ark_ir_free(ir);
        if (err_out != NULL) {
            *err_out = err_dup("ark_js_render: out of memory");
        }
        return 1;
    }

    int build_rc = build_runtime_js(&sb, &usage);
    ark_ir_free(ir);
    if (build_rc != 0) {
        sb_free(&sb);
        if (err_out != NULL) {
            *err_out = err_dup("ark_js_render: out of memory");
        }
        return 1;
    }

    int add_rc = ark_build_result_add_file(out, ARK_JS_SCRIPT_PATH,
        (const uint8_t*)sb.data, sb.len);
    sb_free(&sb);
    if (add_rc != 0) {
        if (err_out != NULL) {
            *err_out = err_dup("ark_js_render: failed to store output file");
        }
        return 1;
    }

    return 0;
}

static ArkBackend g_js_backend = {
    "js",
    ARK_BACKEND_JS,
    NULL,             /* init */
    ark_js_render,
    NULL,             /* postprocess — deferred per IMPLEMENTATION.md Stage 5 */
    NULL,             /* shutdown */
};

const ArkBackend* ark_js_backend(void) {
    return &g_js_backend;
}
