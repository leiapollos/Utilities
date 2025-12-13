//
// Created by André Leite on 10/12/2024.
//

#include "generator.hpp"
#include <stdarg.h>
#include <stdio.h>

void generator_init(Generator* gen, Arena* arena) {
    MEMSET(gen, 0, sizeof(Generator));
    gen->arena = arena;
    str8builder_init(&gen->output, arena, 4096);
    gen->indentLevel = 0;
}

static void gen_newline(Generator* gen) {
    str8builder_append_char(&gen->output, '\n');
}

static void gen_indent(Generator* gen) {
    for (U32 i = 0; i < gen->indentLevel; ++i) {
        str8builder_append(&gen->output, str8("    "));
    }
}

static void gen_line(Generator* gen, const char* fmt, ...) {
    gen_indent(gen);
    
    va_list args;
    va_start(args, fmt);
    
    va_list argsCopy;
    va_copy(argsCopy, args);
    int needed = vsnprintf(NULL, 0, fmt, argsCopy);
    va_end(argsCopy);
    
    if (needed > 0) {
        char* buffer = ARENA_PUSH_ARRAY(gen->arena, char, (U64)needed + 1);
        vsnprintf(buffer, (size_t)needed + 1, fmt, args);
        str8builder_append(&gen->output, str8(buffer, (U64)needed));
    }
    
    va_end(args);
    str8builder_append_char(&gen->output, '\n');
}

static void generate_sum_type(Generator* gen, ASTSumType* sumType) {
    // TODO: Implement sum type generation
    gen_line(gen, "// Sum Type: %.*s", (int)sumType->name.size, sumType->name.data);
}

void generator_generate_file(Generator* gen, ASTFile* file) {
    gen_line(gen, "//");
    gen_line(gen, "// AUTO-GENERATED FILE - DO NOT EDIT");
    gen_line(gen, "// Generated from: %.*s", (int)file->filename.size, file->filename.data);
    gen_line(gen, "//");
    gen_newline(gen);
    gen_line(gen, "#pragma once");
    gen_newline(gen);
    
    for (ASTSumType* st = file->sumTypes; st; st = st->next) {
        generate_sum_type(gen, st);
    }
}

StringU8 generator_get_output(Generator* gen) {
    return str8builder_to_string(&gen->output);
}
