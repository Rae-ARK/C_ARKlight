/*
 * core/normalize.c — Stage 1 (docs/IMPLEMENTATION.md): mirrors
 * `arklight.ir.normalize`.
 *
 * ARKlight-py's normalize does three things to a dynamically-typed
 * Python AST: flatten nested lists, drop None/False-equivalent
 * children, and wrap bare strings as Text nodes. carklight's Stage 0
 * struct model is already statically typed at construction time —
 * every ark_* constructor requires a real ArkNode*, so there is no
 * "bare string child" or "child that's itself a list" representable
 * in the tree at all. Those two transforms are therefore no-ops here,
 * not because they were skipped, but because the C data model already
 * rules them out structurally (see docs/IMPLEMENTATION.md Stage 1).
 *
 * What *does* carry over: a children array can still contain a NULL
 * ArkNode* entry (the natural C encoding of "this child was
 * conditionally omitted," e.g. built from `cond ? ark_text(...) :
 * NULL`) — that's this port's equivalent of Python's None/False
 * child, and pruning it is the one transform Stage 1 actually
 * performs, recursively.
 */

#include "carklight.h"
#include "internal.h"

/* ark_normalize mutates and returns `node` in place: children arrays
 * are compacted (NULL entries removed, count shrunk) rather than
 * reallocated, so no new ArkNode is ever allocated by this pass and
 * no existing one is freed. Safe to call on an already-normalized
 * tree (idempotent) and on NULL (returns NULL). */
ArkNode* ark_normalize(ArkNode* node) {
    if (node == NULL) {
        return NULL;
    }

    size_t write = 0;
    for (size_t read = 0; read < node->child_count; read++) {
        ArkNode* child = node->children[read];
        if (child == NULL) {
            /* None/False-equivalent child: dropped, nothing to free —
             * a NULL slot never owned an allocation. */
            continue;
        }
        node->children[write++] = ark_normalize(child);
    }
    node->child_count = write;

    return node;
}
