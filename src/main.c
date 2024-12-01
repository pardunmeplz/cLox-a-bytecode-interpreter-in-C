#include "../include/vm.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

static void repl() {
  char line[1024];
  for (;;) {
    printf(">> ");

    if (!fgets(line, sizeof(line), stdin)) {
      printf("\n");
      break;
    }

    interpret(line);
  }
}

static char *readFile(const char *path) {
  FILE *file = fopen(path, "rb");

  if (file == NULL) {
    fprintf(stderr, "Could not open file %s\n", path);
    exit(74);
  }

  fseek(file, 0L, SEEK_END);
  size_t fileSize = ftell(file);
  rewind(file);

  char *buffer = (char *)malloc(fileSize + 1);
  if (buffer == NULL) {
    fprintf(stderr, "Not enough memory to read %s", path);
    exit(74);
  }

  size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
  buffer[bytesRead] = '\0';

  fclose(file);
  return buffer;
}

static void runFile(const char *path) {
  char *source = readFile(path);
  InterpretResult result = interpret(source);
  free(source);

  if (result == INTERPRET_COMPILE_ERROR)
    exit(65);
  if (result == INTERPRET_RUNTIME_ERROR)
    exit(70);
}

int main(int argc, const char *argv[]) {
  initVM();

  if (argc == 1) {
    repl();
  } else if (argc == 2) {
    runFile(argv[1]);
  } else {
    fprintf(stderr, "Usage: clox [path]\n");
  }

  // Chunk chunk;
  // initChunk(&chunk);
  //
  // int constant = addConstant(&chunk, 1.2);
  // writeChunk(&chunk, OP_CONSTANT, 2);
  // writeChunk(&chunk, constant, 2);

  // int constantb = addConstant(&chunk, 1.8);
  // writeChunk(&chunk, OP_CONSTANT, 2);
  // writeChunk(&chunk, constantb, 2);

  // writeChunk(&chunk, OP_ADD, 2);

  // int constantc = addConstant(&chunk, 1.8);
  // writeChunk(&chunk, OP_CONSTANT, 2);
  // writeChunk(&chunk, constantc, 2);

  // writeChunk(&chunk, OP_DIVIDE, 2);

  // writeChunk(&chunk, OP_RETURN, 2);

  // disassembleChunk(&chunk, "test chunk");
  // interpret(&chunk);
  freeVM();
  return 0;
}
