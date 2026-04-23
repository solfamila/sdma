#include <errno.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "fsl_debug_console.h"

caddr_t _sbrk(int incr) {
  static unsigned char heap[64 * 1024];
  static size_t used = 0U;

  if (incr < 0) {
    errno = ENOMEM;
    return (caddr_t)-1;
  }

  if (used + (size_t)incr > sizeof(heap)) {
    errno = ENOMEM;
    return (caddr_t)-1;
  }

  caddr_t previous = (caddr_t)&heap[used];
  used += (size_t)incr;
  return previous;
}

int _write(int handle, char* buffer, int size) {
  (void)handle;

  for (int index = 0; index < size; ++index) {
    DbgConsole_Putchar(buffer[index]);
  }

  return size;
}

int _read(int handle, char* buffer, int size) {
  (void)handle;
  (void)buffer;
  (void)size;
  errno = ENOSYS;
  return -1;
}

int _close(int file) {
  (void)file;
  errno = ENOSYS;
  return -1;
}

int _lseek(int file, int ptr, int dir) {
  (void)file;
  (void)ptr;
  (void)dir;
  return 0;
}

int _fstat(int file, struct stat* st) {
  (void)file;
  st->st_mode = S_IFCHR;
  return 0;
}

int _isatty(int file) {
  (void)file;
  return 1;
}

int _getpid(void) { return 1; }

int _kill(int pid, int sig) {
  (void)pid;
  (void)sig;
  errno = EINVAL;
  return -1;
}

int ftruncate(int file, off_t length) {
  (void)file;
  (void)length;
  errno = ENOSYS;
  return -1;
}

void _exit(int status) {
  (void)status;
  while (1) {
  }
}

void _init(void) {}

void _fini(void) {}