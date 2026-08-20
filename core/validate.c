/*
 * core/validate.c — Stage 2 (docs/IMPLEMENTATION.md): mirrors
 * `arklight.ir.validate`.
 *
 * Three checks, per the stage's scope:
 *
 *   1. Schema membership — is `node->type` one of the known
 *      `component_type_t` values? Every public constructor in
 *      carklight.h can only ever produce a known type, so this only
 *      matters defensively (a corrupted/out-of-range value reaching
 *      here some other way) — it has no reachable rejection case
 *      through the public API today, the same honest gap Stage 1 had
 *      for its own two structurally-unreachable transforms.
 *
 *   2. Required-prop presence — Heading/Text/Button all carry their
 *      payload in `text` (see core/internal.h); Stage 0's
 *      constructors let it come back NULL (allocation failure) or
 *      empty, so Stage 2 is where "this component needs real text" is
 *      actually enforced. Heading's `level` is called out by name in
 *      carklight.h's own doc comment ("not validated at this stage
 *      (Stage 2's job)") — validated here against the 1-6 range.
 *
 *   3. Text-only-children rule — has no instantiable case among the
 *      five hand-written Stage 0 component types. Heading/Text/Button
 *      don't accept ArkNode children at all (their payload is the
 *      `text` field directly, not a children array), and Page/
 *      Container accept arbitrary component children by design — so
 *      there's no current component whose children must be
 *      Text-only. Schema membership is still checked recursively for
 *      every child a Page/Container does have.
 *
 * Error convention: matches the `char** err_out` shape
 * docs/ADDENDUM.md §4.1 already establishes for `ArkBackend`
 * callbacks. 0 = valid. Non-zero = invalid, and *err_out (if non-NULL)
 * is set to a malloc'd, caller-owned message describing the first
 * failure found (depth-first, first child first) — validation stops
 * at the first failure rather than collecting every error in the
 * tree.
 */

#include "carklight.h"
#include "internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    component_type_t type;
    const char* name;        /* for error messages */
    int requires_text;       /* Heading/Text/Button: text must be non-NULL, non-empty */
    int has_level_range;     /* Heading only */
    int level_min;
    int level_max;
} ark_schema_entry_t;

/* Hand-written, mirroring the shape of a handful of entries in
 * ARKlight-py's `arklight.ir.schema.SCHEMA` — see carklight.h's file
 * header for why only these five exist yet (Stage 8 generates the
 * full ~87-component set). */
static const ark_schema_entry_t SCHEMA[] = {
    { ARK_PAGE,      "Page",      0, 0, 0, 0 },
    { ARK_HEADING,   "Heading",   1, 1, 1, 6 },
    { ARK_TEXT,      "Text",      1, 0, 0, 0 },
    { ARK_BUTTON,    "Button",    1, 0, 0, 0 },
    { ARK_CONTAINER, "Container", 0, 0, 0, 0 },
};
static const size_t SCHEMA_COUNT = sizeof(SCHEMA) / sizeof(SCHEMA[0]);

static const ark_schema_entry_t* schema_lookup(component_type_t type) {
    for (size_t i = 0; i < SCHEMA_COUNT; i++) {
        if (SCHEMA[i].type == type) {
            return &SCHEMA[i];
        }
    }
    return NULL;
}

/* Formats a malloc'd error message into *err_out (if err_out is
 * non-NULL) and returns 1, so every call site can just `return
 * fail(...)`. If err_out is NULL the caller only wanted the pass/fail
 * signal, so the message is never built. */
static int fail(char** err_out, const char* fmt, ...) {
    if (err_out == NULL) {
        return 1;
    }
    *err_out = NULL;

    va_list ap;
    va_start(ap, fmt);
    char probe[1];
    int needed = vsnprintf(probe, sizeof probe, fmt, ap);
    va_end(ap);
    if (needed < 0) {
        return 1; /* formatting itself failed; leave *err_out NULL */
    }

    char* msg = malloc((size_t)needed + 1);
    if (msg == NULL) {
        return 1; /* OOM building the message; leave *err_out NULL */
    }

    va_start(ap, fmt);
    vsnprintf(msg, (size_t)needed + 1, fmt, ap);
    va_end(ap);

    *err_out = msg;
    return 1;
}

int ark_validate(const ArkNode* node, char** err_out) {
    if (err_out != NULL) {
        *err_out = NULL;
    }
    if (node == NULL) {
        return 0; /* nothing to validate */
    }

    const ark_schema_entry_t* entry = schema_lookup(node->type);
    if (entry == NULL) {
        return fail(err_out,
                     "carklight: validate: unknown component type %d",
                     (int)node->type);
    }

    if (entry->requires_text && (node->text == NULL || node->text[0] == '\0')) {
        return fail(err_out,
                     "carklight: validate: %s requires non-empty text",
                     entry->name);
    }

    if (entry->has_level_range &&
        (node->level < entry->level_min || node->level > entry->level_max)) {
        return fail(err_out,
                     "carklight: validate: %s level must be between %d and %d (got %d)",
                     entry->name, entry->level_min, entry->level_max, node->level);
    }

    for (size_t i = 0; i < node->child_count; i++) {
        int rc = ark_validate(node->children[i], err_out);
        if (rc != 0) {
            return rc;
        }
    }

    return 0;
}
