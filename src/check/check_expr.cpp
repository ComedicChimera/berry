#include "checker.hpp"

static std::unordered_map<TokenKind, HirOpKind> binop_table {
    { TOK_PLUS, HIROP_ADD },
    { TOK_MINUS, HIROP_SUB },
    { TOK_STAR, HIROP_MUL },
    { TOK_FSLASH, HIROP_DIV },
    { TOK_MOD, HIROP_MOD },
    { TOK_SHL, HIROP_SHL },
    { TOK_SHR, HIROP_SHR },
    { TOK_AMP, HIROP_BWAND },
    { TOK_PIPE, HIROP_BWOR },
    { TOK_CARRET, HIROP_BWXOR },
    { TOK_EQ, HIROP_EQ },
    { TOK_NE, HIROP_NE },
    { TOK_LT, HIROP_LT },
    { TOK_LE, HIROP_LE },
    { TOK_GT, HIROP_GT },
    { TOK_GE, HIROP_GE },
    { TOK_AND, HIROP_LGAND },
    { TOK_OR, HIROP_LGOR },
};

static std::unordered_map<TokenKind, HirOpKind> unop_table {
    { TOK_MINUS, HIROP_NEG },
    { TOK_TILDE, HIROP_BWNEG },
    { TOK_NOT, HIROP_NOT }
};

HirExpr* Checker::checkExpr(AstNode* node, Type* infer_type) {
    HirExpr* hexpr;

    switch (node->kind) {
    case AST_TEST_MATCH: {
        auto* hcond = checkExpr(node->an_TestMatch.expr);
        finishExpr();

        pushPatternCtx();
        auto hpatterns = checkCasePattern(node->an_TestMatch.pattern, hcond->type).first;
        declarePatternCaptures(hpatterns[0]);
        popPatternCtx();

        hexpr = allocExpr(HIR_TEST_MATCH, node->span);
        hexpr->type = &prim_bool_type;
        hexpr->ir_TestMatch.expr = hcond;
        hexpr->ir_TestMatch.patterns = hpatterns;
    } break;
    case AST_CAST: {
        auto* dest_type = checkTypeLabel(node->an_Cast.dest_type, true);
        auto* hsrc = checkExpr(node->an_Cast.expr, dest_type);
        mustCast(node->span, hsrc->type, dest_type);

        hexpr = allocExpr(HIR_CAST, node->span);
        hexpr->type = dest_type;
        hexpr->ir_Cast.expr = hsrc;
    } break;
    case AST_BINOP: {
        auto* hlhs = checkExpr(node->an_Binop.lhs);
        auto* hrhs = checkExpr(node->an_Binop.rhs);
        auto hop = binop_table[node->an_Binop.op.tok_kind];

        auto* result_type = mustApplyBinaryOp(
            node->span,
            hop,
            hlhs->type,
            hrhs->type
        );

        hexpr = allocExpr(HIR_BINOP, node->span);
        hexpr->type = result_type;
        hexpr->ir_Binop.lhs = hlhs;
        hexpr->ir_Binop.rhs = hrhs;
        hexpr->ir_Binop.op = hop;
    } break;
    case AST_UNOP: {
        auto* hoperand = checkExpr(node->an_Unop.expr);
        auto hop = unop_table[node->an_Unop.op.tok_kind];

        auto* result_type = mustApplyUnaryOp(node->span, hop, hoperand->type);

        hexpr = allocExpr(HIR_UNOP, node->span);
        hexpr->type = result_type;
        hexpr->ir_Unop.expr = hoperand;
        hexpr->ir_Unop.op = hop;
    } break;
    case AST_ADDR: {
        markNonComptime(node->span);

        auto* helem = checkExpr(node->an_Addr.expr);

        if (!helem->assignable) {
            error(helem->span, "value is not addressable");
        } 

        hexpr = allocExpr(HIR_ADDR, node->span);
        hexpr->type = allocType(TYPE_PTR);
        hexpr->type->ty_Ptr.elem_type = helem->type;
        hexpr->ir_Addr.expr = helem;
    } break;
    case AST_DEREF: {
        markNonComptime(node->span);

        auto* hptr = checkExpr(node->an_Deref.expr);

        auto* ptr_type = hptr->type->Inner();
        if (ptr_type->kind == TYPE_PTR) {
            hexpr = allocExpr(HIR_DEREF, node->span);
            hexpr->type = ptr_type->ty_Ptr.elem_type;
            hexpr->assignable = true;
            hexpr->ir_Deref.expr = hptr;
        } else {
            fatal(hptr->span, "{} is not a pointer", hptr->type->ToString());
        }

    } break;
    case AST_CALL:
        hexpr = checkCall(node);
        break;
    case AST_INDEX: {
        auto* harray = checkExpr(node->an_Index.expr);
        HirExpr* hindex;

        auto* type = harray->type->Inner();
        switch (type->kind) {
        case TYPE_ARRAY: case TYPE_SLICE: case TYPE_STRING:
            hindex = checkExpr(node->an_Index.index, platform_int_type);
            mustIntType(hindex->span, hindex->type);

            hexpr = allocExpr(HIR_INDEX, node->span);
            hexpr->type = type->ty_Slice.elem_type;
            hexpr->assignable = harray->assignable && type->kind != TYPE_STRING;
            hexpr->ir_Index.expr = harray;
            hexpr->ir_Index.index = hindex;
            break;
        default:
            fatal(harray->span, "{} is not indexable", harray->type->ToString());
        }
    } break;
    case AST_SLICE: {
        auto* harray = checkExpr(node->an_Slice.expr);
        HirExpr* hlo = nullptr;
        HirExpr* hhi = nullptr;

        auto* type = harray->type->Inner();
        Type* ret_type = type;
        switch (type->kind) {
        case TYPE_ARRAY: 
            ret_type = allocType(TYPE_SLICE);
            ret_type->ty_Slice.elem_type = type->ty_Array.elem_type;
            // fallthrough
        case TYPE_SLICE: case TYPE_STRING:
            if (node->an_Slice.start_index) {
                hlo = checkExpr(node->an_Slice.start_index, platform_int_type);
                mustIntType(hlo->span, hlo->type);
            }

            if (node->an_Slice.end_index) {
                hhi = checkExpr(node->an_Slice.end_index, platform_int_type);
                mustIntType(hhi->span, hhi->type);
            }

            hexpr = allocExpr(HIR_SLICE, node->span);
            hexpr->type = ret_type;
            hexpr->ir_Slice.expr = harray;
            hexpr->ir_Slice.start_index = hlo;
            hexpr->ir_Slice.end_index = hhi;
            break;
        default:
            fatal(harray->span, "{} is not sliceable", harray->type->ToString());
        }
    } break;
    case AST_SELECTOR:
        hexpr = checkSelector(node, infer_type);
        break;
    case AST_NEW: {
        markNonComptime(node->span);

        auto* elem_type = checkTypeLabel(node->an_New.type, true);

        hexpr = allocExpr(HIR_NEW, node->span);
        hexpr->type = allocType(TYPE_PTR);
        hexpr->type->ty_Ptr.elem_type = elem_type;
        hexpr->ir_New.elem_type = elem_type;
        // TODO: revise for heap allocation
        hexpr->ir_New.alloc_mode = enclosing_return_type ? HIRMEM_STACK : HIRMEM_GLOBAL;
    } break;
    case AST_NEW_ARRAY: 
        hexpr = checkNewArray(node);
        break;
    case AST_NEW_STRUCT:
        hexpr = checkNewStruct(node, infer_type);
        break;
    case AST_ARRAY_LIT:
        hexpr = checkArrayLit(node, infer_type);
        break;
    case AST_STRUCT_LIT:
        hexpr = checkStructLit(node, infer_type);
        break;
    case AST_IDENT: {
        auto [symbol, dep] = mustLookup(node->an_Ident.name, node->span);
        if (dep != nullptr) {
            fatal(node->span, "cannot use a module as a value");
        }

        if (symbol->flags & SYM_TYPE) {
            fatal(node->span, "cannot use a type as a value");
        }

        hexpr = checkValueSymbol(symbol, node->span);
    } break;
    case AST_NUM_LIT:
        hexpr = allocExpr(HIR_NUM_LIT, node->span);
        hexpr->type = infer_type ? infer_type : newUntyped(UK_NUM);
        hexpr->ir_Num.value = node->an_Num.value;
        break;
    case AST_FLOAT_LIT:
        hexpr = allocExpr(HIR_FLOAT_LIT, node->span);
        hexpr->type = infer_type ? infer_type : newUntyped(UK_FLOAT);
        hexpr->ir_Float.value = node->an_Float.value;
        break;
    case AST_BOOL_LIT:
        hexpr = allocExpr(HIR_BOOL_LIT, node->span);
        hexpr->type = &prim_bool_type;
        hexpr->ir_Bool.value = node->an_Bool.value;
        break;
    case AST_RUNE_LIT:
        hexpr = allocExpr(HIR_NUM_LIT, node->span);
        hexpr->type = &prim_i32_type;
        hexpr->ir_Num.value = (uint64_t)node->an_Rune.value;
        break;
    case AST_STRING_LIT:
        hexpr = allocExpr(HIR_STRING_LIT, node->span);
        hexpr->type = &prim_string_type;
        hexpr->ir_String.value = node->an_String.value;
        break;
    case AST_NULL:
        hexpr = allocExpr(HIR_NULL, node->span);
        if (infer_type) {
            hexpr->type = infer_type;
        } else {
            hexpr->type = newUntyped(UK_NULL);
            null_spans.emplace_back(hexpr->type, hexpr->span);
        }
        break;
    case AST_MACRO_SIZEOF: {
        auto* type = checkTypeLabel(node->an_Macro.arg, true);
        
        hexpr = allocExpr(HIR_MACRO_SIZEOF, node->span);
        hexpr->type = platform_int_type;
        hexpr->ir_TypeMacro.arg = type;
    } break;
    case AST_MACRO_ALIGNOF: {
        auto* type = checkTypeLabel(node->an_Macro.arg, true);
        
        hexpr = allocExpr(HIR_MACRO_ALIGNOF, node->span);
        hexpr->type = platform_int_type;
        hexpr->ir_TypeMacro.arg = type;
    } break;
    default:
        Panic("expr checking is not implemented for {}", (int)node->kind);
        return nullptr;
    }

    return hexpr;
}

HirExpr* Checker::checkCall(AstNode* node) {
    markNonComptime(node->span);

    auto* callee = node->an_Call.func;
    HirExpr* hcallee = nullptr;
    if (callee->kind == AST_IDENT) { // name()
        auto [symbol, dep] = mustLookup(callee->an_Ident.name, callee->span);
        if (dep != nullptr) {
            fatal(callee->span, "cannot use a module as a value");
        }

        if (symbol->flags & SYM_TYPE) { // Type()
            return checkFactoryCall(node->span, symbol->type, node->an_Call.args);
        }

        hcallee = checkValueSymbol(symbol, callee->span);
    } else if (callee->kind == AST_SELECTOR) { // [expr].name()
        auto* root = callee->an_Sel.expr;
        HirExpr* hroot = nullptr;
        if (root->kind == AST_IDENT) { // name1.name2()
            auto [symbol, dep] = mustLookup(root->an_Ident.name, root->span);
            if (dep != nullptr) { // module.name()
                symbol = mustFindSymbolInDep(*dep, callee->an_Sel.field_name, callee->span);

                if (symbol->flags & SYM_TYPE) { // module.Type()
                    return checkFactoryCall(node->span, symbol->type, node->an_Call.args);
                }

                hcallee = checkStaticGet(dep->id, symbol, dep->mod->name, callee->span);
                goto normal_func;
            } else if (symbol->flags & SYM_TYPE) { // Type.name()
                hcallee = checkEnumLit(callee, symbol->type);
                goto normal_func;
            } else { // value.name()
                hroot = checkValueSymbol(symbol, root->span);
            }
        } else { // [expr].name()
            hroot = checkExpr(root);
        }

        auto [hexpr, method] = checkFieldOrMethod(hroot, callee->an_Sel.field_name, callee->span);
        if (hexpr == nullptr) { // Method call
            auto hargs = checkArgs(node->span, method->signature, node->an_Call.args);

            auto* hmcall = allocExpr(HIR_CALL_METHOD, node->span);
            hmcall->type = method->signature->ty_Func.return_type;
            hmcall->ir_CallMethod.self = hroot;
            hmcall->ir_CallMethod.method = method;
            hmcall->ir_CallMethod.args = hargs;
            // TODO: heap allocation
            hmcall->ir_CallMethod.alloc_mode = enclosing_return_type ? HIRMEM_STACK : HIRMEM_GLOBAL;
            return hmcall;
        }

        hcallee = hexpr;
    } else {
        hcallee = checkExpr(callee);
    }
    
normal_func:
    auto* func_type = hcallee->type->Inner();
    if (func_type->kind != TYPE_FUNC) {
        fatal(hcallee->span, "{} is not callable", hcallee->type->ToString());
    }

    auto hargs = checkArgs(node->span, func_type, node->an_Call.args);

    auto* hcall = allocExpr(HIR_CALL, node->span);
    hcall->type = func_type->ty_Func.return_type;
    hcall->ir_Call.func = hcallee;
    hcall->ir_Call.args = hargs;
    // TODO: heap allocation
    hcall->ir_Call.alloc_mode = enclosing_return_type ? HIRMEM_STACK : HIRMEM_GLOBAL;
    return hcall;
}

HirExpr* Checker::checkFactoryCall(const TextSpan& span, Type* type, std::span<AstNode*> args) {
    auto* factory_func = type->ty_Named.factory;
    if (factory_func == nullptr) {
        fatal(span, "type {} has no factory function", type->ToString());
    }

    if (factory_func->parent_id != mod.id) {
        if (factory_func->exported) {
            for (auto& dep : mod.deps) { // Add usage ID
                if (dep.mod->id == factory_func->parent_id) {
                    dep.usages.insert(factory_func->decl_number);
                    break;
                }
            }
        } else {
            fatal(span, "factory function {}() is not exported", type->ToString());
        }
    } else if (comptime_depth > 0) { // Do we even need this check?
        init_graph[curr_decl_number].push_back(factory_func->decl_number);
    }

    auto hargs = checkArgs(span, factory_func->signature, args);

    auto* hfcall = allocExpr(HIR_CALL_FACTORY, span);
    hfcall->type = factory_func->signature->ty_Func.return_type;
    hfcall->ir_CallFactory.func = factory_func;
    hfcall->ir_CallFactory.args = hargs;
    // TODO: heap allocation
    hfcall->ir_CallFactory.alloc_mode = enclosing_return_type ? HIRMEM_STACK : HIRMEM_GLOBAL;
    return hfcall;
}

std::span<HirExpr*> Checker::checkArgs(const TextSpan& span, Type* func_type, std::span<AstNode*>& args) {
    auto fparams = func_type->ty_Func.param_types;
    if (args.size() != fparams.size()) {
        fatal(span, "function expects {} arguments by got {}", fparams.size(), args.size());
    }

    std::vector<HirExpr*> hargs;
    for (size_t i = 0; i < args.size(); i++) {
        auto* harg = checkExpr(args[i], fparams[i]);
        harg = subtypeCast(harg, fparams[i]);

        hargs.push_back(harg);
    }

    return arena.MoveVec(std::move(hargs));
}

HirExpr* Checker::checkSelector(AstNode* node, Type* infer_type) {
    HirExpr* root_expr;

    auto* aroot = node->an_Sel.expr;
    switch (aroot->kind) {
    case AST_IDENT: { // name1.name2
        auto [symbol, dep] = mustLookup(aroot->an_Ident.name, aroot->span);
        if (dep != nullptr) { // module.name
            auto* imported_symbol = mustFindSymbolInDep(*dep, node->an_Sel.field_name, node->span);
            return checkStaticGet(dep->id, imported_symbol, dep->mod->name, node->span);
        }

        if (symbol->flags & SYM_TYPE) { // Type.name2
            maybeExpandComptime(symbol);

            return checkEnumLit(node, symbol->type);
        }

        // Default to field access.
        root_expr = checkValueSymbol(symbol, aroot->span);
    } break;
    case AST_SELECTOR: { // [expr].name1.name2
        auto* asubroot = aroot->an_Sel.expr;

        if (asubroot->kind == AST_IDENT) { // name1.name2.name3
            auto [symbol, dep] = mustLookup(asubroot->an_Ident.name, asubroot->span);
            if (dep != nullptr) { // module.name1.name2
                auto* imported_symbol = mustFindSymbolInDep(*dep, aroot->an_Sel.field_name, aroot->span);

                if (imported_symbol->flags & SYM_TYPE) { // module.Type.name
                    return checkEnumLit(node, imported_symbol->type);
                }

                root_expr = checkStaticGet(dep->id, imported_symbol, dep->mod->name, aroot->span);
            } else if (symbol->flags & SYM_TYPE) { // Type.name1.name2
                maybeExpandComptime(symbol);

                root_expr = checkEnumLit(aroot, symbol->type);
            } else { // value.name1.name2
                root_expr = checkValueSymbol(symbol, asubroot->span);
                root_expr = checkField(root_expr, aroot->an_Sel.field_name, aroot->span);
            }
        } else { 
            // Default to field access.
            root_expr = checkExpr(aroot);
        }
    } break;
    case AST_DOT: // .name
        if (infer_type) {
            return checkEnumLit(node, infer_type);
        } else {
            fatal(node->span, "cannot infer type of enum literal");
        }
        break;
    default:
        root_expr = checkExpr(node->an_Sel.expr);
        break;
    }


    return checkField(root_expr, node->an_Sel.field_name, node->span);
}

HirExpr* Checker::checkField(HirExpr* root, std::string_view field_name, const TextSpan& span) {
    auto [hexpr, method] = checkFieldOrMethod(root, field_name, span);
    if (hexpr == nullptr) {
        // TODO: method references
        fatal(span, "cannot use method as a value");
    }

    return hexpr;
}

std::pair<HirExpr*, Method*> Checker::checkFieldOrMethod(HirExpr* root, std::string_view field_name, const TextSpan& span) {
    Type* display_type = root->type;
    auto* root_type = display_type->Inner();

    bool is_auto_deref = false;
    if (root_type->kind == TYPE_PTR) {
        is_auto_deref = true;
        markNonComptime(span);

        display_type = root_type->ty_Ptr.elem_type;
        root_type = display_type->FullUnwrap();
    } else {
        root_type = root_type->FullUnwrap();
    }

    Type* result_type = nullptr;
    size_t field_index = 0;
    bool assignable = is_auto_deref || root->assignable;
    switch (root_type->kind) {
    case TYPE_ARRAY:
    case TYPE_SLICE:
    case TYPE_STRING:
        if (field_name == "_ptr") {
            result_type = allocType(TYPE_PTR);

            // Should be ok for both ty_Array and ty_Slice.
            result_type->ty_Ptr.elem_type = root_type->ty_Slice.elem_type;
            field_index = 0;
        } else if (field_name == "_len") {
            result_type = platform_int_type;
            field_index = 1;

            assignable &= root_type->kind != TYPE_ARRAY;
        }
        break;
    case TYPE_FUNC:
        if (field_name == "_addr") {
            result_type = &prim_ptr_u8_type;

            field_index = 0;
            assignable = false;
        }
        break;
    case TYPE_STRUCT: {
        auto maybe_field_index = root_type->ty_Struct.name_map.try_get(field_name);
        if (maybe_field_index) {
            field_index = maybe_field_index.value();

            auto& field = root_type->ty_Struct.fields[field_index];
            if (field.exported || display_type->ty_Named.mod_id == mod.id) {
                result_type = field.type;
            } else {
                fatal(span, "field {} of {} is not exported", field_name, display_type->ToString());
            }
        }
    } break;
    }

    if (result_type == nullptr) { // Try to do a method lookup.
        auto* method = tryLookupMethod(span, display_type, field_name);
        if (method != nullptr) {
            return { nullptr, method };
        }

        fatal(span, "type {} has no field or method named {}", display_type->ToString(), field_name);
    }

    auto* hexpr = allocExpr(is_auto_deref ? HIR_DEREF_FIELD : HIR_FIELD, span);
    hexpr->type = result_type;
    hexpr->assignable = assignable;
    hexpr->ir_Field.expr = root;
    hexpr->ir_Field.field_index = field_index;
    return { hexpr, nullptr };
}

HirExpr* Checker::checkEnumLit(AstNode* node, Type* type) {
    auto* enum_type = type->FullUnwrap();
    if (enum_type->kind != TYPE_ENUM) {
        fatal(node->span, "{} is not an enum type", type->ToString());
    }

    auto maybe_tag_value = enum_type->ty_Enum.tag_map.try_get(node->an_Sel.field_name);
    if (!maybe_tag_value) {
        fatal(node->span, "enum {} has no variant named {}", type->ToString(), node->an_Sel.field_name);
    }

    auto* hexpr = allocExpr(HIR_ENUM_LIT, node->span);
    hexpr->type = type;
    hexpr->ir_EnumLit.tag_value = maybe_tag_value.value();
    return hexpr;
}

HirExpr* Checker::checkStaticGet(size_t dep_id, Symbol* imported_symbol, std::string_view mod_name, const TextSpan& span) {
    if (imported_symbol->flags & SYM_TYPE) {
        fatal(span, "cannot use a type as a value");
    }

    if ((imported_symbol->flags & SYM_COMPTIME) == 0) {
        if (comptime_depth > 0) {
            fatal(span, "value of {}.{} cannot be determined at compile time", mod_name, imported_symbol->name);
        } else {
            is_comptime_expr = false;
        }
    } 

    auto* hexpr = allocExpr(HIR_STATIC_GET, span);
    hexpr->type = imported_symbol->type;
    hexpr->assignable = !imported_symbol->immut;
    hexpr->ir_StaticGet.imported_symbol = imported_symbol;
    hexpr->ir_StaticGet.dep_id = dep_id;
    return hexpr;
}

HirExpr* Checker::checkNewArray(AstNode* node) {
    markNonComptime(node->span);

    auto* elem_type = checkTypeLabel(node->an_NewArray.type, true);

    is_comptime_expr = true;
    auto* hlen = checkExpr(node->an_NewArray.len, platform_int_type);
    finishExpr();

    uint64_t const_len = 0;
    if (is_comptime_expr) {
        if (evalComptimeSizeValue(hlen, &const_len)) {
            if (const_len == 0) {
                fatal(hlen->span, "array size must be greater than zero");
            }
        } else {
            fatal(hlen->span, "array size must be greater than zero");
        }
    }

    is_comptime_expr = false;

    auto* slice_type = allocType(TYPE_SLICE);
    slice_type->ty_Slice.elem_type = elem_type;

    auto* hexpr = allocExpr(HIR_NEW_ARRAY, node->span);
    hexpr->type = slice_type;
    hexpr->ir_NewArray.len = hlen;
    hexpr->ir_NewArray.const_len = const_len;
    // TODO: revise for automatic allocation
    hexpr->ir_NewArray.alloc_mode = enclosing_return_type ? HIRMEM_STACK : HIRMEM_GLOBAL;
    return hexpr;
}

HirExpr* Checker::checkNewStruct(AstNode* node, Type* infer_type) {
    auto* atype = node->an_StructLit.type;

    Type* elem_type;
    if (atype->kind == AST_DOT) {
        if (infer_type) {
            infer_type = infer_type->Inner();
            if (infer_type->kind == TYPE_PTR) {
                elem_type = infer_type->ty_Ptr.elem_type;
            } else {
                fatal(atype->span, "cannot infer type of struct literal");
            }
        } else {
            fatal(atype->span, "cannot infer type of struct literal");
        }
    } else {
        elem_type = checkTypeLabel(atype, true);
    }

    auto* struct_type = elem_type->FullUnwrap();
    if (struct_type->kind != TYPE_STRUCT) {
        fatal(atype->span, "{} is not a struct type", elem_type->ToString());
    }

    auto field_inits = checkFieldInits(node->an_StructLit.field_inits, struct_type, elem_type);

    Type* ptr_type;
    if (infer_type) {
        ptr_type = infer_type;
    } else {
        ptr_type = allocType(TYPE_PTR);
        ptr_type->ty_Ptr.elem_type = elem_type;
    }

    auto* hexpr = allocExpr(HIR_NEW_STRUCT, node->span);
    hexpr->type = ptr_type;
    hexpr->ir_StructLit.field_inits = arena.MoveVec(std::move(field_inits));
    // TODO: heap allocation
    hexpr->ir_StructLit.alloc_mode = enclosing_return_type ? HIRMEM_STACK : HIRMEM_GLOBAL;
    return hexpr;
}

HirExpr* Checker::checkArrayLit(AstNode* node, Type* infer_type) {
    Type* elem_infer_type = nullptr;
    if (infer_type) {
        infer_type = infer_type->Inner();

        if (infer_type->kind == TYPE_ARRAY)
            elem_infer_type = infer_type->ty_Array.elem_type;
        else if (infer_type->kind == TYPE_SLICE)
            elem_infer_type = infer_type->ty_Slice.elem_type;
    }

    auto& aitems = node->an_ExprList.exprs;
    std::vector<HirExpr*> items;
    for (auto* aitem : aitems) {
        items.push_back(checkExpr(aitem, elem_infer_type));
    }

    auto* first_type = items[0]->type;
    for (size_t i = 1; i < items.size(); i++) {
        mustEqual(items[i]->span, first_type, items[i]->type);
    }

    Type* arr_type;
    if (infer_type && infer_type->kind == TYPE_ARRAY) {
        arr_type = allocType(TYPE_ARRAY);
        arr_type->ty_Array.elem_type = first_type;
        arr_type->ty_Array.len = (uint64_t)items.size();
    } else {
        arr_type = allocType(TYPE_SLICE);
        arr_type->ty_Slice.elem_type = first_type;
    }
    

    auto* hexpr = allocExpr(HIR_ARRAY_LIT, node->span);
    hexpr->type = arr_type;
    hexpr->ir_ArrayLit.items = arena.MoveVec(std::move(items));
    // TODO: heap allocation
    hexpr->ir_ArrayLit.alloc_mode = enclosing_return_type ? HIRMEM_STACK : HIRMEM_GLOBAL;
    return hexpr;
}

HirExpr* Checker::checkStructLit(AstNode* node, Type* infer_type) {
    auto* atype = node->an_StructLit.type;

    Type* type;
    if (atype->kind == AST_DOT) {
        if (infer_type) {
            type = infer_type;
        } else {
            fatal(atype->span, "cannot infer type of struct literal");
        }
    } else {
        type = checkTypeLabel(atype, true);
    }

    auto* struct_type = type->FullUnwrap();
    if (struct_type->kind != TYPE_STRUCT) {
        fatal(atype->span, "{} is not a struct type", type->ToString());
    }

    auto field_inits = checkFieldInits(node->an_StructLit.field_inits, struct_type, type);

    auto* hexpr = allocExpr(HIR_STRUCT_LIT, node->span);
    hexpr->type = type;
    hexpr->ir_StructLit.field_inits = arena.MoveVec(std::move(field_inits));
    return hexpr;
}

std::vector<HirFieldInit> Checker::checkFieldInits(std::span<AstNode*> afield_inits, Type* struct_type, Type* display_type) {
    std::vector<HirFieldInit> field_inits;

    size_t field_index = 0;
    for (auto* ainit : afield_inits) {
        if (ainit->kind == AST_NAMED_INIT) {
            auto maybe_field_index = struct_type->ty_Struct.name_map.try_get(ainit->an_NamedInit.name);
            if (!maybe_field_index) {
                error(ainit->span, "struct {} has no field named {}", display_type->ToString(), ainit->an_NamedInit.name);
                continue;
            }

            field_index = maybe_field_index.value();
            ainit = ainit->an_NamedInit.init;
        } else {
            if (field_index > struct_type->ty_Struct.fields.size()) {
                error(
                    ainit->span, 
                    "struct has {} fields but {} values are specified", 
                    struct_type->ty_Struct.fields.size(), 
                    afield_inits.size()
                );
                break;
            }
        }
        
        auto* field_type = struct_type->ty_Struct.fields[field_index].type;
        auto* hinit = checkExpr(ainit, field_type);
        hinit = subtypeCast(hinit, field_type);

        field_inits.emplace_back(hinit, field_index);
        field_index++;
    }

    return field_inits;
}

HirExpr* Checker::checkValueSymbol(Symbol* symbol, const TextSpan& span) {
    if (symbol->flags & SYM_COMPTIME) {
        maybeExpandComptime(symbol);
    } else if (comptime_depth > 0) {
        fatal(span, "value of {} cannot be determined at compile time", symbol->name);
    } else {
        is_comptime_expr = false;
        init_graph[curr_decl_number].push_back(symbol->decl_number);
    }

    auto* hexpr = allocExpr(HIR_IDENT, span);
    hexpr->type = symbol->type;
    hexpr->assignable = !symbol->immut;
    hexpr->ir_Ident.symbol = symbol;
    return hexpr;
}

/* -------------------------------------------------------------------------- */

void Checker::markNonComptime(const TextSpan& span) {
    if (comptime_depth > 0) {
        fatal(span, "expression cannot be evaluated at compile time");
    } else {
        is_comptime_expr = false;
    }
}

void Checker::maybeExpandComptime(Symbol* symbol) {
    if (first_pass && comptime_depth > 0) {
        if (symbol->type == nullptr) {
            // Recursively expand comptime values.
            Assert(symbol->parent_id == mod.id, "comptime is undetermined after module checking is completed");
            checkDecl(mod.unsorted_decls[symbol->decl_number]);
        }
    }
}