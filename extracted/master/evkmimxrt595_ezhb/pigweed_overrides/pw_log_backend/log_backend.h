#pragma once

#include "pw_preprocessor/compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

void pigweed_log_backend_Log(int level,
                             const char *module_name,
                             const char *file_name,
                             int line_number,
                             const char *format,
                             ...) PW_PRINTF_FORMAT(5, 6);

#ifdef __cplusplus
}
#endif

#define PW_HANDLE_LOG(level, module, flags, ...) \
    pigweed_log_backend_Log(level, module, __FILE__, __LINE__, __VA_ARGS__)
