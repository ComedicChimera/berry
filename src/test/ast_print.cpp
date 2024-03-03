#include "ast_print.hpp"

#include <iostream>

static std::string spanToStr(const TextSpan& span) {
    return std::format(
        "Span({}, {} to {}, {})",
        span.start_line, span.start_col, 
        span.end_line, span.end_col
    );
}

static std::string typeToStr(Type* type) {
    if (type == nullptr) {
        return "<undef>";
    } else {
        return type->ToString();
    }
}

static std::string boolStr(bool b) {
    return b ? "true" : "false";
}

static void printMetadata(std::span<MetadataTag> meta) {
    std::cout << '[';

    int i = 0;
    for (auto& tag : meta) {
        if (i > 0) {
            std::cout << ", ";
        }

        if (tag.value.size() > 0) {
            std::cout << std::format(
                "Metadata({}, {}, {}, {})", 
                tag.name, spanToStr(tag.name_span),
                tag.value, spanToStr(tag.value_span)
            );
        } else {
            std::cout << std::format("Metadata({}, {})", tag.name, spanToStr(tag.name_span));
        }

        i++;
    }

    std::cout << ']';
}

static void printAstOp(AstOpKind op) {
    switch (op) {
    case AOP_ADD:
        std::cout << "ADD";
        break;
    case AOP_SUB:
        std::cout << "SUB";
        break;
    case AOP_MUL:
        std::cout << "MUL";
        break;
    case AOP_DIV:
        std::cout << "DIV";
        break;
    case AOP_MOD:
        std::cout << "MOD";
        break;
    case AOP_SHL:
        std::cout << "SHL";
        break;
    case AOP_SHR:
        std::cout << "SHR";
        break;
    case AOP_EQ:
        std::cout << "EQ";
        break;
    case AOP_NE:
        std::cout << "NE";
        break;
    case AOP_LT:
        std::cout << "LT";
        break;
    case AOP_GT:
        std::cout << "GT";
        break;
    case AOP_LE:
        std::cout << "LE";
        break;
    case AOP_GE:
        std::cout << "GE";
        break;
    case AOP_BWAND:
        std::cout << "BWAND";
        break;
    case AOP_BWOR:
        std::cout << "BWOR";
        break;
    case AOP_BWXOR:
        std::cout << "BWXOR";
        break;
    case AOP_LGAND:
        std::cout << "LGAND";
        break;
    case AOP_LGOR:
        std::cout << "LGOR";
        break;
    case AOP_NEG:
        std::cout << "NEG";
        break;
    case AOP_NOT:
        std::cout << "NOT";
        break;
    case AOP_NONE:
        std::cout << "NONE";
        break;
    default:
        Panic("unsupported AST operator: {}", (int)op);
        break;
    }
}

/* -------------------------------------------------------------------------- */

static void printExpr(AstExpr* node);

static void printBinop(AstExpr* node) {
    std::cout << std::format("BinaryOp(span={}, type={}, aop=", spanToStr(node->span), typeToStr(node->type));

    printAstOp(node->an_Binop.op);

    std::cout << ", lhs=";
    printExpr(node->an_Binop.lhs);

    std::cout << ", rhs=";
    printExpr(node->an_Binop.rhs);

    std::cout << ')';
}

static void printUnop(AstExpr* node) {
    std::cout << std::format("UnaryOp(span={}, type={}, aop=", spanToStr(node->span), typeToStr(node->type));
    
    printAstOp(node->an_Unop.op);

    std::cout << ", operand=";
    printExpr(node->an_Unop.operand);
    std::cout << ')';
}

static void printCall(AstExpr* node) {
    std::cout << std::format("Call(span={}, type={}, func=", spanToStr(node->span), typeToStr(node->type));

    printExpr(node->an_Call.func);

    std::cout << ", args = [";

    for (int i = 0; i < node->an_Call.args.size(); i++) {
        if (i > 0) {
            std::cout << ", ";
        }

        printExpr(node->an_Call.args[i]);
    }

    std::cout << "])";
}

static void printIndex(AstExpr* node) {
    std::cout << std::format("Index(span={}, type={}, array=", spanToStr(node->span), typeToStr(node->type));

    printExpr(node->an_Index.array);

    std::cout << ", index=";
    printExpr(node->an_Index.index);

    std::cout << ')';
}

static void printSlice(AstExpr* node) {
    std::cout << std::format("Slice(span={}, type={}, array=", spanToStr(node->span), typeToStr(node->type));

    printExpr(node->an_Slice.array);

    std::cout << ", start_index=";
    printExpr(node->an_Slice.start_index);

    std::cout << ", end_index=";
    printExpr(node->an_Slice.end_index);

    std::cout << ')';
}

static void printArrayLit(AstExpr* node) {
    std::cout << std::format("ArrayLit(span={}, type={}, content=[", spanToStr(node->span), typeToStr(node->type));

    for (int i = 0; i < node->an_Array.elems.size(); i++) {
        if (i > 0) {
            std::cout << ", ";
        }

        printExpr(node->an_Array.elems[i]);
    }

    std::cout << "])";
}

static void printStructLitPos(AstExpr* node) {
    std::cout << std::format("{}(span={}, type={}, field_inits=[", 
        node->kind == AST_STRUCT_LIT_POS ? "StructLit" : "StructPtrLit",
        spanToStr(node->span), typeToStr(node->type)
    );


    int i = 0;
    for (auto* field_init : node->an_StructLitPos.field_values) {
        if (i > 0) {
            std::cout << ", ";
        }

        printExpr(field_init);

        i++;
    }

    std::cout << "])";
}

static void printStructLitNamed(AstExpr* node) {
    std::cout << std::format("{}(span={}, type={}, field_inits=[", 
        node->kind == AST_STRUCT_LIT_NAMED ? "StructLit" : "StructPtrLit",
        spanToStr(node->span), typeToStr(node->type)
    );

    int i = 0;
    for (auto& field_pair : node->an_StructLitNamed.field_values) {
        if (i > 0) {
            std::cout << ", ";
        }

        std::cout << std::format("NamedField(name={}, type={}, init=", field_pair.first->an_Ident.temp_name, typeToStr(field_pair.second->type));
        printExpr(field_pair.second);
        std::cout << ')';

        i++;
    }

    std::cout << "])";
}

static void printExpr(AstExpr* node) {
    if (node == nullptr) {
        std::cout << "<empty>";
        return;
    }

    switch (node->kind) {
    case AST_CAST:
        std::cout << std::format("Cast(span={}, type={}, src=", spanToStr(node->span), typeToStr(node->type));
        printExpr(node->an_Cast.src);
        std::cout << ')';
        break;
    case AST_BINOP:
        printBinop(node);
        break;
    case AST_UNOP:
        printUnop(node);
        break;
    case AST_ADDR:
        std::cout << std::format("AddrOf(span={}, type={}, elem=", spanToStr(node->span), typeToStr(node->type));
        printExpr(node->an_Addr.elem);
        std::cout << ')';
        break;
    case AST_DEREF:
        std::cout << std::format("Deref(span={}, type={}, ptr=", spanToStr(node->span), typeToStr(node->type));
        printExpr(node->an_Deref.ptr);
        std::cout << ')';
        break;
    case AST_CALL:
        printCall(node);
        break;
    case AST_INDEX:
        printIndex(node);
        break;
    case AST_SLICE:
        printSlice(node);
        break;
    case AST_FIELD:
        std::cout << std::format("FieldAccess(span={}, type={}, root=", spanToStr(node->span), typeToStr(node->type));
        printExpr(node->an_Field.root);

        std::cout << std::format(", field_name={})", node->an_Field.field_name);
        break;
    case AST_ARRAY:
        printArrayLit(node);
        break;
    case AST_NEW:
        std::cout << std::format(
            "New(span={}, type={}, elem_type={}, size_expr=",
            spanToStr(node->span),
            typeToStr(node->type),
            typeToStr(node->an_New.elem_type)
        );
        printExpr(node->an_New.size_expr);
        std::cout << ')';
        break;
    case AST_STRUCT_LIT_NAMED:
    case AST_STRUCT_PTR_LIT_NAMED:
        printStructLitNamed(node);
        break;
    case AST_STRUCT_LIT_POS:
    case AST_STRUCT_PTR_LIT_POS:
        printStructLitPos(node);
        break;
    case AST_STRUCT_LIT_TYPE:
        Panic("print struct lit type called directly");
        break;
    case AST_IDENT:
        std::cout << std::format(
            "Identifier(span={}, type={}, name={})",
            spanToStr(node->span),
            typeToStr(node->type),
            node->an_Ident.symbol ? node->an_Ident.symbol->name : node->an_Ident.temp_name
        );
        break;
    case AST_INT:
        std::cout << std::format(
            "IntLit(span={}, type={}, value={})",
            spanToStr(node->span),
            typeToStr(node->type),
            node->an_Int.value
        );
        break;
    case AST_FLOAT:
        std::cout << std::format(
            "FloatLit(span={}, type={}, value={})",
            spanToStr(node->span),
            typeToStr(node->type),
            node->an_Float.value
        );
        break;
    case AST_BOOL:
        std::cout << std::format(
            "BoolLit(span={}, type={}, value={})",
            spanToStr(node->span),
            typeToStr(node->type),
            node->an_Bool.value
        );
        break;
    case AST_NULL:
        std::cout << std::format("Null(span={}, type={})", spanToStr(node->span), typeToStr(node->type));
        break;
    case AST_STR:
        std::cout << std::format(
            "StringLit(span={}, type={}, value=\"{}\")",
            spanToStr(node->span),
            typeToStr(node->type),
            node->an_String.value
        );
        break;
    default:
        Panic("ast printing is not implemented for {}", (int)node->kind);
    }
}

/* -------------------------------------------------------------------------- */

static void printStmt(AstStmt* node);

static void printBlock(AstStmt* node) {
    std::cout << std::format("Block(span={}, stmts=[", spanToStr(node->span));

    for (int i = 0; i < node->an_Block.stmts.size(); i++) {
        if (i > 0) {
            std::cout << ", ";
        }

        printStmt(node->an_Block.stmts[i]);
    }

    std::cout << "])";
}

static void printIf(AstStmt* node) {
    std::cout << std::format("IfTree(span={}, branches=[", spanToStr(node->span));
    for (int i = 0; i < node->an_If.branches.size(); i++) {
        if (i > 0)
            std::cout << ", ";

        auto& branch = node->an_If.branches[i];

        std::cout << std::format("CondBranch(span={}, condition=", spanToStr(branch.span));
        printExpr(branch.cond_expr);

        std::cout << ", body=";
        printStmt(branch.body);

        std::cout << ')';
    }

    std::cout << "], else=";
    printStmt(node->an_If.else_block);

    std::cout << ')';
}

static void printWhile(AstStmt* node) {
    std::cout << std::format(
        "{}(span={}, condition=", 
        node->an_While.is_do_while ? "DoWhileLoop" : "WhileLoop", 
        spanToStr(node->span)
    );

    printExpr(node->an_While.cond_expr);

    std::cout << ", body=";
    printStmt(node->an_While.body);

    std::cout << ", else=";
    printStmt(node->an_While.else_block);

    std::cout << ')';
}

static void printFor(AstStmt* node) {
    std::cout << std::format("ForLoop(span={}, var_def=", spanToStr(node->span));

    printStmt(node->an_For.var_def);

    std::cout << ", condition=";
    printExpr(node->an_For.cond_expr);

    std::cout << ", update_stmt=";
    printStmt(node->an_For.update_stmt);

    std::cout << ", body=";
    printStmt(node->an_For.body);

    std::cout << ", else=";
    printStmt(node->an_For.else_block);

    std::cout << ')';
}

static void printLocalVar(AstStmt* node) {
    std::cout << std::format("LocalVarDef(span={}, ", spanToStr(node->span));

    auto* symbol = node->an_LocalVar.symbol;
    std::cout << std::format("type={}, name={}, init=", typeToStr(symbol->type), symbol->name);
    printExpr(node->an_LocalVar.init);

    std::cout << ')';
}

static void printAssign(AstStmt* node) {
    std::cout << std::format("Assign(span={}, lhs=", spanToStr(node->span));
    printExpr(node->an_Assign.lhs);
    std::cout << ", rhs=";
    printExpr(node->an_Assign.rhs);
    std::cout << ", assign_op=";
    printAstOp(node->an_Assign.assign_op);
    std::cout << ')';
}

static void printIncDec(AstStmt* node) {
    std::cout << std::format("IncDec(span={}, lhs=", spanToStr(node->span));
    printExpr(node->an_IncDec.lhs);
    std::cout << ", op=";
    printAstOp(node->an_IncDec.op);
    std::cout << ')';
}

static void printStmt(AstStmt* stmt) {
    if (stmt == nullptr) {
        std::cout << "<empty>";
        return;
    }

    switch (stmt->kind) {
    case AST_BLOCK:
        printBlock(stmt);
        break;
    case AST_IF:
        printIf(stmt);
        break;
    case AST_WHILE:
        printWhile(stmt);
        break;
    case AST_FOR:
        printFor(stmt);
        break;
    case AST_LOCAL_VAR:
        printLocalVar(stmt);
        break;
    case AST_ASSIGN:
        printAssign(stmt);
        break;
    case AST_INCDEC:
        printIncDec(stmt);
        break;
    case AST_EXPR_STMT:
        printExpr(stmt->an_ExprStmt.expr);
        break;
    case AST_RETURN:
        std::cout << std::format("Return(span={}, value=", spanToStr(stmt->span));
        printExpr(stmt->an_Return.value);

        std::cout << ')';
        break;
    case AST_BREAK:
        std::cout << std::format("Break(span={})", spanToStr(stmt->span));
        break;
    case AST_CONTINUE:
        std::cout << std::format("Continue(span={})", spanToStr(stmt->span));
        break;
    default:
        Panic("ast printing not implemented for {}", (int)stmt->kind);
    }
}

/* -------------------------------------------------------------------------- */

static void printFunc(AstDef* node) {
    std::cout << std::format("FuncDef(span={}, meta=", spanToStr(node->span));
    printMetadata(node->metadata);

    auto& fn = node->an_Func;

    std::cout << ", name=" << fn.symbol->name << ", type=" << typeToStr(fn.symbol->type) << ", params=[";

    for (int i = 0; i < fn.params.size(); i++) {
        if (i > 0) {
            std::cout << ", ";
        }

        std::cout << std::format(
            "Param(span={}, type={}, name={})", 
            spanToStr(fn.params[i]->span), 
            typeToStr(fn.params[i]->type),
            fn.params[i]->name
        );
    }

    std::cout << "], body=";
    printStmt(fn.body);

    std::cout << ')';
}

static void printGlobalVar(AstDef* node) {
    std::cout << std::format("GlobalVarDef(span={}, meta=", spanToStr(node->span));
    printMetadata(node->metadata);

    auto* symbol = node->an_GlobalVar.symbol;
    std::cout << std::format("type={}, name={}, init=", typeToStr(symbol->type), symbol->name);
    printExpr(node->an_GlobalVar.init);

    std::cout << ')';
}

static void printStructDef(AstDef* node) {
    std::cout << std::format(
        "StructDef(span={}, name={}, type={}, field_attrs=[])", 
        spanToStr(node->span), 
        node->an_StructDef.symbol->name, 
        typeToStr(node->an_StructDef.symbol->type)
    );

    // TODO: print field attrs
}   

static void printDef(AstDef* def) {
    switch (def->kind) {
    case AST_FUNC:
        printFunc(def);
        break;
    case AST_GLOBAL_VAR:
        printGlobalVar(def);
        break;
    case AST_STRUCT_DEF:
        printStructDef(def);
        break;
    default:
         Panic("ast printing not implemented for {}", (int)def->kind);
    }
}

/* -------------------------------------------------------------------------- */

void PrintAst(const SourceFile& src_file) {
    for (auto* def : src_file.defs) {
        printDef(def);

        std::cout << "\n\n";
    }
}
