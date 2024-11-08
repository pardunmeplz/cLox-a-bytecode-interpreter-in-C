#include "../include/scanner.h"
#include <stdbool.h>
#include <string.h>

typedef struct{
  const char* source;
  const char* current;
  const char* start;
  int line;
} Scanner;

Scanner scanner;

void initScanner(const char *source){
  scanner.source = source;
  scanner.current = source; 
  scanner.line = 1;
}

static bool isAtEnd(){
   return *scanner.current == '\0';
}

static Token makeToken(TokenType type){
  Token token;
  token.type = type;
  token.start = scanner.start;
  token.line = scanner.line;
  token.length = (int) (scanner.current - scanner.start);
  return token;
}

static Token errorToken(const char* message){
 Token token;
  token.type = TOKEN_ERROR;
  token.start = message;
  token.length = (int) strlen(message);
  token.line = scanner.line;
  return token;
}

static char advance(){
  return *scanner.current++;
}

Token scanToken(){
  scanner.start = scanner.current;

  if(isAtEnd()) return makeToken(TOKEN_EOF);

  char c = advance();

  switch (c) {
    case '(': return makeToken(TOKEN_LEFT_PAREN);
    case ')': return makeToken(TOKEN_RIGHT_PAREN);
    case '{': return makeToken(TOKEN_LEFT_BRACE);
    case '}': return makeToken(TOKEN_RIGHT_BRACE);
    case ';': return makeToken(TOKEN_SEMICOLON);
    case '.': return makeToken(TOKEN_DOT);
    case ',': return makeToken(TOKEN_COMMA);
    case '+': return makeToken(TOKEN_PLUS);
    case '-': return makeToken(TOKEN_MINUS);
    case '*': return makeToken(TOKEN_STAR);
    case '/': return makeToken(TOKEN_SLASH);
  }

  return errorToken("Unexpected character.");
}
