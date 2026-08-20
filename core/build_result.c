/*
 * core/build_result.c — ArkBuildResult alloc/free (Stage 0) plus,
 * from Stage 4 onward, the file storage every backend's `render`
 * fills via ark_build_result_add_file and every caller reads back via
 * ark_result_file_count/ark_result_file_path/ark_result_file_data
 * (docs/carklight.h's Stage 4 block comment; names match
 * PROPOSAL.md §3.4's public surface).
 */

#include "carklight.h"
#include "internal.h"

#include <stdlib.h>
#include <string.h>

/* Copies src into a freshly malloc'd, NUL-terminated buffer; NULL in,
 * NULL out — same convention as node.c/ir_build.c's own dup_or_null,
 * kept as a separate static copy here per core/'s per-file
 * self-containment convention. */
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

ArkBuildResult* ark_build_result_new_empty(void) {
    ArkBuildResult* result = calloc(1, sizeof(ArkBuildResult));
    return result; /* files NULL, file_count 0 via calloc */
}

int ark_build_result_add_file(ArkBuildResult* result, const char* path,
                               const uint8_t* data, size_t len) {
    if (result == NULL || path == NULL) {
        return 1;
    }

    ArkResultFile* grown = realloc(result->files,
        (result->file_count + 1) * sizeof(ArkResultFile));
    if (grown == NULL) {
        return 1; /* original result->files/file_count left untouched */
    }
    result->files = grown;

    char* path_copy = dup_or_null(path);
    if (path_copy == NULL) {
        return 1;
    }

    /* Allocated one byte larger than `len` and NUL-terminated,
     * regardless of content — `len` itself still reports the exact
     * byte length a binary-output backend would need, but this way a
     * text-producing backend's (Stage 4's HTML, today) output can
     * also be handed straight to a C-string function without a
     * caller-side copy. */
    uint8_t* data_copy = malloc(len + 1);
    if (data_copy == NULL) {
        free(path_copy);
        return 1;
    }
    if (len > 0) {
        memcpy(data_copy, data, len);
    }
    data_copy[len] = '\0';

    result->files[result->file_count].path = path_copy;
    result->files[result->file_count].data = data_copy;
    result->files[result->file_count].len = len;
    result->file_count++;
    return 0;
}

size_t ark_result_file_count(const ArkBuildResult* result) {
    return result != NULL ? result->file_count : 0;
}

const char* ark_result_file_path(const ArkBuildResult* result, size_t index) {
    if (result == NULL || index >= result->file_count) {
        return NULL;
    }
    return result->files[index].path;
}

const uint8_t* ark_result_file_data(const ArkBuildResult* result, size_t index,
                                     size_t* len_out) {
    if (result == NULL || index >= result->file_count) {
        return NULL;
    }
    if (len_out != NULL) {
        *len_out = result->files[index].len;
    }
    return result->files[index].data;
}

void ark_free_result(ArkBuildResult* result) {
    if (result == NULL) {
        return;
    }
    for (size_t i = 0; i < result->file_count; i++) {
        free(result->files[i].path);
        free(result->files[i].data);
    }
    free(result->files);
    free(result);
}
