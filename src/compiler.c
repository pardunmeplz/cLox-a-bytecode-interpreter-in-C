#include "../include/compiler.h"
#include "../include/scanner.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef void (*ParseFn)();

typedef struct{
  Token previous;
  Token current;
  bool hasError;
  bool panicMode;
} Parser;

typedef enum {
  PREC_NONE,
  PREC_ASSIGNMENT,  // =
  PREC_OR,          // or
  PREC_AND,         // and
  PREC_EQUALITY,    // == !=
  PREC_COMPARISON,  // < > <= >=
  PREC_TERM,        // + -
  PREC_FACTOR,      // * /
  PREC_UNARY,       // ! -
  PREC_CALL,        // . ()
  PREC_PRIMARY
} Precedence;

typedef struct{
  ParseFn prefix;
  ParseFn infix;
  Precedence precedence;
}ParseRule ;


struct{
  Token previous;
  Token current;
  bool hasError;
  bool panicMode;
}parser;

static ParseRule* getRule(TokenType type);
static void parsePrecidence(Precedence precedence);

Chunk* compilingChunk;

static Chunk* currentChunk(){
  return compilingChunk;
}

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

// --- parser logic
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


// --- compiler logic
static void emitByte(uint8_t byte){
  writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(uint8_t byte, uint8_t byte2){
  // trusting the book saying this will be convinient later....
  emitByte(byte);
  emitByte(byte2);
}

static void endCompiler(){
  emitByte(OP_RETURN);
}

static void expression(){
  //todo: implement parsing expressions

  parsePrecidence(PREC_ASSIGNMENT);
}

static void grouping(){
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expected ')' at end of expression");
}

static uint8_t makeConstant(Value value){
  uint8_t constant = addConstant(currentChunk(), value); 
  if(constant > UINT8_MAX){
    error("Too many constants in one chunk", &parser.current);
    return 0;
  }
  return constant;
}

static void emitConstant(Value value){
  emitBytes(OP_CONSTANT, makeConstant(value));
}

static void number(){
  double value = strtod(parser.previous.start, NULL);
  emitConstant(value);
}

static void unary(){
  TokenType operatorType = parser.previous.type;

  parsePrecidence(PREC_UNARY);
  switch (operatorType) {
    case TOKEN_MINUS:
      emitByte(OP_NEGATE);
      break;
    default: return;
  }
}

static void binary(){
  TokenType operatorType = parser.previous.type;
  ParseRule* rule = getRule(operatorType);
  parsePrecidence(((Precedence)(rule->precedence +1)));

  switch (operatorType){
  case TOKEN_PLUS: 
      emitByte((OP_ADD));
      break;
  case TOKEN_MINUS:
      emitByte((OP_SUBTRACT));
      break;
  case TOKEN_STAR:
      emitByte((OP_MULTIPLY));
      break;
  case TOKEN_SLASH:
      emitByte((OP_DIVIDE));
      break;
  default:
    return;
  }
}

// da table
ParseRule rules[] = {
  [TOKEN_LEFT_PAREN]    = {grouping, NULL,   PREC_NONE},
  [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,   PREC_NONE},
  [TOKEN_LEFT_BRACE]    = {NULL,     NULL,   PREC_NONE}, 
  [TOKEN_RIGHT_BRACE]   = {NULL,     NULL,   PREC_NONE},
  [TOKEN_COMMA]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_DOT]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_MINUS]         = {unary,    binary, PREC_TERM},
  [TOKEN_PLUS]          = {NULL,     binary, PREC_TERM},
  [TOKEN_SEMICOLON]     = {NULL,     NULL,   PREC_NONE},
  [TOKEN_SLASH]         = {NULL,     binary, PREC_FACTOR},
  [TOKEN_STAR]          = {NULL,     binary, PREC_FACTOR},
  [TOKEN_BANG]          = {NULL,     NULL,   PREC_NONE},
  [TOKEN_BANG_EQUAL]    = {NULL,     NULL,   PREC_NONE},
  [TOKEN_EQUAL]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_EQUAL_EQUAL]   = {NULL,     NULL,   PREC_NONE},
  [TOKEN_GREATER]       = {NULL,     NULL,   PREC_NONE},
  [TOKEN_GREATER_EQUAL] = {NULL,     NULL,   PREC_NONE},
  [TOKEN_LESS]          = {NULL,     NULL,   PREC_NONE},
  [TOKEN_LESS_EQUAL]    = {NULL,     NULL,   PREC_NONE},
  [TOKEN_IDENTIFIER]    = {NULL,     NULL,   PREC_NONE},
  [TOKEN_STRING]        = {NULL,     NULL,   PREC_NONE},
  [TOKEN_NUMBER]        = {number,   NULL,   PREC_NONE},
  [TOKEN_AND]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_CLASS]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE},
  [TOKEN_FALSE]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_FOR]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_FUN]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_IF]            = {NULL,     NULL,   PREC_NONE},
  [TOKEN_NIL]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_OR]            = {NULL,     NULL,   PREC_NONE},
  [TOKEN_PRINT]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_RETURN]        = {NULL,     NULL,   PREC_NONE},
  [TOKEN_SUPER]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_THIS]          = {NULL,     NULL,   PREC_NONE},
  [TOKEN_TRUE]          = {NULL,     NULL,   PREC_NONE},
  [TOKEN_VAR]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_WHILE]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_ERROR]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_EOF]           = {NULL,     NULL,   PREC_NONE},
};

static ParseRule* getRule(TokenType type){
  return &rules[type];
}

static void parsePrecidence(Precedence precedence){
  advance();
  ParseFn prefixRule = getRule(parser.previous.type)->prefix;
  if(prefixRule == NULL){
    error("Expect expression", &parser.previous);
    return;
  }
  prefixRule();

  while (precedence <= getRule(parser.current.type)->precedence){
    advance();
    ParseFn infixRule = getRule(parser.previous.type)->infix;
    infixRule();
  }
}

bool compile(const char* source, Chunk* chunk){
  initScanner(source);
  compilingChunk = chunk;

  parser.hasError = false;
  parser.panicMode = false;

  advance();
  expression();
  consume(TOKEN_EOF, "Expected end of file");
  endCompiler();

  return !parser.hasError;
}
