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

int main() {
    OS_init();
    Arena* arena = arena_alloc();
    StringU8 source = str8("struct Foo { int x; };");
    
    printf("Testing lexer with source: %.*s\n", (int)source.size, source.data);
    
    Lexer lexer;
    lexer_init(&lexer, arena, source, str8("test.meta"));
    
    for(;;) {
        Token t = lexer_next_token(&lexer);
        printf("Token: %s", token_kind_name(t.kind));
        if (t.kind == TokenKind_Identifier || t.kind == TokenKind_String || t.kind == TokenKind_Number) {
             printf(" '%.*s'", (int)t.text.size, t.text.data);
        }
        printf("\n");
        if(t.kind == TokenKind_EOF) break;
    }
    return 0;
}
