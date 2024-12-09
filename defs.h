//
// Created by sean on 19/04/20.
//

#ifndef ADD_SECTION_DEFS_H
#define ADD_SECTION_DEFS_H

#define CLEAN(error_code) status=error_code; goto cleanup
#define CHECK(v) do { \
    status = (v); \
    if (status!=EXIT_SUCCESS) goto cleanup; \
} while(0)

#define CHECK_NOT_NULL(v) do { \
    if (NULL == (v)) { \
        CLEAN(EXIT_FAILURE); \
    } \
} while(0)

#include <stdlib.h>

#endif //ADD_SECTION_DEFS_H
