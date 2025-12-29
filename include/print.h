#ifndef clox_print_h
#define clox_print_h

#include <stddef.h>
#include <stdio.h>

#include "value.h"

typedef void (*WriterWriteFn)(void* ctx, const char* data, size_t len);

typedef struct Writer {
  WriterWriteFn write;
  void* ctx;
} Writer;

void writerWrite(Writer* writer, const char* data, size_t len);
void writerPrintf(Writer* writer, const char* format, ...);
Writer writerForFile(FILE* file);

void writeValue(Writer* writer, Value value);
void writeObject(Writer* writer, Value value);

#endif
