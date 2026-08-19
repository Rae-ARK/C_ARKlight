#include "carklight.h"
#include "internal.h"

#include <stdlib.h>

ArkBuildResult* ark_build_result_new_empty(void) {
    ArkBuildResult* result = calloc(1, sizeof(ArkBuildResult));
    return result; /* file_count is 0 via calloc; no files yet (Stage 4/5) */
}

void ark_free_result(ArkBuildResult* result) {
    free(result); /* no owned file data yet — nothing else to release */
}
