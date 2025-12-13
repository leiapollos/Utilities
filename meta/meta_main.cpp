//
// Created by André Leite on 10/12/2024.
//

#include "nstl/base/base_include.hpp"
#include "nstl/base/base_spmd.hpp"
#include "nstl/os/os_include.hpp"

#include "nstl/os/os_include.cpp"
#include "nstl/base/base_include.cpp"

#include "lexer.hpp"
#include "lexer.cpp"
#include "parser.hpp"
#include "parser.cpp"
#include "generator.hpp"
#include "generator.cpp"

int main() {
    OS_init();
    Arena* arena = arena_alloc();
    StringU8 source = str8("@sum_type Event { WindowResize { width: U32, height: U32 }, Quit }");
    
    printf("Testing parser with source: %.*s\n", (int)source.size, source.data);
    
    Lexer lexer;
    lexer_init(&lexer, arena, source, str8("test.meta"));
    
    Parser parser;
    MEMSET(&parser, 0, sizeof(parser));
    parser.arena = arena;
    parser.lexer = &lexer;
    
    ASTFile* file = parser_parse_file(&parser);
    ast_print_file(file);
    
    printf("\n--- Generating Code ---\n");
    Generator gen;
    generator_init(&gen, arena);
    generator_generate_file(&gen, file);
    
    StringU8 output = generator_get_output(&gen);
    printf("%.*s\n", (int)output.size, output.data);
    
    return 0;
}
