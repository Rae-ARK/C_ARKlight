#include "carklight.h"
#include "internal.h"

/* Allocation goes through core/alloc.c as of Stage 5e — see
 * carklight.h's "Stage 5e" block comment. */

ArkSite* ark_site_new_from_root(ArkNode* root) {
    ArkSite* site = ark_alloc(sizeof(ArkSite));
    if (site == NULL) {
        return NULL;
    }
    site->root = root; /* takes ownership */
    return site;
}

void ark_free_site(ArkSite* site) {
    if (site == NULL) {
        return;
    }
    ark_free_node(site->root);
    ark_dealloc(site);
}

const ArkNode* ark_site_root(const ArkSite* site) {
    return site != NULL ? site->root : NULL;
}
