//
// Created by André Leite on 10/12/2024.
//

#pragma once

struct ASTField {
    StringU8 name;
    StringU8 typeName;
    B32 isPointer;
    B32 isReference;
    
    ASTField* next;
};

struct ASTVariant {
    StringU8 name;
    ASTField* fields;
    U32 fieldCount;
    
    ASTVariant* next;
};

struct ASTSumType {
    StringU8 name;
    ASTField* commonFields;
    U32 commonFieldCount;
    ASTVariant* variants;
    U32 variantCount;
    B32 generateMatch;
    
    ASTSumType* next;
};

struct ASTFile {
    StringU8 filename;
    ASTSumType* sumTypes;
    U32 sumTypeCount;
    
    B32 hasError;
    StringU8 errorMessage;
};

struct Parser {
    Lexer* lexer;
    Arena* arena;
    
    B32 hasError;
    StringU8 errorMessage;
};

void parser_init(Parser* parser, Arena* arena, Lexer* lexer);
ASTFile* parser_parse_file(Parser* parser);

void ast_print_file(ASTFile* file);
