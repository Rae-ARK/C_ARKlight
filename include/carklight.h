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

/* Test/internal scaffold: an empty, immediately freeable build
 * result, exercising the same alloc/free discipline as the two types
 * above. Not connected to any backend yet — see Stage 4/5. */
ArkBuildResult* ark_build_result_new_empty(void);

void ark_free_result(ArkBuildResult* result);

#ifdef __cplusplus
}
#endif

#endif /* CARKLIGHT_H */
