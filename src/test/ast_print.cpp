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

static void printMetadata(const Metadata& meta) {
    std::cout << '[';

    int i = 0;
    for (auto& pair : meta) {
        if (i > 0) {
            std::cout << ", ";
        }

        if (pair.second.value.size() > 0) {
            std::cout << std::format(
                "Metadata({}, {}, {}, {})", 
                pair.second.name, spanToStr(pair.second.name_span),
                pair.second.value, spanToStr(pair.second.value_span)
            );
        } else {
            std::cout << std::format("Metadata({}, {})", pair.second.name, spanToStr(pair.second.name_span));
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

void AstPrinter::visitOrEmpty(std::unique_ptr<AstNode>& node) {
    if (node) {
        visitNode(node);
    } else {
        std::cout << "<empty>";
    }
}

void AstPrinter::visitOrEmpty(std::unique_ptr<AstExpr>& node) {
    if (node) {
        visitNode(node);
    } else {
        std::cout << "<empty>";
    }
}

/* -------------------------------------------------------------------------- */

void AstPrinter::Visit(AstFuncDef& node) {
    std::cout << std::format("FuncDef(span={}, meta=", spanToStr(node.span));
    printMetadata(node.metadata);

    std::cout << ", name=" << node.symbol->name << ", type=" << typeToStr(node.symbol->type) << ", params=[";

    for (int i = 0; i < node.params.size(); i++) {
        if (i > 0) {
            std::cout << ", ";
        }

        std::cout << std::format(
            "Param(span={}, type={}, name={})", 
            spanToStr(node.params[i]->span), 
            typeToStr(node.params[i]->type),
            node.params[i]->name
        );
    }

    std::cout << "], body=";
    visitOrEmpty(node.body);

    std::cout << ')';
}

void AstPrinter::Visit(AstGlobalVarDef& node) {
    std::cout << std::format("GlobalVarDef(span={}, meta=", spanToStr(node.span));
    printMetadata(node.metadata);

    std::cout << ", var=";
    node.var_def->Accept(this);

    std::cout << ')';
}

/* -------------------------------------------------------------------------- */

void AstPrinter::Visit(AstBlock& node) {
    std::cout << std::format("Block(span={}, stmts=[", spanToStr(node.span));

    for (int i = 0; i < node.stmts.size(); i++) {
        if (i > 0) {
            std::cout << ", ";
        }

        visitNode(node.stmts[i]);
    }

    std::cout << "])";
}

void AstPrinter::Visit(AstCondBranch& node) {
    std::cout << std::format("CondBranch(span={}, condition=", spanToStr(node.span));
    visitNode(node.cond_expr);

    std::cout << ", body=";
    visitNode(node.body);

    std::cout << ')';
}

void AstPrinter::Visit(AstIfTree& node) {
    std::cout << std::format("IfTree(span={}, branches=[", spanToStr(node.span));
    for (int i = 0; i < node.branches.size(); i++) {
        if (i > 0)
            std::cout << ", ";

        node.branches[i].Accept(this);
    }

    std::cout << "], else=";
    visitOrEmpty(node.else_body);

    std::cout << ')';
}

void AstPrinter::Visit(AstWhileLoop& node) {
    std::cout << std::format(
        "{}(span={}, condition=", 
        node.is_do_while ? "DoWhileLoop" : "WhileLoop", 
        spanToStr(node.span)
    );

    visitNode(node.cond_expr);

    std::cout << ", body=";
    visitNode(node.body);

    std::cout << ", else=";
    visitOrEmpty(node.body);

    std::cout << ')';
}

void AstPrinter::Visit(AstForLoop& node) {
    std::cout << std::format("ForLoop(span={}, var_def=", spanToStr(node.span));

    if (node.var_def) {
        node.var_def->Accept(this);
    } else {
        std::cout << "<empty>";
    }

    std::cout << ", condition=";
    visitOrEmpty(node.cond_expr);

    std::cout << ", update_stmt=";
    visitOrEmpty(node.update_stmt);

    std::cout << ", body=";
    visitNode(node.body);

    std::cout << ", else=";
    visitOrEmpty(node.else_clause);

    std::cout << ')';
}

void AstPrinter::Visit(AstIncDec& node) {
    std::cout << std::format("IncDec(span={}, lhs=", spanToStr(node.span));
    visitNode(node.lhs);
    std::cout << ", op=";
    printAstOp(node.op_kind);
    std::cout << ')';
}

void AstPrinter::Visit(AstAssign& node) {
    std::cout << std::format("Assign(span={}, lhs=", spanToStr(node.span));
    visitNode(node.lhs);
    std::cout << ", rhs=";
    visitNode(node.rhs);
    std::cout << ", assign_op=";
    printAstOp(node.assign_op_kind);
    std::cout << ')';
}

void AstPrinter::Visit(AstReturn& node) {
    std::cout << std::format("Return(span={}, value=", spanToStr(node.span));
    visitOrEmpty(node.value);

    std::cout << ')';
}

void AstPrinter::Visit(AstBreak& node) {
    std::cout << std::format("Break(span={})", spanToStr(node.span));
}

void AstPrinter::Visit(AstContinue& node) {
    std::cout << std::format("Continue(span={})", spanToStr(node.span));
}

void AstPrinter::Visit(AstLocalVarDef& node) {
    std::cout << std::format("LocalVarDef(span={}, type={}, name={}, init=", spanToStr(node.span), typeToStr(node.symbol->type), node.symbol->name);

    visitOrEmpty(node.init);

    std::cout << std::format(", array_size={})", node.array_size);
}

/* -------------------------------------------------------------------------- */

void AstPrinter::Visit(AstCast& node) {
    std::cout << std::format("Cast(span={}, type={}, src=", spanToStr(node.span), typeToStr(node.type));
    visitNode(node.src);
    std::cout << ')';
}

void AstPrinter::Visit(AstBinaryOp& node) {
    std::cout << std::format("BinaryOp(span={}, type={}, aop=", spanToStr(node.span), typeToStr(node.type));

    printAstOp(node.op_kind);

    std::cout << ", lhs=";
    visitNode(node.lhs);

    std::cout << ", rhs=";
    visitNode(node.rhs);

    std::cout << ')';
}

void AstPrinter::Visit(AstUnaryOp& node) {
    std::cout << std::format("UnaryOp(span={}, type={}, aop=", spanToStr(node.span), typeToStr(node.type));
    
    printAstOp(node.op_kind);

    std::cout << ", operand=";
    visitNode(node.operand);
    std::cout << ')';
}

void AstPrinter::Visit(AstAddrOf& node) {
    std::cout << std::format("AddrOf(span={}, type={}, elem=", spanToStr(node.span), typeToStr(node.type));
    visitNode(node.elem);
    std::cout << ", const=" << boolStr(node.is_const) << ')';
}

void AstPrinter::Visit(AstDeref& node) {
    std::cout << std::format("Deref(span={}, type={}, ptr=", spanToStr(node.span), typeToStr(node.type));
    visitNode(node.ptr);
    std::cout << ')';
}

void AstPrinter::Visit(AstCall& node) {
    std::cout << std::format("Call(span={}, type={}, func=", spanToStr(node.span), typeToStr(node.type));

    visitNode(node.func);

    std::cout << ", args = [";

    for (int i = 0; i < node.args.size(); i++) {
        if (i > 0) {
            std::cout << ", ";
        }

        visitNode(node.args[i]);
    }

    std::cout << "])";
}

void AstPrinter::Visit(AstIndex& node) {
    std::cout << std::format("Index(span={}, type={}, array=", spanToStr(node.span), typeToStr(node.type));

    visitNode(node.arr);

    std::cout << ", index=";
    visitNode(node.index);

    std::cout << ')';
}

void AstPrinter::Visit(AstSlice& node) {
    std::cout << std::format("Slice(span={}, type={}, array=", spanToStr(node.span), typeToStr(node.type));

    visitNode(node.arr);

    std::cout << ", start_index=";
    visitOrEmpty(node.start_index);

    std::cout << ", end_index=";
    visitOrEmpty(node.end_index);

    std::cout << ')';
}   

void AstPrinter::Visit(AstFieldAccess& node) {
    std::cout << std::format("FieldAccess(span={}, type={}, root=", spanToStr(node.span), typeToStr(node.type));
    visitNode(node.root);

    std::cout << std::format(", field_name={})", node.field_name);
}

/* -------------------------------------------------------------------------- */

void AstPrinter::Visit(AstArrayLit& node) {
    std::cout << std::format("ArrayLit(span={}, type={}, content=[", spanToStr(node.span), typeToStr(node.type));

    for (int i = 0; i < node.elements.size(); i++) {
        if (i > 0) {
            std::cout << ", ";
        }

        visitNode(node.elements[i]);
    }

    std::cout << "])";
}

void AstPrinter::Visit(AstIdent& node) {
    std::cout << std::format(
        "Identifier(span={}, type={}, name={})",
        spanToStr(node.span),
        typeToStr(node.type),
        node.GetName()
    );
}

void AstPrinter::Visit(AstBoolLit& node) {
    std::cout << std::format(
        "BoolLit(span={}, type={}, value={})",
        spanToStr(node.span),
        typeToStr(node.type),
        node.value
    );
}

void AstPrinter::Visit(AstFloatLit& node) {
    std::cout << std::format(
        "FloatLit(span={}, type={}, value={})",
        spanToStr(node.span),
        typeToStr(node.type),
        node.value
    );
}

void AstPrinter::Visit(AstIntLit& node) {
    std::cout << std::format(
        "IntLit(span={}, type={}, value={})",
        spanToStr(node.span),
        typeToStr(node.type),
        node.value
    );
}

void AstPrinter::Visit(AstNullLit& node) {
    std::cout << std::format("Null(span={}, type={})", spanToStr(node.span), typeToStr(node.type));
}

void AstPrinter::Visit(AstStringLit& node) {
    std::cout << std::format(
        "StringLit(span={}, type={}, value=\"{}\")",
        spanToStr(node.span),
        typeToStr(node.type),
        node.value
    );
}