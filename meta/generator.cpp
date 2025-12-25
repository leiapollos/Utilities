//
// Created by André Leite on 10/12/2024.
//

static void gen_indent(Generator* gen) {
    for (U32 i = 0; i < gen->indentLevel; ++i) {
        str8builder_append(&gen->output, str8("    "));
    }
}

static void gen_line_(Generator* gen, StringU8 content) {
    gen_indent(gen);
    str8builder_append(&gen->output, content);
    str8builder_append_char(&gen->output, '\n');
}

#define gen_line(gen, fmt, ...) gen_line_((gen), str8_fmt((gen)->arena, fmt, __VA_ARGS__))

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

static void gen_field_type(Str8Builder* sb, Arena* arena, ASTField* f, B32 includeSpace) {
    StringU8 suffix = includeSpace ? str8(" ") : str8("");
    if (f->isPointer) {
        str8builder_append(sb, str8_fmt(arena, "{}*{}", f->typeName, suffix));
    } else if (f->isReference) {
        str8builder_append(sb, str8_fmt(arena, "{}&{}", f->typeName, suffix));
    } else {
        str8builder_append(sb, str8_fmt(arena, "{}{}", f->typeName, suffix));
    }
}

static void gen_field_decl(Str8Builder* sb, Arena* arena, ASTField* f) {
    gen_field_type(sb, arena, f, 1);
    str8builder_append(sb, f->name);
}

static void gen_field_line(Generator* gen, ASTField* f) {
    Str8Builder sb;
    str8builder_init(&sb, gen->arena, f->typeName.size + f->name.size + 8);
    gen_field_decl(&sb, gen->arena, f);
    str8builder_append_char(&sb, ';');
    gen_line_(gen, str8builder_to_string(&sb));
}

void generator_init(Generator* gen, Arena* arena) {
    MEMSET(gen, 0, sizeof(Generator));
    gen->arena = arena;
    str8builder_init(&gen->output, arena, KB(16));
    gen->indentLevel = 0;
}

static void generate_tag_enum(Generator* gen, ASTSumType* sumType) {
    gen_line(gen, "enum {}_Tag {{", sumType->name);
    gen->indentLevel++;
    
    U32 index = 0;
    for (ASTVariant* v = sumType->variants; v; v = v->next, index++) {
        gen_line(gen, "{}_Tag_{} = {},", sumType->name, v->name, index);
    }
    
    gen->indentLevel--;
    gen_line_(gen, str8("};"));
    gen_newline(gen);
}

static void generate_variant_struct(Generator* gen, ASTSumType* sumType, ASTVariant* variant) {
    if (variant->fieldCount == 0) {
        return;
    }
    
    gen_line(gen, "struct {}_{} {{", sumType->name, variant->name);
    gen->indentLevel++;
    
    for (ASTField* f = variant->fields; f; f = f->next) {
        gen_field_line(gen, f);
    }
    
    gen->indentLevel--;
    gen_line_(gen, str8("};"));
    gen_newline(gen);
}

static void generate_main_struct(Generator* gen, ASTSumType* sumType) {
    gen_line(gen, "struct {} {{", sumType->name);
    gen->indentLevel++;
    
    gen_line(gen, "{}_Tag tag;", sumType->name);
    
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
        gen_line_(gen, str8("union {"));
        gen->indentLevel++;
        
        for (ASTVariant* v = sumType->variants; v; v = v->next) {
            if (v->fieldCount > 0) {
                StringU8 memberName = variant_member_name(gen->arena, v->name);
                gen_line(gen, "{}_{} {};", sumType->name, v->name, memberName);
            }
        }
        
        gen->indentLevel--;
        gen_line_(gen, str8("};"));
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
            gen_field_decl(&params, gen->arena, f);
        }
        
        for (ASTField* f = v->fields; f; f = f->next) {
            if (!first) {
                str8builder_append(&params, str8(", "));
            }
            first = 0;
            gen_field_decl(&params, gen->arena, f);
        }
        
        StringU8 paramStr = str8builder_to_string(&params);
        
        gen_line(gen, "static {} {}({}) {{", sumType->name, snakeName, paramStr);
        gen->indentLevel++;
        
        gen_line(gen, "{} result = {{}};", sumType->name);
        gen_line(gen, "result.tag = {}_Tag_{};", sumType->name, v->name);
        
        for (ASTField* f = sumType->commonFields; f; f = f->next) {
            gen_line(gen, "result.{} = {};", f->name, f->name);
        }
        
        if (v->fieldCount > 0) {
            for (ASTField* f = v->fields; f; f = f->next) {
                gen_line(gen, "result.{}.{} = {};", memberName, f->name, f->name);
            }
        }
        
        gen_line_(gen, str8("return result;"));
        gen->indentLevel--;
        gen_line_(gen, str8("}"));
    }
    
    gen_newline(gen);
    
    for (ASTVariant* v = sumType->variants; v; v = v->next) {
        StringU8 snakeName = str8_to_snake_case(gen->arena, v->name);
        gen_line(gen, "B32 is_{}() const {{ return tag == {}_Tag_{}; }}", snakeName, sumType->name, v->name);
    }
    
    gen_newline(gen);
    
    for (ASTVariant* v = sumType->variants; v; v = v->next) {
        if (v->fieldCount == 0) {
            continue;
        }
        
        StringU8 snakeName = str8_to_snake_case(gen->arena, v->name);
        StringU8 memberName = variant_member_name(gen->arena, v->name);
        
        gen_line(gen, "{}_{}* as_{}() {{", sumType->name, v->name, snakeName);
        gen->indentLevel++;
        gen_line(gen, "ASSERT_DEBUG(tag == {}_Tag_{});", sumType->name, v->name);
        gen_line(gen, "return (tag == {}_Tag_{}) ? &{} : nullptr;", sumType->name, v->name, memberName);
        gen->indentLevel--;
        gen_line_(gen, str8("}"));
        
        gen_line(gen, "const {}_{}* as_{}() const {{", sumType->name, v->name, snakeName);
        gen->indentLevel++;
        gen_line(gen, "ASSERT_DEBUG(tag == {}_Tag_{});", sumType->name, v->name);
        gen_line(gen, "return (tag == {}_Tag_{}) ? &{} : nullptr;", sumType->name, v->name, memberName);
        gen->indentLevel--;
        gen_line_(gen, str8("}"));
    }
    
    if (sumType->generateMatch) {
        gen_newline(gen);
        
        gen_line_(gen, str8("template<typename... Visitors>"));
        gen_line_(gen, str8("auto match(Visitors&&... visitors) {"));
        gen->indentLevel++;
        
        gen_line_(gen, str8("struct Overloaded : Visitors... { using Visitors::operator()...; };"));
        gen_line_(gen, str8("Overloaded overloaded{static_cast<Visitors&&>(visitors)...};"));
        gen_newline(gen);
        gen_line_(gen, str8("switch (tag) {"));
        gen->indentLevel++;
        
        for (ASTVariant* v = sumType->variants; v; v = v->next) {
            StringU8 memberName = variant_member_name(gen->arena, v->name);
            gen_line(gen, "case {}_Tag_{}:", sumType->name, v->name);
            gen->indentLevel++;
            
            if (v->fieldCount > 0) {
                gen_line(gen, "return overloaded(&{});", memberName);
            } else {
                gen_line(gen, "return overloaded(static_cast<{}_{}*>(nullptr));", sumType->name, v->name);
            }
            
            gen->indentLevel--;
        }
        
        gen_line_(gen, str8("default:"));
        gen->indentLevel++;
        gen_line_(gen, str8("ASSERT_DEBUG(false && \"Invalid tag\");"));
        gen_line_(gen, str8("return overloaded(static_cast<void*>(nullptr));"));
        gen->indentLevel--;
        
        gen->indentLevel--;
        gen_line_(gen, str8("}"));
        gen->indentLevel--;
        gen_line_(gen, str8("}"));
    }
    
    gen->indentLevel--;
    gen_line_(gen, str8("};"));
    gen_newline(gen);
}

static void generate_sum_type(Generator* gen, ASTSumType* sumType) {
    gen_line_(gen, str8("// ////////////////////////"));
    gen_line(gen, "// Sum Type: {}", sumType->name);
    gen_newline(gen);
    
    for (ASTVariant* v = sumType->variants; v; v = v->next) {
        if (v->fieldCount == 0) {
            gen_line(gen, "struct {}_{} {{}};", sumType->name, v->name);
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
    gen_line_(gen, str8("//"));
    gen_line_(gen, str8("// AUTO-GENERATED FILE - DO NOT EDIT"));
    gen_line(gen, "// Generated from: {}", file->filename);
    gen_line_(gen, str8("//"));
    gen_newline(gen);
    gen_line_(gen, str8("#pragma once"));
    gen_newline(gen);
    
    for (ASTSumType* st = file->sumTypes; st; st = st->next) {
        generate_sum_type(gen, st);
    }
}

StringU8 generator_get_output(Generator* gen) {
    return str8builder_to_string(&gen->output);
}
