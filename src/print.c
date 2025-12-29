#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "print.h"

void writerWrite(Writer* writer, const char* data, size_t len) {
  if (len == 0) return;
  writer->write(writer->ctx, data, len);
}

void writerPrintf(Writer* writer, const char* format, ...) {
  char buf[128];

  va_list args;
  va_start(args, format);
  va_list argsCopy;
  va_copy(argsCopy, args);

  int n = vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);

  if (n < 0) {
    va_end(argsCopy);
    return;
  }

  if ((size_t)n < sizeof(buf)) {
    writerWrite(writer, buf, (size_t)n);
    va_end(argsCopy);
    return;
  }

  size_t cap = (size_t)n + 1;
  char* heap = (char*)malloc(cap);
  if (heap == NULL) exit(1);

  (void)vsnprintf(heap, cap, format, argsCopy);
  va_end(argsCopy);

  writerWrite(writer, heap, (size_t)n);
  free(heap);
}

static void fileWrite(void* ctx, const char* data, size_t len) {
  (void)fwrite(data, 1, len, (FILE*)ctx);
}

Writer writerForFile(FILE* file) {
  Writer writer;
  writer.write = fileWrite;
  writer.ctx = file;
  return writer;
}

