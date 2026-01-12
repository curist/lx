#include <stdlib.h>

#include "memory.h"
#include "vm.h"

#ifdef DEBUG_LOG_GC
#include <stdio.h>
#include "debug.h"
#endif

// Disable per-object logging under stress GC (summary only)
#if defined(DEBUG_LOG_GC) && defined(DEBUG_STRESS_GC)
  #define DEBUG_LOG_GC_VERBOSE 0
#else
  #define DEBUG_LOG_GC_VERBOSE 1
#endif

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
  vm.bytesAllocated += newSize - oldSize;
  if (newSize > oldSize) {
    // Never start a GC while one is already running.
    if (!vm.gcRunning) {
#ifdef DEBUG_STRESS_GC
      collectGarbage();
#endif
      if (vm.bytesAllocated > vm.nextGC) {
        collectGarbage();
      }
    }
  }

  if (newSize == 0) {
    free(pointer);
    return NULL;
  }

  void* result = realloc(pointer, newSize);
  if (result == NULL) exit(1);
  return result;
}

void markObject(Obj* object) {
  if (object == NULL) return;
  if (object->isMarked) return;

#if defined(DEBUG_LOG_GC) && DEBUG_LOG_GC_VERBOSE
  printf("%p mark ", (void*)object);
  printValue(stdout, OBJ_VAL(object));
  printf("\n");
#endif

  object->isMarked = true;

  if (vm.grayCapacity < vm.grayCount + 1) {
    int oldCapacity = vm.grayCapacity;
    vm.grayCapacity = GROW_CAPACITY(oldCapacity);

    vm.grayStack = (Obj**)reallocate(vm.grayStack,
                                     sizeof(Obj*) * oldCapacity,
                                     sizeof(Obj*) * vm.grayCapacity);
    if (vm.grayStack == NULL) exit(1);
  }

  vm.grayStack[vm.grayCount++] = object;
}

void markValue(Value value) {
  if (IS_OBJ(value)) markObject(AS_OBJ(value));
}

static void markArray(ValueArray* array) {
  for (int i = 0; i < array->count; i++) {
    markValue(array->values[i]);
  }
}

static void blackenObject(Obj* object) {
#if defined(DEBUG_LOG_GC) && DEBUG_LOG_GC_VERBOSE
  printf("%p blacken ", (void*)object);
  printValue(stdout, OBJ_VAL(object));
  printf("\n");
#endif

  switch (object->type) {
    case OBJ_CLOSURE: {
      ObjClosure* closure = (ObjClosure*)object;
      markObject((Obj*)closure->function);
      for (int i = 0; i < closure->upvalueCount; i++) {
        markObject((Obj*)closure->upvalues[i]);
      }
      break;
    }
    case OBJ_FUNCTION: {
      ObjFunction* function = (ObjFunction*)object;
      markObject((Obj*)function->name);
      markObject((Obj*)function->filename);
      markArray(&function->chunk.constants);
      break;
    }
    case OBJ_UPVALUE:
      markValue(((ObjUpvalue*)object)->closed);
      break;
    case OBJ_NATIVE:
    case OBJ_STRING:
      break;
    case OBJ_HASHMAP:
      markTable(&((ObjHashmap*)object)->table);
      break;
    case OBJ_ENUM: {
      ObjEnum* e = (ObjEnum*)object;
      markTable(&e->forward);
      markTable(&e->reverse);
      markArray(&e->names);
      break;
    }
    case OBJ_ARRAY: {
      markArray(&((ObjArray*)object)->array);
      break;
    }
    case OBJ_FIBER: {
      ObjFiber* fiber = (ObjFiber*)object;

      // Mark stack values (NULL guard to avoid undefined pointer comparison)
      if (fiber->stack != NULL && fiber->stackTop != NULL) {
        for (Value* slot = fiber->stack; slot < fiber->stackTop; slot++) {
          markValue(*slot);
        }
      }

      // Mark call frames (closures)
      if (fiber->frames != NULL) {
        CallFrame* frames = (CallFrame*)fiber->frames;
        for (int i = 0; i < fiber->frameCount; i++) {
          markObject((Obj*)frames[i].closure);
        }
      }

      // Mark open upvalues
      for (ObjUpvalue* upvalue = fiber->openUpvalues; upvalue != NULL; upvalue = upvalue->next) {
        markObject((Obj*)upvalue);
      }

      // Mark error state
      markValue(fiber->lastError);

      // Mark caller fiber
      markObject((Obj*)fiber->caller);

      break;
    }
  }
}

static void freeObject(Obj* object) {
#if defined(DEBUG_LOG_GC) && DEBUG_LOG_GC_VERBOSE
  printf("%p free type %d\n", (void*)object, object->type);
#endif

  switch (object->type) {
    case OBJ_CLOSURE: {
      ObjClosure* closure = (ObjClosure*)object;
      FREE_ARRAY(ObjUpvalue*, closure->upvalues, closure->upvalueCount);
      FREE(ObjClosure, object);
      break;
    }
    case OBJ_FUNCTION: {
      ObjFunction* function = (ObjFunction*)object;
      freeChunk(&function->chunk);
      FREE(ObjFunction, object);
      break;
    }
    case OBJ_NATIVE:
      FREE(ObjNative, object);
      break;
    case OBJ_STRING: {
      ObjString* string = (ObjString*)object;
      FREE_ARRAY(char, string->chars, string->length + 1);
      FREE(ObjString, object);
      break;
    }
    case OBJ_UPVALUE:
      FREE(ObjUpvalue, object);
      break;
    case OBJ_HASHMAP: {
      ObjHashmap* hm = (ObjHashmap*)object;
      freeTable(&hm->table);
      FREE(ObjHashmap, object);
      break;
    }
    case OBJ_ENUM: {
      ObjEnum* e = (ObjEnum*)object;
      freeTable(&e->forward);
      freeTable(&e->reverse);
      freeValueArray(&e->names);
      FREE(ObjEnum, object);
      break;
    }
    case OBJ_ARRAY: {
      ObjArray* a = (ObjArray*)object;
      freeValueArray(&a->array);
      FREE(ObjArray, object);
      break;
    }
    case OBJ_FIBER: {
      ObjFiber* fiber = (ObjFiber*)object;
      FREE_ARRAY(Value, fiber->stack, fiber->stackCapacity);
      FREE_ARRAY(CallFrame, fiber->frames, fiber->frameCapacity);
      FREE(ObjFiber, object);
      break;
    }
  }
}

void freeObjects() {
  Obj* object = vm.objects;
  while (object != NULL) {
    Obj* next = object->next;
    freeObject(object);
    object = next;
  }

  free(vm.grayStack);
}

static void markRoots() {
  for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
    markValue(*slot);
  }

  for (int i = 0; i < vm.frameCount; i++) {
    markObject((Obj*)vm.frames[i].closure);
  }

  for (ObjUpvalue* upvalue = vm.openUpvalues; upvalue != NULL; upvalue = upvalue->next) {
    markObject((Obj*)upvalue);
  }

  markTable(&vm.globals);
  markValue(vm.lastResult);
  markValue(vm.lastError);
}

static void traceReferences() {
  while (vm.grayCount > 0) {
    Obj* object = vm.grayStack[--vm.grayCount];
    blackenObject(object);
  }
}

static void sweep() {
  Obj* previous = NULL;
  Obj* object = vm.objects;
  while (object != NULL) {
    if (object->isMarked) {
      object->isMarked = false;
      previous = object;
      object = object->next;
    } else {
      Obj* unreached = object;
      object = object->next;
      if (previous != NULL) {
        previous->next = object;
      } else {
        vm.objects = object;
      }

      freeObject(unreached);
    }
  }
}

void collectGarbage() {
#ifdef DEBUG
  if (vm.gcRunning) {
    fprintf(stderr, "BUG: GC reentered.\n");
    abort();
  }
#endif
  vm.gcRunning = true;

#ifdef DEBUG_LOG_GC
  size_t before = vm.bytesAllocated;
#endif

  markRoots();
  traceReferences();
  tableRemoveWhite(&vm.strings);
  sweep();

  vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;

  vm.gcRunning = false;  // clear BEFORE doing formatted IO

#ifdef DEBUG_LOG_GC
  fprintf(stderr, "-- gc begin\n");
  fprintf(stderr, "-- gc end\n");
  if (vm.bytesAllocated <= before) {
    fprintf(stderr, "   collected %zu bytes (from %zu to %zu) next at %zu\n",
           before - vm.bytesAllocated, before, vm.bytesAllocated, vm.nextGC);
  } else {
    fprintf(stderr, "   gc grew by %zu bytes (from %zu to %zu) next at %zu\n",
           vm.bytesAllocated - before, before, vm.bytesAllocated, vm.nextGC);
  }
#endif
}
