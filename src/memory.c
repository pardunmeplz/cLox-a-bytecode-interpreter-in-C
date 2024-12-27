#include <stdlib.h>

#include "../include/memory.h"
#include "../include/object.h"
#include "../include/vm.h"

#ifdef DEBUG_LOG_GC
#include "../include/debug.h"
#include <stdio.h>
#endif

void *reallocate(void *pointer, size_t oldSize, size_t newSize) {
  if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
    collectGarbage();
#endif
    collectGarbage();
  }

  if (newSize == 0) {
    free(pointer);
    return NULL;
  }
  void *result = realloc(pointer, newSize);
  if (result == NULL)
    exit(1);
  return result;
}

void freeObject(Obj *object) {
#ifdef DEBUG_LOG_GC
  printf("%p free type %d\n", (void *)object, object->type);
#endif /* ifdef DEBUG_LOG_GC */

  switch (object->type) {
  case OBJ_STRING: {
    ObjString *string = (ObjString *)object;
    FREE_ARRAY(char, string->chars, string->length + 1);
    FREE(ObjString, object);
    break;
  }

  case OBJ_FUNCTION: {
    ObjFunction *func = (ObjFunction *)object;
    freeChunk(&func->chunk);
    FREE(ObjFunction, object);
    break;
  }

  case OBJ_NATIVE:
    FREE(ObjNative, object);
    break;

  case OBJ_CLOSURE: {
    ObjClosure *closure = (ObjClosure *)object;
    FREE_ARRAY(ObjUpvalue *, closure->upvalues, closure->upvalueCount);
    FREE(ObjClosure, object);
    break;
  }

  case OBJ_UPVALUE:
    FREE(ObjUpvalue, object);
    break;
  }
}

void freeObjects() {
  Obj *object = vm.objects;
  while (object != NULL) {
    Obj *next = object->next;
    freeObject(object);
    object = next;
  }
}

void collectGarbage() {
#ifdef DEBUG_LOG_GC
  printf("-- gc begin\n");
#endif /* ifdef DEBUG_LOG_GC */

#ifdef DEBUG_LOG_GC
  printf("-- gc end\n");
#endif /* ifdef DEBUG_LOG_GC */
}
