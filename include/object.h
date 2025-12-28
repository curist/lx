#ifndef clox_object_h
#define clox_object_h

#include <stdio.h>
#include "chunk.h"
#include "value.h"
#include "table.h"

#define OBJ_TYPE(value)        (AS_OBJ(value)->type)

#define IS_CLOSURE(value)      isObjType(value, OBJ_CLOSURE)
#define IS_FUNCTION(value)     isObjType(value, OBJ_FUNCTION)
#define IS_NATIVE(value)       isObjType(value, OBJ_NATIVE)
#define IS_STRING(value)       isObjType(value, OBJ_STRING)
#define IS_HASHMAP(value)      isObjType(value, OBJ_HASHMAP)
#define IS_ENUM(value)         isObjType(value, OBJ_ENUM)
#define IS_ARRAY(value)        isObjType(value, OBJ_ARRAY)

#define AS_CLOSURE(value)      ((ObjClosure*)AS_OBJ(value))
#define AS_FUNCTION(value)     ((ObjFunction*)AS_OBJ(value))
#define AS_NATIVE(value)       (((ObjNative*)AS_OBJ(value)))
#define AS_STRING(value)       ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)      (((ObjString*)AS_OBJ(value))->chars)
#define AS_HASHMAP(value)      (((ObjHashmap*)AS_OBJ(value))->table)
#define AS_ENUM(value)         ((ObjEnum*)AS_OBJ(value))
#define AS_ENUM_FORWARD(value) (((ObjEnum*)AS_OBJ(value))->forward)
#define AS_ENUM_REVERSE(value) (((ObjEnum*)AS_OBJ(value))->reverse)
#define AS_ARRAY(value)        (((ObjArray*)AS_OBJ(value))->array)

#define COPY_CSTRING(string)   copyString(string, (int)strlen(string))
#define CSTRING_VAL(string)    OBJ_VAL(copyString(string, (int)strlen(string)))

typedef enum {
  OBJ_CLOSURE,
  OBJ_FUNCTION,
  OBJ_NATIVE,
  OBJ_STRING,
  OBJ_UPVALUE,
  OBJ_HASHMAP,
  OBJ_ENUM,
  OBJ_ARRAY,
} ObjType;

struct Obj {
  ObjType type;
  bool isMarked;
  struct Obj* next;
};

typedef struct {
  Obj obj;
  int arity;
  int upvalueCount;
  Chunk chunk;
  ObjString* name;
  ObjString* filename;
} ObjFunction;

typedef bool (*NativeFn)(int argCount, Value* args);

typedef struct {
  Obj obj;
  NativeFn function;
  ObjString* name;
} ObjNative;

typedef struct {
  Obj obj;
  Table table;
} ObjHashmap;

typedef struct {
  Obj obj;
  Table forward;  // name -> value
  Table reverse;  // value -> name
  ValueArray names; // declaration order member names
} ObjEnum;

typedef struct {
  Obj obj;
  ValueArray array;
} ObjArray;

struct ObjString {
  Obj obj;
  size_t length;
  char* chars;
  uint64_t hash;
};

typedef struct ObjUpvalue {
  Obj obj;
  Value* location;
  Value closed;
  struct ObjUpvalue* next;
} ObjUpvalue;

typedef struct {
  Obj obj;
  ObjFunction* function;
  ObjUpvalue** upvalues;
  int upvalueCount;
} ObjClosure;

ObjClosure* newClosure(ObjFunction* function);
ObjFunction* newFunction();
ObjNative* newNative(NativeFn function, ObjString* name);
ObjString* takeString(char* chars, int length);
ObjString* copyString(const char* chars, int length);
ObjUpvalue* newUpvalue(Value* slot);
ObjHashmap* newHashmap();
ObjEnum* newEnum();
ObjArray* newArray();
void printObject(FILE* fd, Value value);

static inline bool isObjType(Value value, ObjType type) {
  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif
