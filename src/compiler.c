#include "../include/compiler.h"
#include "../include/debug.h"
#include "../include/memory.h"
#include "../include/object.h"
#include "../include/scanner.h"
#include "../include/value.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void (*ParseFn)(bool canAssign);

typedef struct {
  uint8_t index;
  bool isLocal;
} Upvalue;

typedef struct {
  Token previous;
  Token current;
  bool hasError;
  bool panicMode;
} Parser;

typedef enum {
  PREC_NONE,
  PREC_ASSIGNMENT, // =
  PREC_OR,         // or
  PREC_AND,        // and
  PREC_EQUALITY,   // == !=
  PREC_COMPARISON, // < > <= >=
  PREC_TERM,       // + -
  PREC_FACTOR,     // * /
  PREC_UNARY,      // ! -
  PREC_CALL,       // . ()
  PREC_PRIMARY
} Precedence;

typedef struct {
  ParseFn prefix;
  ParseFn infix;
  Precedence precedence;
} ParseRule;

typedef struct ClassCompiler {
  struct ClassCompiler *enclosing;
} ClassCompiler;

typedef struct {
  Token name;
  int depth;
  bool isCaptured;
} Local;

typedef enum {
  TYPE_FUNCTION,
  TYPE_METHOD,
  TYPE_INITIALIZER,
  TYPE_SCRIPT // represents the top level script, technically not a func
} FunctionType;

typedef struct Compiler {
  ObjFunction *function;
  FunctionType type;
  Local locals[UINT8_COUNT];
  int localCount;
  Upvalue upvalues[UINT8_COUNT];
  int scopeDepth;
  struct Compiler *enclosing;
} Compiler;

struct {
  Token previous;
  Token current;
  bool hasError;
  bool panicMode;
} parser;

static ParseRule *getRule(TokenType type);
static void parsePrecidence(Precedence precedence);
static void statement();
static void declaration();
static uint8_t parseVariable(char *errorMessage);
static void defineVariable(uint8_t global);
static uint8_t identifierConstant(Token *name);
static void declareVariable();
static void namedVariable(Token name, bool canAssign);

Compiler *current = NULL;
Chunk *compilingChunk;
ClassCompiler *currentClass = NULL;

static void initCompiler(Compiler *compiler, FunctionType type) {
  compiler->enclosing = current;
  compiler->function = NULL;
  compiler->type = type;
  compiler->localCount = 0;
  compiler->scopeDepth = 0;
  compiler->function = newFunction();
  current = compiler;
  if (type != TYPE_SCRIPT) {
    current->function->name =
        copyString(parser.previous.start, parser.previous.length);
  }

  Local *local = &current->locals[current->localCount++];
  local->depth = 0;
  local->name.start = "";
  local->name.length = 0;
  local->isCaptured = false;
  if (type != TYPE_FUNCTION) {
    local->name.start = "this";
    local->name.length = 4;
  } else {
    local->name.start = "";
    local->name.length = 0;
  }
}

static Chunk *currentChunk() { return &current->function->chunk; }

static void error(const char *message, Token *token) {
  // panic mode
  if (parser.panicMode)
    return;
  parser.panicMode = true;

  // print line number
  fprintf(stderr, "[line %d] Error", token->line);

  // print token lexme or human redable alternative
  if (token->type == TOKEN_EOF) {
    fprintf(stderr, " at end");
  } else if (token->type == TOKEN_ERROR) {
  } else {
    fprintf(stderr, " at '%.*s'", token->length, token->start);
  }

  // print message
  fprintf(stderr, ": %s\n", message);
  parser.hasError = true;
}

// --- parser logic
static void advance() {
  parser.previous = parser.current;
  for (;;) {
    parser.current = scanToken();
    if (parser.current.type != TOKEN_ERROR)
      break;

    error(parser.current.start, &parser.previous);
  }
}

static void consume(TokenType type, char *message) {
  if (parser.current.type == type) {
    advance();
    return;
  }

  error(message, &parser.current);
}

static bool check(TokenType type) { return parser.current.type == type; }

static bool match(TokenType type) {
  if (!check(type))
    return false;
  advance();
  return true;
}

// --- compiler logic
static void emitByte(uint8_t byte) {
  writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(uint8_t byte, uint8_t byte2) {
  // trusting the book saying this will be convinient later....
  emitByte(byte);
  emitByte(byte2);
}

static int emitJump(uint8_t instruction) {
  emitByte(instruction);
  emitByte(0xff);
  emitByte(0xff);
  return currentChunk()->count - 2;
}

static void emitLoop(uint16_t start) {
  emitByte(OP_LOOP);
  int offset = currentChunk()->count - start + 2;
  emitByte((offset >> 8) & 0xff);
  emitByte(offset & 0xff);
}

static void patchJump(uint8_t slot) {
  uint16_t gap = currentChunk()->count - slot - 2;

  if (gap > UINT16_MAX) {
    error("max jump limit exceeded", &parser.current);
  }

  currentChunk()->code[slot] = (gap >> 8) & 0xff;
  currentChunk()->code[slot + 1] = gap & 0xff;
}

static void emitReturn() {
  if (current->type == TYPE_INITIALIZER) {
    emitBytes(OP_GET_LOCAL, 0);
  } else {
    emitByte(OP_NIL);
  }
  emitByte(OP_RETURN);
}

static ObjFunction *endCompiler() {
  emitReturn();
  ObjFunction *function = current->function;

#ifdef DEBUG_PRINT_CODE
  if (!parser.hasError) {
    disassembleChunk(currentChunk(), function->name != NULL
                                         ? function->name->chars
                                         : "<script>");
  }
#endif /* ifdef DEBUG_PRINT_CODE */
  current = current->enclosing;
  return function;
}

static void synchronize() {
  parser.panicMode = false;

  while (parser.current.type != TOKEN_EOF) {
    if (parser.previous.type == TOKEN_SEMICOLON)
      return;
    switch (parser.current.type) {
    case TOKEN_CLASS:
    case TOKEN_FUN:
    case TOKEN_VAR:
    case TOKEN_FOR:
    case TOKEN_IF:
    case TOKEN_WHILE:
    case TOKEN_PRINT:
    case TOKEN_RETURN:
      return;
    default:;
    }
  }
}

static void expression() { parsePrecidence(PREC_ASSIGNMENT); }

static void varDeclaration() {
  uint8_t global = parseVariable("Expect variable name.");

  if (match(TOKEN_EQUAL)) {
    expression();
  } else {
    emitByte(OP_NIL);
  }
  consume((TOKEN_SEMICOLON), "Expect ';' after declaration.");

  defineVariable(global);
}

static void printStatement() {
  expression();
  consume((TOKEN_SEMICOLON), "Expect ';' after value.");
  emitByte(OP_PRINT);
}

static void expressionStatement() {
  expression();
  consume((TOKEN_SEMICOLON), "Expect ';' after value.");
  emitByte(OP_POP);
}

static void markInitialized() {
  if (current->scopeDepth == 0)
    return;
  current->locals[current->localCount - 1].depth = current->scopeDepth;
}

static void block() {
  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    declaration();
  }

  consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void beginScope() { current->scopeDepth++; }

static void endScope() {
  current->scopeDepth--;

  while (current->localCount > 0 &&
         current->locals[current->localCount - 1].depth > current->scopeDepth) {
    if (current->locals[current->localCount - 1].isCaptured) {
      emitByte(OP_CLOSE_UPVALUE);
    } else {
      emitByte(OP_POP);
    }
    current->localCount--;
  }
}

static uint8_t makeConstant(Value value) {
  uint8_t constant = addConstant(currentChunk(), value);
  if (constant > UINT8_MAX) {
    error("Too many constants in one chunk", &parser.current);
    return 0;
  }
  return constant;
}

static void function(FunctionType type) {
  Compiler compiler;
  initCompiler(&compiler, type);
  beginScope();

  consume(TOKEN_LEFT_PAREN, "Expect '(' after function name ");
  if (!check(TOKEN_RIGHT_PAREN)) {
    do {
      current->function->arity++;
      if (current->function->arity > 255) {
        error("Can't have more than 255 parameters.", &parser.current);
      }

      uint8_t constant = parseVariable("Expect parameter name");
      defineVariable(constant);

    } while (match(TOKEN_COMMA));
  }
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after function parameters ");

  consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
  block();

  ObjFunction *function = endCompiler();
  emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(function)));

  for (int i = 0; i < function->upvalueCount; i++) {
    emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
    emitByte(compiler.upvalues[i].index);
  }
}

static void funDeclaration() {
  uint8_t global = parseVariable("Expect function name");
  markInitialized();
  function(TYPE_FUNCTION);
  defineVariable(global);
}

static void method() {
  consume(TOKEN_IDENTIFIER, "Expect method name");
  uint8_t constant = identifierConstant(&parser.previous);

  FunctionType type = TYPE_METHOD;
  if (parser.previous.length == 4 &&
      memcmp(parser.previous.start, "init", 4) == 0) {
    type = TYPE_INITIALIZER;
  }
  function(type);
  emitBytes(OP_METHOD, constant);
}

static void classDeclaration() {
  consume(TOKEN_IDENTIFIER, "Expected class name after keyword");
  Token className = parser.previous;
  uint8_t nameConstant = identifierConstant(&parser.previous);
  declareVariable();

  emitBytes(OP_CLASS, nameConstant);
  defineVariable(nameConstant);

  ClassCompiler ClassCompiler;
  ClassCompiler.enclosing = currentClass;
  currentClass = &ClassCompiler;

  namedVariable(className, false);

  consume(TOKEN_LEFT_BRACE, "Expect '{' before class body");
  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    method();
  }
  consume(TOKEN_RIGHT_BRACE, "Expect '}' after class body");
  emitByte(OP_POP);
  currentClass = currentClass->enclosing;
}

static void declaration() {
  if (match(TOKEN_FUN)) {
    funDeclaration();
  } else if (match(TOKEN_VAR)) {
    varDeclaration();
  } else if (match(TOKEN_CLASS)) {
    classDeclaration();
  } else {
    statement();
  }
  if (parser.panicMode)
    synchronize();
}

static void whileStatement() {
  uint16_t loopStart = currentChunk()->count;
  consume(TOKEN_LEFT_PAREN, "Expect '(' after while");
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition");

  int exitJump = emitJump(OP_JUMP_IF_FALSE);
  emitByte(OP_POP);

  statement();
  emitLoop(loopStart);

  patchJump(exitJump);
  emitByte(OP_POP);
}

static void ifStatement() {
  consume(TOKEN_LEFT_PAREN, "Expect '(' after if");
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition");

  int thenJump = emitJump(OP_JUMP_IF_FALSE);
  emitByte(OP_POP);
  statement();

  int elseJump = emitJump(OP_JUMP);

  patchJump(thenJump);
  emitByte(OP_POP);

  if (match(TOKEN_ELSE)) {
    statement();
  }
  patchJump(elseJump);
}

static void forStatement() {

  beginScope();
  consume(TOKEN_LEFT_PAREN, "Expect '(' after for");

  // initizlizer
  if (match(TOKEN_SEMICOLON)) {  // no initializer
  } else if (match(TOKEN_VAR)) { // var declaration in initializer
    varDeclaration();
  } else { // since only expression statements and var declarations are allowed
    expressionStatement();
  }

  uint16_t loopStart = currentChunk()->count;

  // condition
  int exitJump = -1; // in case we don't have condition
  if (!match(TOKEN_SEMICOLON)) {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' at end of condition");
    exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
  }

  // skip the interation expression at start

  if (!match(TOKEN_RIGHT_PAREN)) {
    int skipJump = emitJump(OP_JUMP);
    uint16_t middleJump = currentChunk()->count;
    expression();
    emitByte(OP_POP);
    consume(TOKEN_RIGHT_PAREN, "Expect ')' at end of for statement");

    emitLoop(loopStart);
    loopStart = middleJump;
    patchJump(skipJump);
  }

  statement();
  emitLoop(loopStart);

  if (exitJump != -1) {
    patchJump(exitJump);
    emitByte(OP_POP);
  }
  endScope();
}
static void returnStatemnt() {
  if (match(TOKEN_SEMICOLON)) {
    emitReturn();
  } else {
    if (current->type == TYPE_INITIALIZER) {
      error("can't return a value from an initializer.", &parser.previous);
    }
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' at end of return statement.");
    emitByte(OP_RETURN);
  }
}

static void statement() {

  if (match(TOKEN_PRINT)) {
    printStatement();
  } else if (match(TOKEN_LEFT_BRACE)) {
    beginScope();
    block();
    endScope();
  } else if (match(TOKEN_IF)) {
    ifStatement();
  } else if (match(TOKEN_WHILE)) {
    whileStatement();
  } else if (match(TOKEN_RETURN)) {
    if (current->type == TYPE_SCRIPT) {
      error("Can' return from top-level code.", &parser.previous);
    }
    returnStatemnt();
  } else if (match(TOKEN_FOR)) {
    forStatement();
  } else {
    expressionStatement();
  }
}

static void grouping(bool canAssign) {
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expected ')' at end of expression");
}

static void emitConstant(Value value) {
  emitBytes(OP_CONSTANT, makeConstant(value));
}

static void number(bool canAssign) {
  double value = strtod(parser.previous.start, NULL);
  emitConstant(NUMBER_VAL(value));
}

static void string(bool canAssign) {
  // take string from previous token start to end without the " "
  emitConstant(OBJ_VAL(
      copyString(parser.previous.start + 1, parser.previous.length - 2)));
}

static bool identifiersEqual(Token *a, Token *b) {
  if (a->length != b->length)
    return false;
  return memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(Compiler *compiler, Token *name) {
  for (int i = compiler->localCount - 1; i >= 0; i--) {
    Local *local = &compiler->locals[i];
    if (identifiersEqual(name, &local->name)) {
      if (local->depth == -1) {
        error("Can't read local variable in its own initializer.",
              &parser.current);
      }
      return i;
    }
  }
  return -1;
}

static int addUpvalue(Compiler *compiler, uint8_t index, bool isLocal) {
  int upvalueCount = compiler->function->upvalueCount;

  for (int i = 0; i < upvalueCount; i++) {
    Upvalue *upvalue = &compiler->upvalues[i];
    if (upvalue->index == index && upvalue->isLocal == isLocal) {
      return i;
    }
  }
  if (upvalueCount == UINT8_COUNT) {
    error("Too many closure variables in function.", &parser.previous);
    return 0;
  }
  compiler->upvalues[upvalueCount].isLocal = isLocal;
  compiler->upvalues[upvalueCount].index = index;
  return compiler->function->upvalueCount++;
}

static int resolveUpvalue(Compiler *compiler, Token *name) {
  if (compiler->enclosing == NULL)
    return -1;

  // check for local value outside
  int local = resolveLocal((compiler->enclosing), name);
  if (local != -1) {
    compiler->enclosing->locals[local].isCaptured = true;
    return addUpvalue(compiler, (uint8_t)local, true);
  }

  // check upvalue outside
  int upvalue = resolveUpvalue(compiler->enclosing, name);
  if (upvalue != -1) {
    return addUpvalue(compiler, (uint8_t)upvalue, false);
  }
  return -1;
}

static void namedVariable(Token name, bool canAssign) {
  uint8_t getOp, setOp;
  int arg = resolveLocal(current, &name);
  if (arg != -1) {
    getOp = OP_GET_LOCAL;
    setOp = OP_SET_LOCAL;
  } else if ((arg = resolveUpvalue(current, &name)) != -1) {
    getOp = OP_GET_UPVALUE;
    setOp = OP_SET_UPVALUE;
  } else {
    arg = identifierConstant(&name);
    getOp = OP_GET_GLOBAL;
    setOp = OP_SET_GLOBAL;
  }

  if (canAssign && match(TOKEN_EQUAL)) {
    expression();
    emitBytes(setOp, (uint8_t)arg);
  } else {
    emitBytes(getOp, (uint8_t)arg);
  }
}

static void variable(bool canAssign) {

  namedVariable(parser.previous, canAssign);
}

static void _this(bool canAssign) {
  if (currentClass == NULL) {
    error("Can't use 'this' outside of a class.", &parser.previous);
    return;
  }
  variable(false);
}

static void literal(bool canAssign) {
  switch (parser.previous.type) {
  case TOKEN_FALSE:
    emitByte(OP_FALSE);
    break;
  case TOKEN_NIL:
    emitByte(OP_NIL);
    break;
  case TOKEN_TRUE:
    emitByte(OP_TRUE);
    break;

  default:
    return;
  }
}

static uint8_t argumentList() {
  uint8_t argCount = 0;
  if (!check(TOKEN_RIGHT_PAREN)) {
    do {
      expression();
      if (argCount == 255) {
        error("Can't have more than 255 aguments.", &parser.previous);
      }
      argCount++;

    } while (match(TOKEN_COMMA));
  }
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
  return argCount;
}

static void call(bool canAssign) {
  uint8_t argCount = argumentList();
  emitBytes(OP_CALL, argCount);
}

static void dot(bool canAssign) {
  consume(TOKEN_IDENTIFIER, "Expected proprty name after '.' ");
  uint8_t name = identifierConstant(&parser.previous);

  if (canAssign && match(TOKEN_EQUAL)) {
    expression();
    emitBytes(OP_SET_INST, name);
    return;
  } else if (match(TOKEN_LEFT_PAREN)) {
    uint8_t argCount = argumentList();
    emitBytes(OP_INVOKE, name);
    emitByte(argCount);
    return;
  }
  emitBytes(OP_GET_INST, name);
}

static void _and(bool canAssign) {
  int endJump = emitJump(OP_JUMP_IF_FALSE);
  emitByte(OP_POP);
  parsePrecidence(PREC_AND);
  patchJump(endJump);
}

static void _or(bool canAssign) {
  int dumJump = emitJump(OP_JUMP_IF_FALSE);
  int endJump = emitJump(OP_JUMP);
  patchJump(dumJump);
  emitByte(OP_POP);

  parsePrecidence(PREC_OR);
  patchJump(endJump);
}

static void unary(bool canAssign) {
  TokenType operatorType = parser.previous.type;

  parsePrecidence(PREC_UNARY);
  switch (operatorType) {
  case TOKEN_MINUS:
    emitByte(OP_NEGATE);
    break;
  case TOKEN_BANG:
    emitByte(OP_NOT);
  default:
    return;
  }
}

static void binary(bool canAssign) {
  TokenType operatorType = parser.previous.type;
  ParseRule *rule = getRule(operatorType);
  parsePrecidence(((Precedence)(rule->precedence + 1)));

  switch (operatorType) {
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
  case TOKEN_BANG_EQUAL:
    emitByte((OP_EQUAL));
    emitByte((OP_NOT));
    break;
  case TOKEN_EQUAL_EQUAL:
    emitByte((OP_EQUAL));
    break;
  case TOKEN_GREATER:
    emitByte((OP_GREATER));
    break;
  case TOKEN_GREATER_EQUAL:
    emitByte((OP_LESS));
    emitByte((OP_NOT));
    break;
  case TOKEN_LESS:
    emitByte((OP_LESS));
    break;
  case TOKEN_LESS_EQUAL:
    emitByte((OP_GREATER));
    emitByte((OP_NOT));
    break;
  default:
    return;
  }
}

// da table
ParseRule rules[] = {
    [TOKEN_LEFT_PAREN] = {grouping, call, PREC_CALL},
    [TOKEN_RIGHT_PAREN] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_COMMA] = {NULL, NULL, PREC_NONE},
    [TOKEN_DOT] = {NULL, dot, PREC_CALL},
    [TOKEN_MINUS] = {unary, binary, PREC_TERM},
    [TOKEN_PLUS] = {NULL, binary, PREC_TERM},
    [TOKEN_SEMICOLON] = {NULL, NULL, PREC_NONE},
    [TOKEN_SLASH] = {NULL, binary, PREC_FACTOR},
    [TOKEN_STAR] = {NULL, binary, PREC_FACTOR},
    [TOKEN_BANG] = {unary, NULL, PREC_NONE},
    [TOKEN_BANG_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_EQUAL] = {NULL, NULL, PREC_NONE},
    [TOKEN_EQUAL_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_GREATER] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_IDENTIFIER] = {variable, NULL, PREC_NONE},
    [TOKEN_STRING] = {string, NULL, PREC_NONE},
    [TOKEN_NUMBER] = {number, NULL, PREC_NONE},
    [TOKEN_AND] = {NULL, _and, PREC_AND},
    [TOKEN_CLASS] = {NULL, NULL, PREC_NONE},
    [TOKEN_ELSE] = {NULL, NULL, PREC_NONE},
    [TOKEN_FALSE] = {literal, NULL, PREC_NONE},
    [TOKEN_FOR] = {NULL, NULL, PREC_NONE},
    [TOKEN_FUN] = {NULL, NULL, PREC_NONE},
    [TOKEN_IF] = {NULL, NULL, PREC_NONE},
    [TOKEN_NIL] = {literal, NULL, PREC_NONE},
    [TOKEN_OR] = {NULL, _or, PREC_OR},
    [TOKEN_PRINT] = {NULL, NULL, PREC_NONE},
    [TOKEN_RETURN] = {NULL, NULL, PREC_NONE},
    [TOKEN_SUPER] = {NULL, NULL, PREC_NONE},
    [TOKEN_THIS] = {_this, NULL, PREC_NONE},
    [TOKEN_TRUE] = {literal, NULL, PREC_NONE},
    [TOKEN_VAR] = {NULL, NULL, PREC_NONE},
    [TOKEN_WHILE] = {NULL, NULL, PREC_NONE},
    [TOKEN_ERROR] = {NULL, NULL, PREC_NONE},
    [TOKEN_EOF] = {NULL, NULL, PREC_NONE},
};

static ParseRule *getRule(TokenType type) { return &rules[type]; }

static void parsePrecidence(Precedence precedence) {
  advance();
  ParseFn prefixRule = getRule(parser.previous.type)->prefix;
  if (prefixRule == NULL) {
    error("Expect expression", &parser.previous);
    return;
  }
  bool canAssign = precedence <= PREC_ASSIGNMENT;
  prefixRule(canAssign);

  while (precedence <= getRule(parser.current.type)->precedence) {
    advance();
    ParseFn infixRule = getRule(parser.previous.type)->infix;
    infixRule(canAssign);
  }

  if (canAssign && match(TOKEN_EQUAL)) {
    error("Invalid assignment target.", &parser.current);
  }
}

static uint8_t identifierConstant(Token *name) {
  return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

static void addLocal(Token name) {
  if (current->localCount == UINT8_COUNT) {
    error("TOO many local variables in function.", &parser.current);
    return;
  }
  Local *local = &current->locals[current->localCount++];
  local->name = name;
  local->depth = -1;
  local->isCaptured = false;
}

static void declareVariable() {
  if (current->scopeDepth == 0)
    return;

  Token *name = &parser.previous;
  for (int i = current->localCount - 1; i >= 0; i--) {
    Local *local = &current->locals[i];
    if (local->depth != -1 && local->depth < current->scopeDepth) {
      break;
    }

    if (identifiersEqual(name, &local->name)) {
      error("Redeclaration of variable in the same scope is not allowed",
            &parser.previous);
    }
  }
  addLocal(*name);
}

static uint8_t parseVariable(char *errorMessage) {
  consume(TOKEN_IDENTIFIER, errorMessage);

  declareVariable();
  if (current->scopeDepth > 0) {
    return 0;
  }

  return identifierConstant(&parser.previous);
}

static void defineVariable(uint8_t global) {
  if (current->scopeDepth > 0) {
    markInitialized();
    return;
  }
  emitBytes(OP_DEFINE_GLOBAL, global);
}

ObjFunction *compile(const char *source) {
  initScanner(source);
  Compiler compiler;
  initCompiler(&compiler, TYPE_SCRIPT);

  parser.hasError = false;
  parser.panicMode = false;

  advance();
  while (!match(TOKEN_EOF)) {
    declaration();
  }

  ObjFunction *function = endCompiler();

  return parser.hasError ? NULL : function;
}

void markCompilerRoots() {

  Compiler *compiler = current;
  while (compiler != NULL) {
    markObject((Obj *)compiler->function);
    compiler = compiler->enclosing;
  }
}
