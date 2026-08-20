/*
 * core/ir_build.c — Stage 3 (docs/IMPLEMENTATION.md): mirrors
 * `arklight.ir.build.build_website_ir` (specifically the
 * `_ark_node_to_ir_node` half of it — the `WebsiteIR`/`IRPage`
 * route-wrapping layer has no home yet, see carklight.h's Stage 3
 * doc comment for why).
 *
 * Per-type prop mapping, cross-checked against ARKlight-py's own
 * component signatures (`arklight/api.py` component factories +
 * `examples/hello_site/site.py` usage — `Heading("...", level=2)`,
 * `Page(*children, title=...)`, etc.):
 *
 *   Page(children, title)      -> type "Page",      props: title
 *   Heading(level, text)       -> type "Heading",    props: level;   text child
 *   Text(text)                 -> type "Text",       text child
 *   Button(text, on_click)     -> type "Button",     props: on_click; text child
 *   Container(children)        -> type "Container"
 *
 * This is a pure `const ArkNode* -> ArkIRNode*` transformation: it
 * never mutates `node`, never allocates/frees anything on the
 * ArkNode side, and assumes `node` already passed ark_normalize +
 * ark_validate (same precondition ARKlight-py's build_website_ir
 * places on its own caller).
 */

#include "carklight.h"
#include "internal.h"

#include <stdlib.h>
#include <string.h>

/* Copies src into a freshly malloc'd buffer; NULL in, NULL out — same
 * convention as node.c's own dup_or_null (kept as a separate static
 * copy here rather than shared across translation units, matching
 * the rest of core/'s per-file self-containment). */
static char* dup_or_null(const char* src) {
    if (src == NULL) {
        return NULL;
    }
    size_t len = strlen(src) + 1;
    char* copy = malloc(len);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, src, len);
    return copy;
}

/* Hand-written, mirroring the `type` string ARKlight-py's own
 * component factories stamp onto every ARKNode (`arklight/ast/
 * nodes.py`'s `node(type_name)` factory) — one entry per Stage 0
 * component_type_t. Every ArkNode reaching here was built through a
 * Stage 0 constructor, so `type` is always one of these five; the
 * fallback exists only for defensiveness against a corrupted/
 * out-of-range value, the same reachability gap noted in
 * core/validate.c. */
static const char* component_name(component_type_t type) {
    switch (type) {
        case ARK_PAGE:      return "Page";
        case ARK_HEADING:   return "Heading";
        case ARK_TEXT:      return "Text";
        case ARK_BUTTON:    return "Button";
        case ARK_CONTAINER: return "Container";
        default:            return "Unknown";
    }
}

ArkIRNode* ark_ir_build(const ArkNode* node) {
    if (node == NULL) {
        return NULL;
    }

    ArkIRNode* ir = calloc(1, sizeof(ArkIRNode));
    if (ir == NULL) {
        return NULL;
    }

    ir->type = dup_or_null(component_name(node->type));

    switch (node->type) {
        case ARK_PAGE:
            ir->prop_title = dup_or_null(node->title);
            break;
        case ARK_HEADING:
            ir->prop_level = node->level;
            ir->text = dup_or_null(node->text);
            break;
        case ARK_TEXT:
            ir->text = dup_or_null(node->text);
            break;
        case ARK_BUTTON:
            ir->prop_on_click = dup_or_null(node->on_click);
            ir->text = dup_or_null(node->text);
            break;
        case ARK_CONTAINER:
        default:
            break;
    }

    if (node->child_count > 0) {
        ir->children = malloc(node->child_count * sizeof(ArkIRNode*));
        if (ir->children != NULL) {
            for (size_t i = 0; i < node->child_count; i++) {
                ir->children[i] = ark_ir_build(node->children[i]);
            }
            ir->child_count = node->child_count;
        }
    }

    return ir;
}

void ark_ir_free(ArkIRNode* node) {
    if (node == NULL) {
        return;
    }
    for (size_t i = 0; i < node->child_count; i++) {
        ark_ir_free(node->children[i]);
    }
    free(node->children);
    free(node->text);
    free(node->prop_title);
    free(node->prop_on_click);
    free(node->type);
    free(node);
}

const char* ark_ir_type(const ArkIRNode* node) {
    return node != NULL ? node->type : NULL;
}

const char* ark_ir_text(const ArkIRNode* node) {
    return node != NULL ? node->text : NULL;
}

const char* ark_ir_prop_title(const ArkIRNode* node) {
    return node != NULL ? node->prop_title : NULL;
}

const char* ark_ir_prop_on_click(const ArkIRNode* node) {
    return node != NULL ? node->prop_on_click : NULL;
}

int ark_ir_level(const ArkIRNode* node) {
    return node != NULL ? node->prop_level : 0;
}

size_t ark_ir_child_count(const ArkIRNode* node) {
    return node != NULL ? node->child_count : 0;
}

const ArkIRNode* ark_ir_child_at(const ArkIRNode* node, size_t index) {
    if (node == NULL || index >= node->child_count) {
        return NULL;
    }
    return node->children[index];
}
