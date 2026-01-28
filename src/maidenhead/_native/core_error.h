#ifndef MAIDENHEAD_NATIVE_CORE_ERROR_H
#define MAIDENHEAD_NATIVE_CORE_ERROR_H

#include "errors.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline void mh_set_error(mh_error_context *err, mh_status code, const char *message) {
    if (!err) {
        return;
    }
    err->code = code;
    err->message = message;
    err->key = NULL;
    err->value = NULL;
}

#ifdef __cplusplus
}
#endif

#endif
