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

/*
 * Backing struct for the opaque ArkIRNode (Stage 3, core/ir_build.c).
 * Field shape deliberately mirrors ArkNode above — same hand-written-
 * fixed-fields-over-generic-props approach, same ownership discipline
 * — rather than modeling ARKlight-py's literal `type: str, props:
 * dict, children: list[IRNode | str]` shape. See the Stage 3 doc
 * comment in carklight.h for why `text` is a scalar field here
 * instead of a bare-string entry in `children`.
 */
struct ArkIRNode {
    char* type;           /* owned copy of the component name, e.g. "Page" */

    char* text;            /* Heading/Text/Button: the text-only child, as a scalar. NULL otherwise. */
    char* prop_title;      /* Page only. NULL if absent. */
    char* prop_on_click;   /* Button only. NULL if absent. */
    int   prop_level;      /* Heading only. Unused (0) otherwise. */

    ArkIRNode** children;  /* Page/Container: owned array of owned IR children. */
    size_t      child_count;
};

/*
 * One output file, as added by ark_build_result_add_file (Stage 4).
 * Both `path` and `data` are owned copies — the caller's originals
 * (a backend's local render buffer, typically) are never aliased or
 * retained past that call.
 */
typedef struct {
    char*    path;
    uint8_t* data;
    size_t   len;
} ArkResultFile;

struct ArkBuildResult {
    /* Stage 0 shipped this struct empty (file_count only, always 0)
     * so ark_free_result had real work to do ahead of any backend
     * existing. Stage 4 is the first to actually populate it — same
     * field name, now backed by a real array instead of staying 0
     * forever. */
    ArkResultFile* files;
    size_t         file_count;
};

#endif /* CARKLIGHT_CORE_INTERNAL_H */
