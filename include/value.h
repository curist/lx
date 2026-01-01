#ifndef clox_value_h
#define clox_value_h

#include <stdio.h>
#include <string.h>

#include "common.h"

typedef struct Obj Obj;
typedef struct ObjString ObjString;

typedef enum {
  VAL_BOOL,
  VAL_NIL,
  VAL_NUMBER,
  VAL_OBJ,
} ValueType;

#define SIGN_BIT ((uint64_t)0x8000000000000000)
#define QNAN     ((uint64_t)0x7ffc000000000000)

#define PAYLOAD_MASK ((uint64_t)0x0000FFFFFFFFFFFF)

#define TAG_MASK   ((uint64_t)0x3)
#define TAG_FIXNUM ((uint64_t)0) // 00
#define TAG_NIL   1 // 01.
#define TAG_FALSE 2 // 10.
#define TAG_TRUE  3 // 11.

// Fixnum payload layout (within the 48-bit NaN payload):
// - low 2 bits: tag (00)
// - remaining 46 bits: signed two's-complement integer
#define FIXNUM_TAG_BITS 2
#define FIXNUM_SHIFT FIXNUM_TAG_BITS
#define FIXNUM_BITS (48 - FIXNUM_TAG_BITS)

#define FIXNUM_MAX ((int64_t)((INT64_C(1) << (FIXNUM_BITS - 1)) - 1))
#define FIXNUM_MIN (-(INT64_C(1) << (FIXNUM_BITS - 1)))

#define FIXNUM_PAYLOAD_MASK ((uint64_t)((UINT64_C(1) << FIXNUM_BITS) - 1))

typedef uint64_t Value;

#define IS_BOOL(value)      ((value) == TRUE_VAL || (value) == FALSE_VAL)
#define IS_NIL(value)       ((value) == NIL_VAL)
#define IS_FIXNUM(value) \
    ((((value) & (QNAN | SIGN_BIT)) == QNAN) && (((value) & TAG_MASK) == TAG_FIXNUM))
#define IS_NUMBER(value)    (IS_FIXNUM(value) || (((value) & QNAN) != QNAN))
#define IS_OBJ(value) \
    (((value) & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))

#define AS_BOOL(value)      ((value) == TRUE_VAL)
#define AS_FIXNUM(value)    valueToFixnum(value)
#define AS_NUMBER(value)    valueToNumber(value)
#define AS_OBJ(value) \
    ((Obj*)(uintptr_t)((value) & ~(SIGN_BIT | QNAN)))

#define BOOL_VAL(b)     ((b) ? TRUE_VAL : FALSE_VAL)
#define FALSE_VAL       ((Value)(QNAN | TAG_FALSE))
#define TRUE_VAL        ((Value)(QNAN | TAG_TRUE))
#define NIL_VAL         ((Value)(QNAN | TAG_NIL))
#define FIXNUM_VAL(num) fixnumToValue((int64_t)(num))
#define NUMBER_VAL(num) numToValue(num)
#define OBJ_VAL(obj) \
    (Value)(SIGN_BIT | QNAN | (uintptr_t)(obj))

static inline double valueToNum(Value value) {
  double num;
  memcpy(&num, &value, sizeof(Value));
  return num;
}

static inline Value numToValue(double num) {
  // Canonicalize NaN to avoid collision with fixnum bit pattern.
  // NaN has the property that NaN != NaN in IEEE 754.
  // Standard quiet NaN 0x7ff8... cannot match QNAN pattern 0x7ffc...
  if (num != num) {
    const uint64_t CANON_NAN = UINT64_C(0x7ff8000000000000);
    return (Value)CANON_NAN;
  }
  Value value;
  memcpy(&value, &num, sizeof(double));
  return value;
}

static inline bool fixnumFitsInt64(int64_t num) {
  return num >= FIXNUM_MIN && num <= FIXNUM_MAX;
}

static inline Value fixnumToValue(int64_t num) {
  // Caller is responsible for overflow handling (e.g., fallback to flonum).
  uint64_t payload = ((uint64_t)num) & FIXNUM_PAYLOAD_MASK;
  return (Value)(QNAN | (payload << FIXNUM_SHIFT) | TAG_FIXNUM);
}

static inline int64_t valueToFixnum(Value value) {
  uint64_t payload = (value & PAYLOAD_MASK) >> FIXNUM_SHIFT;
  if (payload & (UINT64_C(1) << (FIXNUM_BITS - 1))) {
    payload |= ~FIXNUM_PAYLOAD_MASK;
  }
  return (int64_t)payload;
}

static inline double valueToNumber(Value value) {
  return IS_FIXNUM(value) ? (double)valueToFixnum(value) : valueToNum(value);
}

typedef struct {
  int capacity;
  int count;
  Value* values;
} ValueArray;

bool valuesEqual(Value a, Value b);
void initValueArray(ValueArray* array);
void writeValueArray(ValueArray* array, Value value);
void freeValueArray(ValueArray* array);
void printValue(FILE* fd, Value value);
int valueToString(Value value, char** out);

#endif
