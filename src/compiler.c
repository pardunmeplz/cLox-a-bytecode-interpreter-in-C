#include "../include/compiler.h"
#include "../include/scanner.h"
#include <stdio.h>

typedef struct{
  Token previous;
  Token current;
  bool hasError;
  bool panicMode;
}Parser;

Parser parser;

static void error(const char* message, Token* token){
  // panic mode
  if(parser.panicMode) return;
  parser.panicMode = true;

  // print line number
  fprintf(stderr, "[line %d] Error", token->line);
  
  // print token lexme or human redable alternative
  if(token->type == TOKEN_EOF){
    fprintf(stderr, " at end");
  } else if(token->type == TOKEN_ERROR){
  } else {
    fprintf(stderr, " at '%.*s'", token->length, token->start); 
  }

  // print message
  fprintf(stderr, ": %s\n", message);
  parser.hasError = true;
}

static void advance(){
  parser.previous = parser.current;

  for(;;){
    parser.current = scanToken();
    if(parser.current.type != TOKEN_ERROR) break;

    error(parser.current.start, &parser.previous);
  }
}

static void consume(TokenType type, char* message){
  if(parser.current.type == type){
    advance();
    return;
  }

  error(message, &parser.current);
}

static void expression(){
  //todo: implement parsing expressions
}

bool compile(const char* source, Chunk* chunk){
  initScanner(source);
  parser.hasError = false;
  parser.panicMode = false;

  advance();
  expression();
  consume(TOKEN_EOF, "Expected end of file");

  return !parser.hasError;
}
