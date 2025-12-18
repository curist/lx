#ifndef clox_table_h
#define clox_table_h

#include "value.h"

typedef struct {
  Value key;
  Value value;
} Entry;

#define TABLE_GROUP_WIDTH 8
#define CTRL_EMPTY 0x80
#define CTRL_TOMB  0xFE

typedef struct {
  int count;
  int tombstones;
  int bucketCount;
  uint32_t bucketMask;
  int capacity;
  Entry* entries;
  uint8_t* control;
} Table;

void initTable(Table* table);
void freeTable(Table* table);
bool tableGet(Table* table, Value key, Value* value);
bool tableSet(Table* table, Value key, Value value);
bool tableDelete(Table* table, Value key);
void tableAddAll(Table* from, Table* to);
ObjString* tableFindString(Table* table, const char* chars, int length, uint64_t hash);
void tableRemoveWhite(Table* table);
void markTable(Table* table);

#endif
