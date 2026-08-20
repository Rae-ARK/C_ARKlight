#include "carklight.h"
#include "internal.h"

#include <stdlib.h>

ArkSite* ark_site_new_from_root(ArkNode* root) {
    ArkSite* site = malloc(sizeof(ArkSite));
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
    free(site);
}

const ArkNode* ark_site_root(const ArkSite* site) {
    return site != NULL ? site->root : NULL;
}
