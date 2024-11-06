#include "../include/chunk.h"
#include "../include/debug.h"
#include "../include/vm.h"

int main(int argc, const char* argv[]){
  initVM();
  Chunk chunk;
  initChunk(&chunk);
  
  int constant = addConstant(&chunk, 1.2);
  writeChunk(&chunk, OP_CONSTANT, 2);
  writeChunk(&chunk, constant, 2);

  int constantb = addConstant(&chunk, 1.8);
  writeChunk(&chunk, OP_CONSTANT, 2);
  writeChunk(&chunk, constantb, 2);

  writeChunk(&chunk, OP_ADD, 2);

  writeChunk(&chunk, OP_RETURN, 2);

  disassembleChunk(&chunk, "test chunk");
  interpret(&chunk);
  freeVM();
  freeChunk(&chunk);
  return 0;
}
