//
// Created by André Leite on 10/12/2024.
//

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

static void gen_newline(Generator* gen) {
    str8builder_append_char(&gen->output, '\n');
}



static StringU8 variant_member_name(Arena* arena, StringU8 variantName) {
    if (variantName.size == 0) {
        return STR8_EMPTY;
    }
    
    U8* out = ARENA_PUSH_ARRAY(arena, U8, variantName.size + 1);
    out[0] = (variantName.data[0] >= 'A' && variantName.data[0] <= 'Z') 
             ? (U8)(variantName.data[0] - 'A' + 'a') 
             : variantName.data[0];
    for (U64 i = 1; i < variantName.size; ++i) {
        out[i] = variantName.data[i];
    }
    out[variantName.size] = '\0';
    return str8(out, variantName.size);
}

static void gen_field_type(Str8Builder* sb, ASTField* f, B32 includeSpace) {
    if (f->isPointer) {
        str8builder_appendf(sb, "%.*s*%s",
            (int)f->typeName.size, f->typeName.data,
            includeSpace ? " " : "");
    } else if (f->isReference) {
        str8builder_appendf(sb, "%.*s&%s",
            (int)f->typeName.size, f->typeName.data,
            includeSpace ? " " : "");
    } else {
        str8builder_appendf(sb, "%.*s%s",
            (int)f->typeName.size, f->typeName.data,
            includeSpace ? " " : "");
    }
}

static void gen_field_decl(Str8Builder* sb, ASTField* f) {
    gen_field_type(sb, f, 1);
    str8builder_appendf(sb, "%.*s", (int)f->name.size, f->name.data);
}

static void gen_field_line(Generator* gen, ASTField* f) {
    Str8Builder sb;
    str8builder_init(&sb, gen->arena, f->typeName.size + f->name.size + 8);
    gen_field_decl(&sb, f);
    str8builder_append_char(&sb, ';');
    gen_line(gen, "%.*s", (int)sb.size, sb.data);
}

void generator_init(Generator* gen, Arena* arena) {
    MEMSET(gen, 0, sizeof(Generator));
    gen->arena = arena;
    str8builder_init(&gen->output, arena, KB(16));
    gen->indentLevel = 0;
}

static void generate_tag_enum(Generator* gen, ASTSumType* sumType) {
    gen_line(gen, "enum %.*s_Tag {", (int)sumType->name.size, sumType->name.data);
    gen->indentLevel++;
    
    U32 index = 0;
    for (ASTVariant* v = sumType->variants; v; v = v->next, index++) {
        gen_line(gen, "%.*s_Tag_%.*s = %u,",
            (int)sumType->name.size, sumType->name.data,
            (int)v->name.size, v->name.data,
            index);
    }
    
    gen->indentLevel--;
    gen_line(gen, "};");
    gen_newline(gen);
}

static void generate_variant_struct(Generator* gen, ASTSumType* sumType, ASTVariant* variant) {
    if (variant->fieldCount == 0) {
        return;
    }
    
    gen_line(gen, "struct %.*s_%.*s {",
        (int)sumType->name.size, sumType->name.data,
        (int)variant->name.size, variant->name.data);
    gen->indentLevel++;
    
    for (ASTField* f = variant->fields; f; f = f->next) {
        gen_field_line(gen, f);
    }
    
    gen->indentLevel--;
    gen_line(gen, "};");
    gen_newline(gen);
}

static void generate_main_struct(Generator* gen, ASTSumType* sumType) {
    gen_line(gen, "struct %.*s {", (int)sumType->name.size, sumType->name.data);
    gen->indentLevel++;
    
    gen_line(gen, "%.*s_Tag tag;", (int)sumType->name.size, sumType->name.data);
    
    for (ASTField* f = sumType->commonFields; f; f = f->next) {
        gen_field_line(gen, f);
    }
    
    gen_newline(gen);
    
    B32 hasNonUnitVariants = 0;
    for (ASTVariant* v = sumType->variants; v; v = v->next) {
        if (v->fieldCount > 0) {
            hasNonUnitVariants = 1;
            break;
        }
    }
    
    if (hasNonUnitVariants) {
        gen_line(gen, "union {");
        gen->indentLevel++;
        
        for (ASTVariant* v = sumType->variants; v; v = v->next) {
            if (v->fieldCount > 0) {
                StringU8 memberName = variant_member_name(gen->arena, v->name);
                gen_line(gen, "%.*s_%.*s %.*s;",
                    (int)sumType->name.size, sumType->name.data,
                    (int)v->name.size, v->name.data,
                    (int)memberName.size, memberName.data);
            }
        }
        
        gen->indentLevel--;
        gen_line(gen, "};");
    }
    
    gen_newline(gen);
    
    for (ASTVariant* v = sumType->variants; v; v = v->next) {
        StringU8 snakeName = str8_to_snake_case(gen->arena, v->name);
        StringU8 memberName = variant_member_name(gen->arena, v->name);
        
        Str8Builder params;
        str8builder_init(&params, gen->arena, 256);
        
        B32 first = 1;
        for (ASTField* f = sumType->commonFields; f; f = f->next) {
            if (!first) {
                str8builder_append(&params, str8(", "));
            }
            first = 0;
            gen_field_decl(&params, f);
        }
        
        for (ASTField* f = v->fields; f; f = f->next) {
            if (!first) {
                str8builder_append(&params, str8(", "));
            }
            first = 0;
            gen_field_decl(&params, f);
        }
        
        StringU8 paramStr = str8builder_to_string(&params);
        
        gen_line(gen, "static %.*s %.*s(%.*s) {",
            (int)sumType->name.size, sumType->name.data,
            (int)snakeName.size, snakeName.data,
            (int)paramStr.size, paramStr.data);
        gen->indentLevel++;
        
        gen_line(gen, "%.*s result = {};", (int)sumType->name.size, sumType->name.data);
        gen_line(gen, "result.tag = %.*s_Tag_%.*s;",
            (int)sumType->name.size, sumType->name.data,
            (int)v->name.size, v->name.data);
        
        for (ASTField* f = sumType->commonFields; f; f = f->next) {
            gen_line(gen, "result.%.*s = %.*s;",
                (int)f->name.size, f->name.data,
                (int)f->name.size, f->name.data);
        }
        
        if (v->fieldCount > 0) {
            for (ASTField* f = v->fields; f; f = f->next) {
                gen_line(gen, "result.%.*s.%.*s = %.*s;",
                    (int)memberName.size, memberName.data,
                    (int)f->name.size, f->name.data,
                    (int)f->name.size, f->name.data);
            }
        }
        
        gen_line(gen, "return result;");
        gen->indentLevel--;
        gen_line(gen, "}");
    }
    
    gen_newline(gen);
    
    for (ASTVariant* v = sumType->variants; v; v = v->next) {
        StringU8 snakeName = str8_to_snake_case(gen->arena, v->name);
        gen_line(gen, "B32 is_%.*s() const { return tag == %.*s_Tag_%.*s; }",
            (int)snakeName.size, snakeName.data,
            (int)sumType->name.size, sumType->name.data,
            (int)v->name.size, v->name.data);
    }
    
    gen_newline(gen);
    
    for (ASTVariant* v = sumType->variants; v; v = v->next) {
        if (v->fieldCount == 0) {
            continue;
        }
        
        StringU8 snakeName = str8_to_snake_case(gen->arena, v->name);
        StringU8 memberName = variant_member_name(gen->arena, v->name);
        
        gen_line(gen, "%.*s_%.*s* as_%.*s() {",
            (int)sumType->name.size, sumType->name.data,
            (int)v->name.size, v->name.data,
            (int)snakeName.size, snakeName.data);
        gen->indentLevel++;
        gen_line(gen, "ASSERT_DEBUG(tag == %.*s_Tag_%.*s);",
            (int)sumType->name.size, sumType->name.data,
            (int)v->name.size, v->name.data);
        gen_line(gen, "return (tag == %.*s_Tag_%.*s) ? &%.*s : nullptr;",
            (int)sumType->name.size, sumType->name.data,
            (int)v->name.size, v->name.data,
            (int)memberName.size, memberName.data);
        gen->indentLevel--;
        gen_line(gen, "}");
        
        gen_line(gen, "const %.*s_%.*s* as_%.*s() const {",
            (int)sumType->name.size, sumType->name.data,
            (int)v->name.size, v->name.data,
            (int)snakeName.size, snakeName.data);
        gen->indentLevel++;
        gen_line(gen, "ASSERT_DEBUG(tag == %.*s_Tag_%.*s);",
            (int)sumType->name.size, sumType->name.data,
            (int)v->name.size, v->name.data);
        gen_line(gen, "return (tag == %.*s_Tag_%.*s) ? &%.*s : nullptr;",
            (int)sumType->name.size, sumType->name.data,
            (int)v->name.size, v->name.data,
            (int)memberName.size, memberName.data);
        gen->indentLevel--;
        gen_line(gen, "}");
    }
    
    if (sumType->generateMatch) {
        gen_newline(gen);
        
        gen_line(gen, "template<typename... Visitors>");
        gen_line(gen, "auto match(Visitors&&... visitors) {");
        gen->indentLevel++;
        
        gen_line(gen, "struct Overloaded : Visitors... { using Visitors::operator()...; };");
        gen_line(gen, "Overloaded overloaded{static_cast<Visitors&&>(visitors)...};");
        gen_newline(gen);
        gen_line(gen, "switch (tag) {");
        gen->indentLevel++;
        
        for (ASTVariant* v = sumType->variants; v; v = v->next) {
            StringU8 memberName = variant_member_name(gen->arena, v->name);
            gen_line(gen, "case %.*s_Tag_%.*s:",
                (int)sumType->name.size, sumType->name.data,
                (int)v->name.size, v->name.data);
            gen->indentLevel++;
            
            if (v->fieldCount > 0) {
                gen_line(gen, "return overloaded(&%.*s);", (int)memberName.size, memberName.data);
            } else {
                gen_line(gen, "return overloaded(static_cast<%.*s_%.*s*>(nullptr));",
                    (int)sumType->name.size, sumType->name.data,
                    (int)v->name.size, v->name.data);
            }
            
            gen->indentLevel--;
        }
        
        gen_line(gen, "default:");
        gen->indentLevel++;
        gen_line(gen, "ASSERT_DEBUG(false && \"Invalid tag\");");
        gen_line(gen, "return overloaded(static_cast<void*>(nullptr));");
        gen->indentLevel--;
        
        gen->indentLevel--;
        gen_line(gen, "}");
        gen->indentLevel--;
        gen_line(gen, "}");
    }
    
    gen->indentLevel--;
    gen_line(gen, "};");
    gen_newline(gen);
}

static void generate_sum_type(Generator* gen, ASTSumType* sumType) {
    gen_line(gen, "// ////////////////////////");
    gen_line(gen, "// Sum Type: %.*s", (int)sumType->name.size, sumType->name.data);
    gen_newline(gen);
    
    for (ASTVariant* v = sumType->variants; v; v = v->next) {
        if (v->fieldCount == 0) {
            gen_line(gen, "struct %.*s_%.*s {};",
                (int)sumType->name.size, sumType->name.data,
                (int)v->name.size, v->name.data);
        }
    }
    gen_newline(gen);
    
    generate_tag_enum(gen, sumType);
    
    for (ASTVariant* v = sumType->variants; v; v = v->next) {
        generate_variant_struct(gen, sumType, v);
    }
    
    generate_main_struct(gen, sumType);
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
