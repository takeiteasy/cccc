// json.c - AST to JSON serialization for header declarations
// Outputs functions, structs, unions, enums as JSON for FFI wrapper generation

#include "jcc.h"
#include "internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Print indentation
void print_indent(FILE *f, int indent) {
    for (int i = 0; i < indent; i++) 
        fprintf(f, "  ");
}

// Print string with JSON escaping
void print_escaped_string(FILE *f, const char *str) {
    if (!str) {
        fprintf(f, "null");
        return;
    }
    fprintf(f, "\"");
    for (const char *p = str; *p; p++) {
        switch (*p) {
            case '"':  fprintf(f, "\\\""); break;
            case '\\': fprintf(f, "\\\\"); break;
            case '\n': fprintf(f, "\\n"); break;
            case '\r': fprintf(f, "\\r"); break;
            case '\t': fprintf(f, "\\t"); break;
            default:   fprintf(f, "%c", *p); break;
        }
    }
    fprintf(f, "\"");
}

// Get type kind name as string
static const char *type_kind_name(TypeKind kind) {
    switch (kind) {
        case TY_VOID:    return "void";
        case TY_BOOL:    return "bool";
        case TY_CHAR:    return "char";
        case TY_SHORT:   return "short";
        case TY_INT:     return "int";
        case TY_LONG:    return "long";
        case TY_FLOAT:   return "float";
        case TY_DOUBLE:  return "double";
        case TY_LDOUBLE: return "ldouble";
        case TY_ENUM:    return "enum";
        case TY_PTR:     return "pointer";
        case TY_FUNC:    return "function";
        case TY_ARRAY:   return "array";
        case TY_VLA:     return "vla";
        case TY_STRUCT:  return "struct";
        case TY_UNION:   return "union";
        default:         return "unknown";
    }
}

// Serialize a type recursively
void serialize_type_json(FILE *f, Type *ty, int indent) {
    if (!ty) {
        fprintf(f, "null");
        return;
    }

    fprintf(f, "{\n");

    // Kind
    print_indent(f, indent + 1);
    fprintf(f, "\"kind\": \"%s\",\n", type_kind_name(ty->kind));

    // Size and alignment
    print_indent(f, indent + 1);
    fprintf(f, "\"size\": %d,\n", ty->size);
    print_indent(f, indent + 1);
    fprintf(f, "\"align\": %d", ty->align);

    // Type-specific properties
    if (ty->is_unsigned) {
        fprintf(f, ",\n");
        print_indent(f, indent + 1);
        fprintf(f, "\"unsigned\": true");
    }

    if (ty->is_const) {
        fprintf(f, ",\n");
        print_indent(f, indent + 1);
        fprintf(f, "\"const\": true");
    }

    // Pointer or array base type
    if (ty->base) {
        fprintf(f, ",\n");
        print_indent(f, indent + 1);
        fprintf(f, "\"base\": ");
        serialize_type_json(f, ty->base, indent + 1);
    }

    // Array length
    if (ty->kind == TY_ARRAY) {
        fprintf(f, ",\n");
        print_indent(f, indent + 1);
        fprintf(f, "\"array_length\": %d", ty->array_len);
    }

    // Struct/Union name and members
    if ((ty->kind == TY_STRUCT || ty->kind == TY_UNION) && ty->name) {
        fprintf(f, ",\n");
        print_indent(f, indent + 1);
        fprintf(f, "\"name\": ");
        // Extract name from token
        int name_len = ty->name->len;
        char *name = malloc(name_len + 1);
        strncpy(name, ty->name->loc, name_len);
        name[name_len] = '\0';
        print_escaped_string(f, name);
        free(name);

        // Members
        if (ty->members) {
            fprintf(f, ",\n");
            print_indent(f, indent + 1);
            fprintf(f, "\"members\": [\n");

            bool first_member = true;
            for (Member *m = ty->members; m; m = m->next) {
                if (!first_member) 
                    fprintf(f, ",\n");
                first_member = false;

                print_indent(f, indent + 2);
                fprintf(f, "{\n");

                // Member name
                print_indent(f, indent + 3);
                fprintf(f, "\"name\": ");
                if (m->name) {
                    int m_name_len = m->name->len;
                    char *m_name = malloc(m_name_len + 1);
                    strncpy(m_name, m->name->loc, m_name_len);
                    m_name[m_name_len] = '\0';
                    print_escaped_string(f, m_name);
                    free(m_name);
                } else 
                    fprintf(f, "null");
                fprintf(f, ",\n");

                // Member type
                print_indent(f, indent + 3);
                fprintf(f, "\"type\": ");
                serialize_type_json(f, m->ty, indent + 3);
                fprintf(f, ",\n");

                // Member offset
                print_indent(f, indent + 3);
                fprintf(f, "\"offset\": %d", m->offset);

                // Bitfield info
                if (m->is_bitfield) {
                    fprintf(f, ",\n");
                    print_indent(f, indent + 3);
                    fprintf(f, "\"bitfield\": true,\n");
                    print_indent(f, indent + 3);
                    fprintf(f, "\"bit_offset\": %d,\n", m->bit_offset);
                    print_indent(f, indent + 3);
                    fprintf(f, "\"bit_width\": %d", m->bit_width);
                }

                fprintf(f, "\n");
                print_indent(f, indent + 2);
                fprintf(f, "}");
            }

            fprintf(f, "\n");
            print_indent(f, indent + 1);
            fprintf(f, "]");
        }
    }

    // Enum name and constants
    if (ty->kind == TY_ENUM) {
        if (ty->name) {
            fprintf(f, ",\n");
            print_indent(f, indent + 1);
            fprintf(f, "\"name\": ");
            int name_len = ty->name->len;
            char *name = malloc(name_len + 1);
            strncpy(name, ty->name->loc, name_len);
            name[name_len] = '\0';
            print_escaped_string(f, name);
            free(name);
        }
    }

    // Function return type and parameters
    if (ty->kind == TY_FUNC) {
        fprintf(f, ",\n");
        print_indent(f, indent + 1);
        fprintf(f, "\"return_type\": ");
        serialize_type_json(f, ty->return_ty, indent + 1);

        if (ty->params) {
            fprintf(f, ",\n");
            print_indent(f, indent + 1);
            fprintf(f, "\"parameters\": [\n");

            bool first_param = true;
            for (Type *p = ty->params; p; p = p->next) {
                if (!first_param) 
                    fprintf(f, ",\n");
                first_param = false;

                print_indent(f, indent + 2);
                fprintf(f, "{\n");

                // Parameter name (if available)
                print_indent(f, indent + 3);
                fprintf(f, "\"name\": ");
                if (p->name) {
                    int p_name_len = p->name->len;
                    char *p_name = malloc(p_name_len + 1);
                    strncpy(p_name, p->name->loc, p_name_len);
                    p_name[p_name_len] = '\0';
                    print_escaped_string(f, p_name);
                    free(p_name);
                } else 
                    fprintf(f, "null");
                fprintf(f, ",\n");

                // Parameter type
                print_indent(f, indent + 3);
                fprintf(f, "\"type\": ");
                serialize_type_json(f, p, indent + 3);

                fprintf(f, "\n");
                print_indent(f, indent + 2);
                fprintf(f, "}");
            }

            fprintf(f, "\n");
            print_indent(f, indent + 1);
            fprintf(f, "]");
        }

        if (ty->is_variadic) {
            fprintf(f, ",\n");
            print_indent(f, indent + 1);
            fprintf(f, "\"variadic\": true");
        }
    }

    fprintf(f, "\n");
    print_indent(f, indent);
    fprintf(f, "}");
}

// Serialize a function declaration
static void serialize_function_json(FILE *f, Obj *fn, int indent) {
    print_indent(f, indent);
    fprintf(f, "{\n");

    // Name
    print_indent(f, indent + 1);
    fprintf(f, "\"name\": ");
    print_escaped_string(f, fn->name);
    fprintf(f, ",\n");

    // Storage class
    print_indent(f, indent + 1);
    if (fn->is_static)
        fprintf(f, "\"storage_class\": \"static\",\n");
    else
        fprintf(f, "\"storage_class\": \"extern\",\n");

    // Type (function signature)
    print_indent(f, indent + 1);
    fprintf(f, "\"type\": ");
    serialize_type_json(f, fn->ty, indent + 1);

    fprintf(f, "\n");
    print_indent(f, indent);
    fprintf(f, "}");
}

// Serialize a struct/union declaration
static void serialize_aggregate_json(FILE *f, Type *ty, int indent, const char *kind) {
    print_indent(f, indent);
    fprintf(f, "{\n");

    // Name
    print_indent(f, indent + 1);
    fprintf(f, "\"name\": ");
    if (ty->name) {
        int name_len = ty->name->len;
        char *name = malloc(name_len + 1);
        strncpy(name, ty->name->loc, name_len);
        name[name_len] = '\0';
        print_escaped_string(f, name);
        free(name);
    } else 
        fprintf(f, "null");
    fprintf(f, ",\n");

    // Size and alignment
    print_indent(f, indent + 1);
    fprintf(f, "\"size\": %d,\n", ty->size);
    print_indent(f, indent + 1);
    fprintf(f, "\"align\": %d", ty->align);

    // Members
    if (ty->members) {
        fprintf(f, ",\n");
        print_indent(f, indent + 1);
        fprintf(f, "\"members\": [\n");

        bool first_member = true;
        for (Member *m = ty->members; m; m = m->next) {
            if (!first_member) 
                fprintf(f, ",\n");
            first_member = false;

            print_indent(f, indent + 2);
            fprintf(f, "{\n");

            // Member name
            print_indent(f, indent + 3);
            fprintf(f, "\"name\": ");
            if (m->name) {
                int m_name_len = m->name->len;
                char *m_name = malloc(m_name_len + 1);
                strncpy(m_name, m->name->loc, m_name_len);
                m_name[m_name_len] = '\0';
                print_escaped_string(f, m_name);
                free(m_name);
            } else 
                fprintf(f, "null");
            fprintf(f, ",\n");

            // Member type
            print_indent(f, indent + 3);
            fprintf(f, "\"type\": ");
            serialize_type_json(f, m->ty, indent + 3);
            fprintf(f, ",\n");

            // Member offset
            print_indent(f, indent + 3);
            fprintf(f, "\"offset\": %d", m->offset);

            // Bitfield info
            if (m->is_bitfield) {
                fprintf(f, ",\n");
                print_indent(f, indent + 3);
                fprintf(f, "\"bitfield\": true,\n");
                print_indent(f, indent + 3);
                fprintf(f, "\"bit_offset\": %d,\n", m->bit_offset);
                print_indent(f, indent + 3);
                fprintf(f, "\"bit_width\": %d", m->bit_width);
            }

            fprintf(f, "\n");
            print_indent(f, indent + 2);
            fprintf(f, "}");
        }

        fprintf(f, "\n");
        print_indent(f, indent + 1);
        fprintf(f, "]");
    }

    fprintf(f, "\n");
    print_indent(f, indent);
    fprintf(f, "}");
}

// Serialize an enum declaration
static void serialize_enum_json(FILE *f, Type *ty, int indent) {
    print_indent(f, indent);
    fprintf(f, "{\n");

    // Name
    print_indent(f, indent + 1);
    fprintf(f, "\"name\": ");
    if (ty->name) {
        int name_len = ty->name->len;
        char *name = malloc(name_len + 1);
        strncpy(name, ty->name->loc, name_len);
        name[name_len] = '\0';
        print_escaped_string(f, name);
        free(name);
    } else 
        fprintf(f, "null");
    fprintf(f, ",\n");

    // Size
    print_indent(f, indent + 1);
    fprintf(f, "\"size\": %d", ty->size);

    // Constants
    if (ty->enum_constants) {
        fprintf(f, ",\n");
        print_indent(f, indent + 1);
        fprintf(f, "\"constants\": [\n");

        bool first_const = true;
        for (EnumConstant *ec = ty->enum_constants; ec; ec = ec->next) {
            if (!first_const) 
                fprintf(f, ",\n");
            first_const = false;

            print_indent(f, indent + 2);
            fprintf(f, "{\n");

            print_indent(f, indent + 3);
            fprintf(f, "\"name\": ");
            print_escaped_string(f, ec->name);
            fprintf(f, ",\n");

            print_indent(f, indent + 3);
            fprintf(f, "\"value\": %d", ec->value);

            fprintf(f, "\n");
            print_indent(f, indent + 2);
            fprintf(f, "}");
        }

        fprintf(f, "\n");
        print_indent(f, indent + 1);
        fprintf(f, "]");
    }

    fprintf(f, "\n");
    print_indent(f, indent);
    fprintf(f, "}");
}

// Serialize a global variable declaration
static void serialize_global_var_json(FILE *f, Obj *var, int indent) {
    print_indent(f, indent);
    fprintf(f, "{\n");

    // Name
    print_indent(f, indent + 1);
    fprintf(f, "\"name\": ");
    print_escaped_string(f, var->name);
    fprintf(f, ",\n");

    // Storage class
    print_indent(f, indent + 1);
    if (var->is_static)
        fprintf(f, "\"storage_class\": \"static\",\n");
    else
        fprintf(f, "\"storage_class\": \"extern\",\n");

    // Type
    print_indent(f, indent + 1);
    fprintf(f, "\"type\": ");
    serialize_type_json(f, var->ty, indent + 1);

    fprintf(f, "\n");
    print_indent(f, indent);
    fprintf(f, "}");
}

// Helper to check if type already seen
static bool type_seen(Type *ty, Type **seen_types, int seen_types_count) {
    for (int i = 0; i < seen_types_count; i++)
        if (seen_types[i] == ty) 
            return true;
    return false;
}

// Helper to mark type as seen
static void mark_type_seen(Type *ty, Type ***seen_types_ptr, int *seen_types_count_ptr, int *seen_types_capacity_ptr) {
    Type **seen_types = *seen_types_ptr;
    int seen_types_count = *seen_types_count_ptr;
    int seen_types_capacity = *seen_types_capacity_ptr;

    if (seen_types_count >= seen_types_capacity) {
        seen_types_capacity = seen_types_capacity == 0 ? 16 : seen_types_capacity * 2;
        seen_types = realloc(seen_types, sizeof(Type*) * seen_types_capacity);
        *seen_types_ptr = seen_types;
        *seen_types_capacity_ptr = seen_types_capacity;
    }
    seen_types[seen_types_count] = ty;
    *seen_types_count_ptr = seen_types_count + 1;
}

// Helper to recursively collect struct/union/enum types
// PLACEHOLDER: This function mutates ty->origin to build temporary linked
// lists, corrupting the type identity graph used by same_type_or_origin.
static void collect_type_recursive(Type *ty, Type **seen_types, int *seen_types_count, int *seen_types_capacity,
                                   Type **structs, Type **unions, Type **enums) {
    if (!ty) return;

    // Mark this type if it's a struct/union/enum
    if (ty->kind == TY_STRUCT && !type_seen(ty, seen_types, *seen_types_count)) {
        mark_type_seen(ty, &seen_types, seen_types_count, seen_types_capacity);
        ty->origin = *structs;
        *structs = ty;
    } else if (ty->kind == TY_UNION && !type_seen(ty, seen_types, *seen_types_count)) {
        mark_type_seen(ty, &seen_types, seen_types_count, seen_types_capacity);
        ty->origin = *unions;
        *unions = ty;
    } else if (ty->kind == TY_ENUM && !type_seen(ty, seen_types, *seen_types_count)) {
        mark_type_seen(ty, &seen_types, seen_types_count, seen_types_capacity);
        ty->origin = *enums;
        *enums = ty;
    }

    // Recursively collect from base type (pointers, arrays)
    if (ty->base) 
        collect_type_recursive(ty->base, seen_types, seen_types_count, seen_types_capacity, structs, unions, enums);

    // Recursively collect from struct/union members
    if ((ty->kind == TY_STRUCT || ty->kind == TY_UNION) && ty->members)
        for (Member *m = ty->members; m; m = m->next) 
            collect_type_recursive(m->ty, seen_types, seen_types_count, seen_types_capacity, structs, unions, enums);

    // Recursively collect from function return type and parameters
    if (ty->kind == TY_FUNC) {
        collect_type_recursive(ty->return_ty, seen_types, seen_types_count, seen_types_capacity, structs, unions, enums);
        for (Type *p = ty->params; p; p = p->next) 
            collect_type_recursive(p, seen_types, seen_types_count, seen_types_capacity, structs, unions, enums);
    }
}

// Main function: Output JSON for all declarations
void cc_output_json(FILE *f, Obj *prog) {
    if (!f || !prog) 
        return;

    // Track unique struct/union/enum types
    // We'll use a simple array to track types we've already output
    Type **seen_types = NULL;
    int seen_types_count = 0;
    int seen_types_capacity = 0;

    // Start JSON output
    fprintf(f, "{\n");

    // First pass: collect unique struct/union/enum types
    Type *structs = NULL;
    Type *unions = NULL;
    Type *enums = NULL;

    for (Obj *obj = prog; obj; obj = obj->next) 
        collect_type_recursive(obj->ty, seen_types, &seen_types_count, &seen_types_capacity, &structs, &unions, &enums);

    // Output functions
    print_indent(f, 1);
    fprintf(f, "\"functions\": [\n");
    bool first = true;
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (obj->is_function) {
            if (!first) 
                fprintf(f, ",\n");
            first = false;
            serialize_function_json(f, obj, 2);
        }
    }
    fprintf(f, "\n");
    print_indent(f, 1);
    fprintf(f, "],\n");

    // Output structs
    print_indent(f, 1);
    fprintf(f, "\"structs\": [\n");
    first = true;
    for (Type *st = structs; st; st = st->origin) {
        if (!first) 
            fprintf(f, ",\n");
        first = false;
        serialize_aggregate_json(f, st, 2, "struct");
    }
    fprintf(f, "\n");
    print_indent(f, 1);
    fprintf(f, "],\n");

    // Output unions
    print_indent(f, 1);
    fprintf(f, "\"unions\": [\n");
    first = true;
    for (Type *un = unions; un; un = un->origin) {
        if (!first) 
            fprintf(f, ",\n");
        first = false;
        serialize_aggregate_json(f, un, 2, "union");
    }
    fprintf(f, "\n");
    print_indent(f, 1);
    fprintf(f, "],\n");

    // Output enums
    print_indent(f, 1);
    fprintf(f, "\"enums\": [\n");
    first = true;
    for (Type *en = enums; en; en = en->origin) {
        if (!first) 
            fprintf(f, ",\n");
        first = false;
        serialize_enum_json(f, en, 2);
    }
    fprintf(f, "\n");
    print_indent(f, 1);
    fprintf(f, "],\n");

    // Output global variables
    print_indent(f, 1);
    fprintf(f, "\"variables\": [\n");
    first = true;
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (!obj->is_function) {
            if (!first) 
                fprintf(f, ",\n");
            first = false;
            serialize_global_var_json(f, obj, 2);
        }
    }
    fprintf(f, "\n");
    print_indent(f, 1);
    fprintf(f, "]\n");

    fprintf(f, "}\n");

    // Cleanup
    if (seen_types)
        free(seen_types);
}

void cc_output_source_map_json(JCC *vm, FILE *f) {
    if (!vm || !f) {
        return;
    }

    fprintf(f, "{\n");
    fprintf(f, "  \"version\": 3,\n");
    fprintf(f, "  \"mappings\": [\n");

    for (int i = 0; i < vm->dbg.source_map_count; i++) {
        SourceMap *map = &vm->dbg.source_map[i];

        fprintf(f, "    {");
        fprintf(f, "\"pc\": %lld, ", map->pc_offset);
        fprintf(f, "\"file\": ");
        print_escaped_string(f, map->file ? map->file->name : NULL);
        fprintf(f, ", ");
        fprintf(f, "\"line\": %d, ", map->line_no);
        fprintf(f, "\"col\": %d, ", map->col_no);
        fprintf(f, "\"end_col\": %d", map->end_col_no);
        fprintf(f, "}");

        if (i < vm->dbg.source_map_count - 1) {
            fprintf(f, ",");
        }
        fprintf(f, "\n");
    }

    fprintf(f, "  ]\n");
    fprintf(f, "}\n");
}
