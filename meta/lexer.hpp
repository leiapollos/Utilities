//
// Created by André Leite on 10/12/2024.
//

#pragma once

// X(Name, DisplayString) - tokens without a single-char representation
// X_CHAR(Name, Char) - single-char tokens (display string derived as "'c'")

#define NONCHAR_TOKENS \
    X(Invalid,      "Invalid")   \
    X(EOF,          "EOF")       \
    X(Identifier,   "Identifier") \
    X(Number,       "Number")    \
    X(String,       "String")

#define CHAR_TOKENS \
    X_CHAR(OpenBrace,    '{') \
    X_CHAR(CloseBrace,   '}') \
    X_CHAR(OpenParen,    '(') \
    X_CHAR(CloseParen,   ')') \
    X_CHAR(OpenBracket,  '[') \
    X_CHAR(CloseBracket, ']') \
    X_CHAR(Comma,        ',') \
    X_CHAR(Colon,        ':') \
    X_CHAR(Semicolon,    ';') \
    X_CHAR(At,           '@') \
    X_CHAR(Equals,       '=') \
    X_CHAR(Star,         '*') \
    X_CHAR(Ampersand,    '&')

#define KEYWORD_TOKENS \
    X(KW_SumType,   "sum_type") \
    X(KW_Common,    "common")   \
    X(KW_NoMatch,   "no_match")

enum TokenKind {
    #define X(name, str) TokenKind_##name,
    #define X_CHAR(name, ch) TokenKind_##name,
    NONCHAR_TOKENS
    CHAR_TOKENS
    KEYWORD_TOKENS
    #undef X
    #undef X_CHAR
    TokenKind_COUNT
};

struct Token {
    TokenKind kind;
    StringU8 text;
    U32 line;
    U32 column;
};

struct Lexer {
    StringU8 source;
    StringU8 filename;
    U64 pos;
    U32 line;
    U32 column;
    U32 lineStart;
    
    Arena* arena;
    
    Token peekedToken;
    B32 hasPeekedToken;
    
    B32 hasError;
    StringU8 errorMessage;
};

void lexer_init(Lexer* lexer, Arena* arena, StringU8 source, StringU8 filename);
Token lexer_next_token(Lexer* lexer);
Token lexer_peek_token(Lexer* lexer);
void lexer_skip_token(Lexer* lexer);

B32 lexer_expect(Lexer* lexer, TokenKind kind, Token* outToken);
B32 lexer_accept(Lexer* lexer, TokenKind kind, Token* outToken);

const char* token_kind_name(TokenKind kind);
