/*
 * carklight.h — public C ABI surface.
 *
 * Stage 0 (docs/IMPLEMENTATION.md): data model & header only. No
 * pipeline logic lives behind these declarations yet — normalize
 * (Stage 1), validate (Stage 2), IR build (Stage 3), and the HTML/
 * CSS/JS backends (Stages 4-5) are not wired in. What's here is the
 * struct model every later stage builds on, plus a hand-written,
 * deliberately small subset of node constructors covering the shapes
 * PROPOSAL.md §3.4 already sketches (Page/Heading/Text/Button/
 * Container). The full ~87-component `component_type_t` set and its
 * generated constructors land in Stage 8, driven off ARKlight-py's
 * own SCHEMA rather than hand-maintained here.
 *
 * ArkSite and ArkBuildResult exist as opaque, independently
 * allocatable/freeable types per Stage 0's scope ("a matched
 * ark_*_new/ark_free_* pair for every type"), but their *_new
 * constructors here are test/internal scaffolding only — not the
 * public `ark_load_arklight`/`ark_build` entry points described in
 * PROPOSAL.md §3.4 and TERMINOLOGY.md, which require normalize/
 * validate/backend logic this stage explicitly defers.
 */

#ifndef CARKLIGHT_H
#define CARKLIGHT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Opaque types --------------------------------------------------- */

typedef struct ArkNode ArkNode;
typedef struct ArkSite ArkSite;
typedef struct ArkBuildResult ArkBuildResult;
typedef struct ArkIRNode ArkIRNode;

/* --- Component types -------------------------------------------------
 * Hand-written for Stage 0. Mirrors the shape of a handful of entries
 * in ARKlight-py's `arklight.ir.schema.SCHEMA`, not the full set —
 * see the file header above and docs/IMPLEMENTATION.md Stage 8.
 */
typedef enum {
    ARK_PAGE,
    ARK_HEADING,
    ARK_TEXT,
    ARK_BUTTON,
    ARK_CONTAINER,
} component_type_t;

/* --- Node construction -----------------------------------------------
 * One constructor per component_type_t entry above, matching
 * PROPOSAL.md §3.4's example surface. Every ArkNode* returned here,
 * regardless of which constructor produced it, is freed the same way:
 * ark_free_node(), which walks and frees children recursively. There
 * is deliberately one free function for the whole ArkNode family
 * rather than one per constructor — ownership is uniform across
 * component types even though construction isn't.
 */

/* Page(children, title). title may be NULL. Does not take ownership
 * of the children array itself (caller-owned, copied in); takes
 * ownership of each child node it references. */
ArkNode* ark_page(ArkNode** children, size_t n, const char* title);

/* Heading(level, text). level is 1-6, not validated at this stage
 * (Stage 2's job). text is copied; caller retains ownership of the
 * pointer passed in. */
ArkNode* ark_heading(int level, const char* text);

/* Text(text). Copied, same ownership rule as ark_heading. */
ArkNode* ark_text(const char* text);

/* Button(text, on_click). on_click may be NULL. Both copied. */
ArkNode* ark_button(const char* text, const char* on_click);

/* Container(children, n). Same child-ownership rule as ark_page. */
ArkNode* ark_container_arr(ArkNode** children, size_t n);

/* Frees a node and, recursively, every child it owns. Safe to call
 * with NULL (no-op). Calling it twice on the same pointer is a
 * double-free, same as free() — callers are responsible for not
 * doing that, same convention the rest of this header uses. */
void ark_free_node(ArkNode* node);

/* --- Stage 1: normalize -----------------------------------------------
 * Mirrors `arklight.ir.normalize`. Pure recursive tree transformation:
 * no I/O, no schema knowledge (that's Stage 2). Mutates and returns
 * `node` in place — every ArkNode* this returns is the same pointer
 * passed in, with child arrays compacted; nothing is allocated or
 * freed by this call. See core/normalize.c for why "flatten nested
 * lists" / "wrap bare strings as Text" are no-ops in this statically-
 * typed port: NULL children (the None/False-equivalent case) are the
 * one thing this pass actually prunes. Idempotent; NULL in, NULL out. */
ArkNode* ark_normalize(ArkNode* node);

/* --- Stage 2: validate --------------------------------------------------
 * Mirrors `arklight.ir.validate`. Recursive, read-only (never mutates
 * `node`). Checks, in order, the first time any of them fails: schema
 * membership (is node->type a known component_type_t?), required-prop
 * presence (Heading/Text/Button all need non-empty text; Heading's
 * level must be 1-6), then recurses into children — so schema
 * membership is effectively checked for the whole tree, not just the
 * root. See core/validate.c for why the text-only-children rule has
 * no reachable case among the five hand-written Stage 0 component
 * types.
 *
 * Returns 0 if valid. Returns non-zero if invalid, and — if err_out is
 * non-NULL — sets *err_out to a malloc'd, caller-owned message
 * describing the first failure found; caller must free() it. Passing
 * err_out as NULL is fine when only the pass/fail result matters.
 * NULL node is valid (returns 0, *err_out untouched/NULL). */
int ark_validate(const ArkNode* node, char** err_out);

/* --- Stage 3: Website IR / build --------------------------------------
 * Mirrors `arklight.ir.build` (ARKlight-py) — converts a validated,
 * normalized ArkNode tree into the backend-independent IR: type/props/
 * children, modeling intent rather than HTML. Callers are expected to
 * have already run ark_normalize + ark_validate on `node`, same
 * precondition as `build_website_ir` places on its Python caller —
 * this stage doesn't re-check either.
 *
 * ARKlight-py's `IRNode.children` is a heterogeneous
 * `list[IRNode | str]`, because a text-only component's (Heading/
 * Text/Button) single text argument is itself stored as a bare-string
 * *child*, not a prop. carklight's ArkNode already made text a scalar
 * field at Stage 0 (see internal.h), so there's no bare-string child
 * to carry through here — ark_ir_text() below is this port's
 * equivalent of Python's `heading.children == ["Title"]`, and
 * ArkIRNode's own children (ark_ir_child_count/ark_ir_child_at) are
 * always nested ArkIRNode, never strings. See core/ir_build.c for the
 * full mapping from ArkNode fields to IR props per component type.
 *
 * ARKlight-py's `WebsiteIR` additionally wraps one `IRNode` per named
 * route under a `site_name` (`IRPage`/`WebsiteIR` in `ir/build.py`).
 * carklight's ArkSite (Stage 0) is a single-root scaffold with no
 * route/site_name concept yet, so that wrapping layer has nowhere to
 * live in this port until ArkSite itself grows one — deferred, not
 * part of Stage 3's scope here.
 *
 * Opaque, like ArkNode/ArkSite/ArkBuildResult — accessors below are
 * how a caller (here, tests/test_ir_build.c) inspects the shape
 * ARKlight-py's tests read directly off IRNode's dataclass fields.
 */

/* Builds a fresh ArkIRNode tree from `node`. Every ArkIRNode returned,
 * anywhere in the tree, is freed via ark_ir_free(). NULL in, NULL out. */
ArkIRNode* ark_ir_build(const ArkNode* node);

/* Frees an IR node and, recursively, every child it owns. Safe to
 * call with NULL (no-op), same convention as ark_free_node. */
void ark_ir_free(ArkIRNode* node);

/* The component name this node was built from, e.g. "Page",
 * "Heading" — mirrors IRNode.type (`str` in ARKlight-py). Never NULL
 * for a non-NULL node. */
const char* ark_ir_type(const ArkIRNode* node);

/* The text-only child's text, for nodes built from Heading/Text/
 * Button — mirrors ARKlight-py's `children == [text]` shape for those
 * types (see the block comment above). NULL for Page/Container, and
 * for a NULL node. */
const char* ark_ir_text(const ArkIRNode* node);

/* Page's `title` prop. NULL if this isn't a Page node, or if the
 * source ArkNode's title was itself NULL. */
const char* ark_ir_prop_title(const ArkIRNode* node);

/* Button's `on_click` prop. NULL if this isn't a Button node, or if
 * the source ArkNode's on_click was itself NULL. */
const char* ark_ir_prop_on_click(const ArkIRNode* node);

/* Heading's `level` prop. Unused (0) for every other node type, same
 * convention as ArkNode.level in internal.h. */
int ark_ir_level(const ArkIRNode* node);

/* Number of (nested-IRNode) children — 0 for Heading/Text/Button,
 * which carry their payload via ark_ir_text() instead. 0 for a NULL
 * node. */
size_t ark_ir_child_count(const ArkIRNode* node);

/* Child at `index`, or NULL if index >= ark_ir_child_count(node). */
const ArkIRNode* ark_ir_child_at(const ArkIRNode* node, size_t index);

/* --- Site & build result: alloc/free scaffolding only for now --------
 * Real construction (ark_load_arklight, ark_build) is not part of
 * Stage 0 — see the file header above.
 */

/* Test/internal scaffold: wraps an already-built ArkNode* tree as an
 * ArkSite for the purpose of exercising alloc/free discipline before
 * any real pipeline exists. Takes ownership of root. NOT the public
 * ark_load_arklight/ark_load_root entry points (PROPOSAL.md §3.4,
 * ADDENDUM.md §1) — those require normalize/validate (Stages 1-2)
 * this stage doesn't implement, and will replace this scaffold
 * outright once they land. */
ArkSite* ark_site_new_from_root(ArkNode* root);

void ark_free_site(ArkSite* site);

/* Read-only access to the root a site was built from. Added in Stage
 * 4 so a backend (which only ever sees ArkSite through this header,
 * never core/internal.h — see docs/ADDENDUM.md §4.1) has a way to
 * reach the tree it's asked to render. NULL for a NULL site. */
const ArkNode* ark_site_root(const ArkSite* site);

/* Test/internal scaffold: an empty, immediately freeable build
 * result, exercising the same alloc/free discipline as the two types
 * above. Not connected to any backend yet — see Stage 4/5. */
ArkBuildResult* ark_build_result_new_empty(void);

/* Appends one output file to `result`, copying both `path` and
 * `data` (caller retains ownership of what it passed in) — the
 * primitive every backend's `render` uses to fill an ArkBuildResult,
 * since ArkBuildResult stays opaque outside core/ the same as every
 * other type here (docs/ADDENDUM.md §4.1's "backends only ever see
 * these through carklight.h" rule applies to writing, not just
 * reading). Returns 0 on success, non-zero on allocation failure (the
 * file is not added). `len` is the byte length of `data`; `data`
 * itself need not be NUL-terminated text — Stage 4's HTML output
 * happens to be, later backends' output may not be. */
int ark_build_result_add_file(ArkBuildResult* result, const char* path,
                               const uint8_t* data, size_t len);

/* Note: the stored copy is always allocated one byte larger than
 * `len` and NUL-terminated, regardless of content, so a text-
 * producing backend's output (Stage 4's HTML, today) can be handed
 * straight to a C-string function without a caller-side copy — `len`
 * itself still reports the exact content length either way, for a
 * future binary-output backend that needs it. */

/* Accessors mirroring PROPOSAL.md §3.4's public surface
 * (`ark_result_file_count`/`ark_result_file_path`/
 * `ark_result_file_data`), usable today against whatever a backend's
 * `render` has already added via ark_build_result_add_file, ahead of
 * `ark_build`/`ark_load_ir` themselves landing (Stage 6/7). */
size_t ark_result_file_count(const ArkBuildResult* result);

/* Path of the file at `index` (e.g. "index.html"), or NULL if index
 * is out of range. Owned by `result`; valid until ark_free_result. */
const char* ark_result_file_path(const ArkBuildResult* result, size_t index);

/* Bytes of the file at `index`; NULL (and *len_out, if non-NULL, left
 * untouched) if index is out of range. Owned by `result`, same
 * lifetime rule as ark_result_file_path. len_out may be NULL if the
 * caller doesn't need the length. */
const uint8_t* ark_result_file_data(const ArkBuildResult* result, size_t index,
                                     size_t* len_out);

void ark_free_result(ArkBuildResult* result);

/* --- Backend interface (docs/ADDENDUM.md §4.1) -------------------------
 * The one fixed contract every backend implements — HTML today
 * (Stage 4), CSS/JS next (Stage 5). `ark_build` (Stage 6/7) will walk
 * a compile-time-registered array of these rather than special-casing
 * any one backend by name; nothing about that dispatch exists yet,
 * but the struct shape is fixed now so Stage 4 has something concrete
 * to implement against.
 *
 * `init`/`postprocess`/`shutdown` are optional (NULL-able); `render`
 * is required. A backend receives the validated `ArkSite` — not a
 * pre-built IR tree — and is responsible for calling ark_ir_build
 * itself (mirrors ARKlight-py's pipeline calling build_website_ir
 * once, upstream of every backend, but carklight has nowhere to cache
 * that shared IR tree yet without ArkSite growing a field for it, so
 * each backend builds and frees its own for now).
 */
typedef struct ArkBackend {
    const char* name;   /* "html", "css", "js", ... */
    uint32_t    flag;   /* one of ARK_BACKEND_* below */
    int  (*init)(struct ArkBackend* self, char** err_out);
    int  (*render)(struct ArkBackend* self, const ArkSite* site,
                    ArkBuildResult* out, char** err_out);
    int  (*postprocess)(struct ArkBackend* self, ArkBuildResult* out,
                         char** err_out);
    void (*shutdown)(struct ArkBackend* self);
} ArkBackend;

#define ARK_BACKEND_HTML (1u << 0)
#define ARK_BACKEND_CSS  (1u << 1)
#define ARK_BACKEND_JS   (1u << 2)

/* --- Stage 4: HTML backend ---------------------------------------------
 * Mirrors `arklight.backend.html.render` — the first backend written
 * against the ArkBackend interface above, and the first stage
 * producing actual output bytes. Confined to backends/html/, and only
 * ever reaches ArkSite/ArkBuildResult through this header, same as
 * any other language binding would (docs/ADDENDUM.md §4.1).
 *
 * Scope, matched to what Stages 0-3 actually carry (five component
 * types, no props table, no routes — see carklight.h's own Stage 0/3
 * comments): tag mapping per component type (Page -> the document's
 * <body>, Heading -> h1-h6 per its level prop, Text -> <p>, Button ->
 * <button> with on_click becoming a data-ark-on-click attribute,
 * Container -> <div>), and HTML-escaping of every text value and
 * attribute value emitted. Every render call produces exactly one
 * output file, "index.html" — ARKlight-py's own root-route mapping
 * (`_output_path_for_route("/") == "index.html"`) applied to the one
 * root ArkSite already has, since there's no route/site_name concept
 * yet for any other route to hang off of (same gap Stage 3's doc
 * comment calls out).
 *
 * Explicitly deferred, matching docs/IMPLEMENTATION.md's Stage 4
 * scope note: the CSS/JS backends (Stage 5) — so no <link
 * rel="stylesheet">/<script> tags yet either, since both would point
 * at output this port doesn't produce until then. Also deferred,
 * because nothing upstream of Stage 4 carries them yet: Link/Image
 * (and therefore internal href/src rewriting), the generic props
 * table (class/style/aria-prefixed/unknown-prop-as-data-attribute),
 * and page-scoped state/Bind/Action. Each becomes eligible for this
 * backend the same way it became eligible for Stages 0-3: only once
 * some earlier stage's data model carries it.
 */

/* Renders `site` to HTML, appending the result to `out` via
 * ark_build_result_add_file. Matches ArkBackend.render's signature so
 * it can be wired in as one directly; `self` is unused today (no
 * per-instance state) but kept for interface conformance. Assumes
 * `site`'s root already passed ark_normalize + ark_validate, same
 * precondition ark_ir_build itself places on its caller. A NULL site,
 * or a site with a NULL root, is treated as "nothing to render" — 0
 * files added, return 0 (not an error). Returns 0 on success,
 * non-zero on failure (OOM or a failed ark_build_result_add_file),
 * setting *err_out (if non-NULL) to a malloc'd, caller-owned message. */
int ark_html_render(ArkBackend* self, const ArkSite* site,
                     ArkBuildResult* out, char** err_out);

/* The compile-time-registered HTML ArkBackend instance itself —
 * {name = "html", flag = ARK_BACKEND_HTML, render = ark_html_render,
 * everything else NULL}. Returns the same static instance every
 * call; never NULL. */
const ArkBackend* ark_html_backend(void);

#ifdef __cplusplus
}
#endif

#endif /* CARKLIGHT_H */
