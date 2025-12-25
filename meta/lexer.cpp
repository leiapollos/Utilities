//
// Created by André Leite on 10/12/2024.
//

#define CHAR_TO_STR(ch) "'" #ch "'"

static const char* g_tokenKindNames[] = {
    #define X(name, str) str,
    #define X_CHAR(name, ch) CHAR_TO_STR(ch),
    NONCHAR_TOKENS
    CHAR_TOKENS
    KEYWORD_TOKENS
    #undef X
    #undef X_CHAR
};

const char* token_kind_name(TokenKind kind) {
    if (kind >= 0 && kind < TokenKind_COUNT) {
        return g_tokenKindNames[kind];
    }
    return "Unknown";
}

static B8 is_whitespace(U8 c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static B8 is_alpha(U8 c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static B8 is_digit(U8 c) {
    return c >= '0' && c <= '9';
}

static B8 is_alnum(U8 c) {
    return is_alpha(c) || is_digit(c);
}

static const TokenKind g_charToToken[256] = {
    #define X_CHAR(name, ch) [(U8)(ch)] = TokenKind_##name,
    CHAR_TOKENS
    #undef X_CHAR
};

static TokenKind keyword_or_identifier(StringU8 text) {
    #define X(name, str) if (str8_equal(text, str8(str))) { return TokenKind_##name; }
    KEYWORD_TOKENS
    #undef X
    return TokenKind_Identifier;
}

void lexer_init(Lexer* lexer, Arena* arena, StringU8 source, StringU8 filename) {
    MEMSET(lexer, 0, sizeof(Lexer));
    lexer->source = source;
    lexer->filename = filename;
    lexer->pos = 0;
    lexer->line = 1;
    lexer->column = 1;
    lexer->lineStart = 0;
    lexer->arena = arena;
    lexer->hasError = 0;
    lexer->hasPeekedToken = 0;
}

static U8 lexer_peek_char(Lexer* lexer) {
    if (lexer->pos >= lexer->source.size) {
        return 0;
    }
    return lexer->source.data[lexer->pos];
}

static U8 lexer_peek_char_n(Lexer* lexer, U64 n) {
    if (lexer->pos + n >= lexer->source.size) {
        return 0;
    }
    return lexer->source.data[lexer->pos + n];
}

static U8 lexer_advance_char(Lexer* lexer) {
    if (lexer->pos >= lexer->source.size) {
        return 0;
    }
    U8 c = lexer->source.data[lexer->pos++];
    if (c == '\n') {
        lexer->line++;
        lexer->column = 1;
        lexer->lineStart = (U32)lexer->pos;
    } else {
        lexer->column++;
    }
    return c;
}

static void lexer_skip_whitespace_and_comments(Lexer* lexer) {
    for (;;) {
        U8 c = lexer_peek_char(lexer);
        
        if (is_whitespace(c)) {
            lexer_advance_char(lexer);
            continue;
        }
        
        if (c == '/' && lexer_peek_char_n(lexer, 1) == '/') {
            lexer_advance_char(lexer);
            lexer_advance_char(lexer);
            while (lexer_peek_char(lexer) != '\n' && lexer_peek_char(lexer) != 0) {
                lexer_advance_char(lexer);
            }
            continue;
        }
        
        if (c == '/' && lexer_peek_char_n(lexer, 1) == '*') {
            lexer_advance_char(lexer);
            lexer_advance_char(lexer);
            while (!(lexer_peek_char(lexer) == '*' && lexer_peek_char_n(lexer, 1) == '/')) {
                if (lexer_peek_char(lexer) == 0) {
                    break;
                }
                lexer_advance_char(lexer);
            }
            if (lexer_peek_char(lexer) != 0) {
                lexer_advance_char(lexer);
                lexer_advance_char(lexer);
            }
            continue;
        }
        
        break;
    }
}

Token lexer_next_token(Lexer* lexer) {
    lexer_skip_whitespace_and_comments(lexer);
    
    Token token = {};
    token.line = lexer->line;
    token.column = lexer->column;
    
    U8 c = lexer_peek_char(lexer);
    
    if (c == 0) {
        token.kind = TokenKind_EOF;
        token.text = str8("EOF");
        return token;
    }
    
    if (is_alpha(c)) {
        U64 start = lexer->pos;
        while (is_alnum(lexer_peek_char(lexer))) {
            lexer_advance_char(lexer);
        }
        token.text = str8(lexer->source.data + start, lexer->pos - start);
        token.kind = keyword_or_identifier(token.text);
        return token;
    }
    
    if (is_digit(c)) {
        U64 start = lexer->pos;
        while (is_digit(lexer_peek_char(lexer))) {
            lexer_advance_char(lexer);
        }
        token.text = str8(lexer->source.data + start, lexer->pos - start);
        token.kind = TokenKind_Number;
        return token;
    }
    
    if (c == '"') {
        lexer_advance_char(lexer);
        U64 start = lexer->pos;
        while (lexer_peek_char(lexer) != '"' && lexer_peek_char(lexer) != 0) {
            if (lexer_peek_char(lexer) == '\\' && lexer_peek_char_n(lexer, 1) != 0) {
                lexer_advance_char(lexer);
            }
            lexer_advance_char(lexer);
        }
        token.text = str8(lexer->source.data + start, lexer->pos - start);
        token.kind = TokenKind_String;
        if (lexer_peek_char(lexer) == '"') {
            lexer_advance_char(lexer);
        }
        return token;
    }
    
    lexer_advance_char(lexer);
    token.text = str8(lexer->source.data + lexer->pos - 1, 1);
    token.kind = g_charToToken[c];
    
    return token;
}

Token lexer_peek_token(Lexer* lexer) {
    if (lexer->hasPeekedToken) {
        return lexer->peekedToken;
    }
    lexer->peekedToken = lexer_next_token(lexer);
    lexer->hasPeekedToken = 1;
    return lexer->peekedToken;
}

void lexer_skip_token(Lexer* lexer) {
    if (lexer->hasPeekedToken) {
        lexer->hasPeekedToken = 0;
    } else {
        lexer_next_token(lexer);
    }
}

B32 lexer_expect(Lexer* lexer, TokenKind kind, Token* outToken) {
    Token tok = lexer_peek_token(lexer);
    if (tok.kind != kind) {
        lexer->hasError = 1;
        lexer->errorMessage = str8_fmt(lexer->arena, "{}:{}:{}: Expected {}, got {}",
            lexer->filename, tok.line, tok.column,
            str8(token_kind_name(kind)), str8(token_kind_name(tok.kind)));
        return 0;
    }
    lexer_skip_token(lexer);
    if (outToken) {
        *outToken = tok;
    }
    return 1;
}

B32 lexer_accept(Lexer* lexer, TokenKind kind, Token* outToken) {
    Token tok = lexer_peek_token(lexer);
    if (tok.kind != kind) {
        return 0;
    }
    lexer_skip_token(lexer);
    if (outToken) {
        *outToken = tok;
    }
    return 1;
}
