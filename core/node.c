#include "carklight.h"
#include "internal.h"

#include <string.h>

/* Copies src into a freshly ark_alloc'd buffer; returns NULL if src
 * is NULL, so optional fields (on_click, title) round-trip NULL
 * cleanly instead of turning into an empty string. Allocation goes
 * through core/alloc.c as of Stage 5e — see carklight.h's "Stage 5e"
 * block comment. */
static char* dup_or_null(const char* src) {
    if (src == NULL) {
        return NULL;
    }
    size_t len = strlen(src) + 1;
    char* copy = ark_alloc(len);
    if (copy == NULL) {
        return NULL; /* caller-visible as "text field ended up NULL" */
    }
    memcpy(copy, src, len);
    return copy;
}

/* Shared allocation path for every constructor below: zero-init so
 * every field not explicitly set by the caller is a well-defined
 * NULL/0, then fill in `type`. */
static ArkNode* node_alloc(component_type_t type) {
    ArkNode* node = ark_calloc(1, sizeof(ArkNode));
    if (node == NULL) {
        return NULL;
    }
    node->type = type;
    return node;
}

/* Copies the children array itself (caller keeps ownership of the
 * array it passed in) while taking ownership of each ArkNode* it
 * points to. Used by both ark_page and ark_container_arr. */
static ArkNode** dup_children(ArkNode** children, size_t n) {
    if (n == 0) {
        return NULL;
    }
    ArkNode** copy = ark_alloc(n * sizeof(ArkNode*));
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, children, n * sizeof(ArkNode*));
    return copy;
}

ArkNode* ark_page(ArkNode** children, size_t n, const char* title) {
    ArkNode* node = node_alloc(ARK_PAGE);
    if (node == NULL) {
        return NULL;
    }
    node->title = dup_or_null(title);
    node->children = dup_children(children, n);
    node->child_count = n;
    return node;
}

ArkNode* ark_heading(int level, const char* text) {
    ArkNode* node = node_alloc(ARK_HEADING);
    if (node == NULL) {
        return NULL;
    }
    node->level = level;
    node->text = dup_or_null(text);
    return node;
}

ArkNode* ark_text(const char* text) {
    ArkNode* node = node_alloc(ARK_TEXT);
    if (node == NULL) {
        return NULL;
    }
    node->text = dup_or_null(text);
    return node;
}

ArkNode* ark_button(const char* text, const char* on_click) {
    ArkNode* node = node_alloc(ARK_BUTTON);
    if (node == NULL) {
        return NULL;
    }
    node->text = dup_or_null(text);
    node->on_click = dup_or_null(on_click);
    return node;
}

ArkNode* ark_container_arr(ArkNode** children, size_t n) {
    ArkNode* node = node_alloc(ARK_CONTAINER);
    if (node == NULL) {
        return NULL;
    }
    node->children = dup_children(children, n);
    node->child_count = n;
    return node;
}

void ark_free_node(ArkNode* node) {
    if (node == NULL) {
        return;
    }
    for (size_t i = 0; i < node->child_count; i++) {
        ark_free_node(node->children[i]);
    }
    ark_dealloc(node->children);
    ark_dealloc(node->text);
    ark_dealloc(node->on_click);
    ark_dealloc(node->title);
    ark_dealloc(node);
}
