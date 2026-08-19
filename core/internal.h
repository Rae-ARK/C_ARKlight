/*
 * core/internal.h — private struct layouts backing the opaque public
 * types in carklight.h. Nothing outside core/ includes this file;
 * that boundary is what keeps ArkNode/ArkSite/ArkBuildResult opaque
 * to every caller, including carklight's own backends (which only
 * ever see them through the public header, same as any other
 * language binding — see docs/ADDENDUM.md §4.1).
 */

#ifndef CARKLIGHT_CORE_INTERNAL_H
#define CARKLIGHT_CORE_INTERNAL_H

#include "carklight.h"

/*
 * Deliberately a small, hand-written field set rather than a generic
 * key/value props table — matches the hand-written component_type_t
 * enum this stage uses (see carklight.h). A schema-driven, generic
 * props representation is Stage 8 work, generated from ARKlight-py's
 * own SCHEMA rather than designed by hand here.
 *
 * Not every field is meaningful for every `type`; which ones are is
 * determined by the constructor that built the node (ark_page,
 * ark_heading, ...), not enforced here — Stage 2 (validate) is where
 * that kind of shape-checking belongs.
 */
struct ArkNode {
    component_type_t type;

    char* text;       /* Heading/Text/Button: display text. NULL otherwise. */
    char* on_click;    /* Button only. NULL if absent. */
    char* title;       /* Page only. NULL if absent. */
    int   level;       /* Heading only. Unused (0) otherwise. */

    ArkNode** children; /* Page/Container: owned array of owned children. */
    size_t    child_count;
};

struct ArkSite {
    ArkNode* root; /* owned */
};

struct ArkBuildResult {
    /* Empty in Stage 0 — no backend populates this yet (Stage 4/5).
     * Field(s) reserved so ark_free_result has real (if trivial) work
     * to do, and so this struct's shape doesn't have to change shape
     * later just to go from "empty" to "holds files". */
    size_t file_count;
};

#endif /* CARKLIGHT_CORE_INTERNAL_H */
