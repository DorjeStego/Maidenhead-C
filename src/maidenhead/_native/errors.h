#ifndef MAIDENHEAD_NATIVE_ERRORS_H
#define MAIDENHEAD_NATIVE_ERRORS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Error codes returned by native functions. */
typedef enum mh_status {
    MH_OK = 0,
    MH_ERR_INVALID_LOCATOR = 1,
    MH_ERR_PRECISION = 2,
    MH_ERR_OUT_OF_RANGE = 3,
    MH_ERR_UNSUPPORTED = 4,
    MH_ERR_MISSING_DEP = 5,
    MH_ERR_INTERNAL = 6
} mh_status;

/* Error context to enrich messages on the Python side. */
typedef struct mh_error_context {
    mh_status code;
    const char *message;
    const char *key;
    const char *value;
} mh_error_context;

#ifdef __cplusplus
}
#endif

#endif
