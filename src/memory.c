#include <stddef.h>
#include <stdlib.h>

#include "../include/compiler.h"
#include "../include/memory.h"
#include "../include/object.h"
#include "../include/vm.h"

#ifdef DEBUG_LOG_GC
#include "../include/debug.h"
#include <stdio.h>
#endif

#define GC_HEAP_GROWTH_FACTOR 2

void *reallocate(void *pointer, size_t oldSize, size_t newSize) {
  vm.bytesAllocated += newSize - oldSize;
  if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
    collectGarbage();
#endif
    if (vm.bytesAllocated > vm.nextGC) {
      collectGarbage();
    }
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
  printObject(OBJ_VAL(object));
  printf("\n");
#endif /* ifdef DEBUG_LOG_GC */

  switch (object->type) {
  case OBJ_STRING: {
    ObjString *string = (ObjString *)object;
    FREE_ARRAY(char, string->chars, string->length + 1);
    FREE(ObjString, object);
    break;
  }

  case OBJ_CLASS: {
    FREE(ObjClass, object);
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
  case OBJ_INSTANCE: {
    ObjInstance *instance = (ObjInstance *)object;
    freeTable(&instance->fields);
    FREE(ObjInstance, object);
    break;
  }
  }
}

void freeObjects() {
  Obj *object = vm.objects;
  while (object != NULL) {
    Obj *next = object->next;
    freeObject(object);
    object = next;
  }
  free(vm.grayStack);
}

void markObject(Obj *object) {
  if (object == NULL)
    return;
  if (object->isMarked)
    return;

#ifdef DEBUG_LOG_GC
  printf("%p mark ", (void *)object);
  printValue(OBJ_VAL(object));
  printf("\n");
#endif
  object->isMarked = true;

  if (vm.grayCapacity < vm.grayCount + 1) {
    vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
    vm.grayStack =
        (Obj **)realloc(vm.grayStack, sizeof(Obj *) * vm.grayCapacity);
    if (vm.grayStack == NULL)
      exit(1);
  }
}

void markValue(Value value) {
  if (IS_OBJ(value))
    markObject(AS_OBJ(value));
}

static void markRoots() {
  for (Value *slot = vm.stack; slot < vm.stackTop; slot++) {
    markValue(*slot);
  }

  for (int i = 0; i < vm.frameCount; i++) {
    markObject((Obj *)vm.frames[i].closure);
  }

  for (ObjUpvalue *upvalue = vm.openUpvalues; upvalue != NULL;
       upvalue = upvalue->next) {
    markObject((Obj *)upvalue);
  }

  markTable(&vm.globals);
  markCompilerRoots();
}

void markArray(ValueArray *value) {
  for (int i = 0; i < value->count; i++) {
    markValue(value->values[i]);
  }
}

static void blackenObject(Obj *object) {
#ifdef DEBUG_LOG_GC
  printf("%p blacken ", (void *)object);
  printValue(OBJ_VAL(object));
  printf("\n");
#endif
  switch (object->type) {
  case OBJ_FUNCTION: {
    ObjFunction *function = (ObjFunction *)object;
    markObject((Obj *)function->name);
    markArray(&function->chunk.constants);
    break;
  }
  case OBJ_CLOSURE: {
    ObjClosure *closure = (ObjClosure *)object;
    markObject((Obj *)closure->function);
    for (int i = 0; i < closure->upvalueCount; i++) {
      markObject((Obj *)closure->upvalues[i]);
    }
    break;
  }
  case OBJ_UPVALUE:
    markValue(((ObjUpvalue *)object)->closed);
    break;

  case OBJ_CLASS: {
    ObjClass *klass = (ObjClass *)object;
    markObject((Obj *)(klass->name));
    break;
  }

  case OBJ_INSTANCE: {
    ObjInstance *instance = (ObjInstance *)object;
    markObject((Obj *)(instance->className));
    markTable(&instance->fields);
    break;
  }

  case OBJ_STRING:
  case OBJ_NATIVE:
    break;
  }
}

static void traceReferences() {
  while (vm.grayCount > 0) {
    blackenObject(vm.grayStack[--vm.grayCount]);
  }
}

static void sweep() {
  Obj *object = vm.objects;
  Obj *previous = NULL;
  while (object != NULL) {
    if (object->isMarked) {
      object->isMarked = false;
      previous = object;
      object = object->next;
    } else {
      Obj *unreached = object;
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
#ifdef DEBUG_LOG_GC
  printf("-- gc begin\n");
#endif /* ifdef DEBUG_LOG_GC */

  size_t before = vm.bytesAllocated;

  markRoots();
  traceReferences();
  tableRemoveWhite(&vm.strings);
  sweep();

  vm.nextGC = vm.bytesAllocated * GC_HEAP_GROWTH_FACTOR;

#ifdef DEBUG_LOG_GC
  printf("-- gc end\n");
  printf("   collected %zu bytes (from %zu to %zu) next at %zu\n",
         before - vm.bytesAllocated, before, vm.bytesAllocated, vm.nextGC);
#endif /* ifdef DEBUG_LOG_GC */
}
