//
// Created by André Leite on 10/12/2024.
//

#pragma once

struct Generator {
    Arena* arena;
    Str8Builder output;
    U32 indentLevel;
};

void generator_init(Generator * gen, Arena * arena);
void generator_generate_file(Generator * gen, ASTFile * file);
StringU8 generator_get_output(Generator* gen);