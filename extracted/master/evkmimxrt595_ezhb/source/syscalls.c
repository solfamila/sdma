/*
 * Minimal bare-metal syscall support required by newlib formatting routines.
 */

#include <errno.h>
#include <stddef.h>
#include <sys/types.h>

caddr_t _sbrk(int incr)
{
    static unsigned char heap[4096];
    static size_t used = 0U;
    size_t next_used;

    if (incr < 0)
    {
        errno = ENOMEM;
        return (caddr_t)-1;
    }

    next_used = used + (size_t)incr;
    if (next_used > sizeof(heap))
    {
        errno = ENOMEM;
        return (caddr_t)-1;
    }

    {
        caddr_t previous = (caddr_t)&heap[used];
        used = next_used;
        return previous;
    }
}