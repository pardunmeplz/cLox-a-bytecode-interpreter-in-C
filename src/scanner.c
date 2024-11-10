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

static char peek(){
  return *scanner.current;
}

static char peekNext(){
  if(isAtEnd()) return '\0';
  return scanner.current[1];
}

static bool match(char c){
  if(isAtEnd()) return false;
  if(peek() == c){
    scanner.current ++;
    return true;
  }
  return false;
}

// characters to ignore
static void skipWhiteSpace(){
  for(;;){
    char c = peek();

    switch(c){
      // newlines
      case '\n': scanner.line ++;
      // blankspaces
      case ' ':
      case '\r':
      case '\t':
        advance();
        break;
      // comments
      case '/':
        if(!match('/'))return;
        while(peekNext()!= '\n' && !isAtEnd()) advance();
        break;
      default: return;
    }
  } 
}

// number consumer and helpers
static bool isDigit(char c){
  return c <= '9' && c >= '0';
}

static Token number(){
  while(isDigit(peek())) advance();
  if(!isDigit(peekNext()) || !match('.')) return makeToken(TOKEN_NUMBER);

  while(isDigit(peek())) advance();
  return makeToken(TOKEN_NUMBER);
}

// string comsumer
static Token string(){
  while(!match('"') && !isAtEnd()){
    if (peek() == '\n') scanner.line ++;
    advance();
  }

  if(isAtEnd()) return errorToken("Unterminated string");

  return makeToken(TOKEN_STRING);
}

// keyword consumer and helpers
static bool isAlpha(char c){
  return (c >= 'a' && c <= 'z' ) ||
          (c >= 'A' && c <= 'Z') ||
          c == '_';
}

static bool isAlphaNumeric(char c){
  return isAlpha(c) || isDigit(c);
}

static Token keyword(){
  while(isAlphaNumeric(peek())) advance();

  return makeToken(TOKEN_IDENTIFIER);
}

// plop out a token on each call
Token scanToken(){
  skipWhiteSpace();
  scanner.start = scanner.current;

  if(isAtEnd()) return makeToken(TOKEN_EOF);

  char c = advance();

  if(isDigit(c)){
    return number();
  }

  if(isAlpha(c)){
    return keyword();
  }

  switch (c) {
    case '(': return makeToken(TOKEN_LEFT_PAREN);
    case ')': return makeToken(TOKEN_RIGHT_PAREN);
    case '{': return makeToken(TOKEN_LEFT_BRACE); case '}': return makeToken(TOKEN_RIGHT_BRACE);
    case ';': return makeToken(TOKEN_SEMICOLON);
    case '.': return makeToken(TOKEN_DOT);
    case ',': return makeToken(TOKEN_COMMA);
    case '+': return makeToken(TOKEN_PLUS);
    case '-': return makeToken(TOKEN_MINUS);
    case '*': return makeToken(TOKEN_STAR);
    case '/': return makeToken(TOKEN_SLASH);
    case '=':
      if (match('=')) return makeToken(TOKEN_EQUAL_EQUAL);
      return makeToken(TOKEN_EQUAL);
    case '!':
      if (match('=')) return makeToken(TOKEN_BANG_EQUAL);
      return makeToken(TOKEN_BANG);
    case '<':
      if (match('=')) return makeToken(TOKEN_LESS_EQUAL);
      return makeToken(TOKEN_LESS);
    case '>':
      if (match('=')) return makeToken(TOKEN_GREATER_EQUAL);
      return makeToken(TOKEN_GREATER);
    case '"':
      return string();
  }

  return errorToken("Unexpected character.");
}
