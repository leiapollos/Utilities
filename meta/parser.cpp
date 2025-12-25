//
// Created by André Leite on 10/12/2024.
//

void parser_init(Parser* parser, Arena* arena, Lexer* lexer) {
    MEMSET(parser, 0, sizeof(Parser));
    parser->lexer = lexer;
    parser->arena = arena;
    parser->hasError = 0;
}

static void parser_error_(Parser* parser, StringU8 message) {
    if (parser->hasError) {
        return;
    }
    parser->hasError = 1;
    
    Token tok = lexer_peek_token(parser->lexer);
    parser->errorMessage = str8_fmt(parser->arena, "{}:{}:{}: {}",
        parser->lexer->filename, tok.line, tok.column, message);
}


static ASTField* parse_field(Parser* parser) {
    Token nameTok;
    if (!lexer_expect(parser->lexer, TokenKind_Identifier, &nameTok)) {
        parser_error_(parser, str8("Expected field name"));
        return nullptr;
    }
    
    if (!lexer_expect(parser->lexer, TokenKind_Colon, nullptr)) {
        parser_error_(parser, str8("Expected ':' after field name"));
        return nullptr;
    }
    
    ASTField* field = ARENA_PUSH_STRUCT(parser->arena, ASTField);
    MEMSET(field, 0, sizeof(ASTField));
    field->name = nameTok.text;
    
    if (lexer_accept(parser->lexer, TokenKind_Star, nullptr)) {
        field->isPointer = 1;
    }
    
    Token typeTok;
    if (!lexer_expect(parser->lexer, TokenKind_Identifier, &typeTok)) {
        parser_error_(parser, str8("Expected type name"));
        return nullptr;
    }
    field->typeName = typeTok.text;
    
    if (lexer_accept(parser->lexer, TokenKind_Ampersand, nullptr)) {
        field->isReference = 1;
    }
    
    return field;
}

static ASTField* parse_field_list(Parser* parser, U32* outCount) {
    if (!lexer_expect(parser->lexer, TokenKind_OpenBrace, nullptr)) {
        parser_error_(parser, str8("Expected '{' to start field list"));
        return nullptr;
    }
    
    ASTField* head = nullptr;
    ASTField* tail = nullptr;
    U32 count = 0;
    
    while (!lexer_accept(parser->lexer, TokenKind_CloseBrace, nullptr)) {
        if (parser->hasError) {
            return nullptr;
        }
        
        ASTField* field = parse_field(parser);
        if (!field) {
            return nullptr;
        }
        
        SLIST_APPEND(head, tail, field);
        count++;
        
        if (!lexer_accept(parser->lexer, TokenKind_Comma, nullptr)) {
            if (!lexer_expect(parser->lexer, TokenKind_CloseBrace, nullptr)) {
                parser_error_(parser, str8("Expected ',' or '}' after field"));
                return nullptr;
            }
            break;
        }
    }
    
    if (outCount) {
        *outCount = count;
    }
    return head;
}

static ASTVariant* parse_variant(Parser* parser) {
    Token nameTok;
    if (!lexer_expect(parser->lexer, TokenKind_Identifier, &nameTok)) {
        parser_error_(parser, str8("Expected variant name"));
        return nullptr;
    }
    
    ASTVariant* variant = ARENA_PUSH_STRUCT(parser->arena, ASTVariant);
    MEMSET(variant, 0, sizeof(ASTVariant));
    variant->name = nameTok.text;
    
    Token peek = lexer_peek_token(parser->lexer);
    if (peek.kind == TokenKind_OpenBrace) {
        variant->fields = parse_field_list(parser, &variant->fieldCount);
        if (parser->hasError) {
            return nullptr;
        }
    }
    
    if (!lexer_accept(parser->lexer, TokenKind_Comma, nullptr)) {
        peek = lexer_peek_token(parser->lexer);
        if (peek.kind != TokenKind_CloseBrace) {
            parser_error_(parser, str8("Expected ',' after variant"));
            return nullptr;
        }
    }
    
    return variant;
}

static ASTSumType* parse_sum_type(Parser* parser) {
    if (!lexer_expect(parser->lexer, TokenKind_KW_SumType, nullptr)) {
        parser_error_(parser, str8("Expected 'sum_type' after '@'"));
        return nullptr;
    }
    
    Token nameTok;
    if (!lexer_expect(parser->lexer, TokenKind_Identifier, &nameTok)) {
        parser_error_(parser, str8("Expected sum type name"));
        return nullptr;
    }
    
    ASTSumType* sumType = ARENA_PUSH_STRUCT(parser->arena, ASTSumType);
    MEMSET(sumType, 0, sizeof(ASTSumType));
    sumType->name = nameTok.text;
    sumType->generateMatch = 1;
    
    if (!lexer_expect(parser->lexer, TokenKind_OpenBrace, nullptr)) {
        parser_error_(parser, str8("Expected '{' after sum type name"));
        return nullptr;
    }
    
    ASTVariant* variantHead = nullptr;
    ASTVariant* variantTail = nullptr;
    
    while (!lexer_accept(parser->lexer, TokenKind_CloseBrace, nullptr)) {
        if (parser->hasError) {
            return nullptr;
        }
        
        Token peek = lexer_peek_token(parser->lexer);
        
        if (peek.kind == TokenKind_At) {
            lexer_skip_token(parser->lexer);
            peek = lexer_peek_token(parser->lexer);
            
            if (peek.kind == TokenKind_KW_Common) {
                lexer_skip_token(parser->lexer);
                sumType->commonFields = parse_field_list(parser, &sumType->commonFieldCount);
                if (parser->hasError) {
                    return nullptr;
                }
                continue;
            } else if (peek.kind == TokenKind_KW_NoMatch) {
                lexer_skip_token(parser->lexer);
                sumType->generateMatch = 0;
                continue;
            } else {
                parser_error_(parser, str8_fmt(parser->arena, "Unknown directive '@{}'", peek.text));
                return nullptr;
            }
        }
        
        ASTVariant* variant = parse_variant(parser);
        if (!variant) {
            return nullptr;
        }
        
        SLIST_APPEND(variantHead, variantTail, variant);
        sumType->variantCount++;
    }
    
    sumType->variants = variantHead;
    return sumType;
}

ASTFile* parser_parse_file(Parser* parser) {
    ASTFile* file = ARENA_PUSH_STRUCT(parser->arena, ASTFile);
    MEMSET(file, 0, sizeof(ASTFile));
    file->filename = parser->lexer->filename;
    
    ASTSumType* sumTypeHead = nullptr;
    ASTSumType* sumTypeTail = nullptr;
    
    for (;;) {
        Token tok = lexer_peek_token(parser->lexer);
        
        if (tok.kind == TokenKind_EOF) {
            break;
        }
        
        if (tok.kind == TokenKind_At) {
            lexer_skip_token(parser->lexer);
            
            ASTSumType* sumType = parse_sum_type(parser);
            if (!sumType) {
                file->hasError = 1;
                file->errorMessage = parser->errorMessage;
                return file;
            }
            
            SLIST_APPEND(sumTypeHead, sumTypeTail, sumType);
            file->sumTypeCount++;
        } else {
            parser_error_(parser, str8_fmt(parser->arena, "Expected '@' to start declaration, got {}", str8(token_kind_name(tok.kind))));
            file->hasError = 1;
            file->errorMessage = parser->errorMessage;
            return file;
        }
    }
    
    file->sumTypes = sumTypeHead;
    return file;
}
