/*
 * Pigweed log backend for RT595 semihosting.
 */

#ifndef APP_ENABLE_SEMIHOST
#define APP_ENABLE_SEMIHOST 1
#endif

#include <stdarg.h>
#include <stdio.h>

#include "pw_log/levels.h"

static void semihost_write0(const char *message)
{
#if APP_ENABLE_SEMIHOST
    register unsigned int operation asm("r0") = 0x04U;
    register const char *parameter asm("r1") = message;

    __asm volatile(
        "bkpt 0xAB"
        : "+r"(operation)
        : "r"(parameter)
        : "memory");
#else
    (void)message;
#endif
}

static const char *pigweed_log_level_name(int level)
{
    switch (level)
    {
        case PW_LOG_LEVEL_DEBUG:
            return "DBG";
        case PW_LOG_LEVEL_INFO:
            return "INF";
        case PW_LOG_LEVEL_WARN:
            return "WRN";
        case PW_LOG_LEVEL_ERROR:
            return "ERR";
        case PW_LOG_LEVEL_CRITICAL:
            return "FTL";
        default:
            return "UNK";
    }
}

void pigweed_log_backend_Log(int level,
                             const char *module_name,
                             const char *file_name,
                             int line_number,
                             const char *format,
                             ...)
{
#if !APP_ENABLE_SEMIHOST
    (void)level;
    (void)module_name;
    (void)file_name;
    (void)line_number;
    (void)format;
    return;
#else
    char buffer[256];
    int prefix_length;
    va_list args;

    prefix_length = snprintf(buffer,
                             sizeof(buffer),
                             "%s %s %s:%d ",
                             pigweed_log_level_name(level),
                             (module_name != NULL) ? module_name : "",
                             (file_name != NULL) ? file_name : "",
                             line_number);
    if ((prefix_length < 0) || (prefix_length >= (int)sizeof(buffer)))
    {
        semihost_write0("FTL pigweed log prefix overflow\n");
        return;
    }

    va_start(args, format);
    (void)vsnprintf(buffer + prefix_length,
                    sizeof(buffer) - (size_t)prefix_length,
                    format,
                    args);
    va_end(args);

    {
        size_t used = 0U;
        while ((used < sizeof(buffer)) && (buffer[used] != '\0'))
        {
            used++;
        }

        if ((used == 0U) || (buffer[used - 1U] != '\n'))
        {
            if (used < (sizeof(buffer) - 1U))
            {
                buffer[used++] = '\n';
                buffer[used] = '\0';
            }
            else
            {
                buffer[sizeof(buffer) - 2U] = '\n';
                buffer[sizeof(buffer) - 1U] = '\0';
            }
        }
    }

    semihost_write0(buffer);
#endif
}
