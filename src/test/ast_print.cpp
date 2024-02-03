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
    if (node.body) {
        node.body->Accept(this);
    } else {
        std::cout << "<empty>";
    }

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

        node.stmts[i]->Accept(this);
    }

    std::cout << "])";
}

void AstPrinter::Visit(AstLocalVarDef& node) {
    std::cout << std::format("LocalVarDef(span={}, type={}, name={}, init=", spanToStr(node.span), typeToStr(node.symbol->type), node.symbol->name);

    if (node.init) {
        node.init->Accept(this);
    } else {
        std::cout << "<empty>";
    }

    std::cout << ')';
}

/* -------------------------------------------------------------------------- */

void AstPrinter::Visit(AstCast& node) {
    std::cout << std::format("Cast(span={}, type={}, src=", spanToStr(node.span), typeToStr(node.type));
    node.src->Accept(this);
    std::cout << ')';
}

void AstPrinter::Visit(AstBinaryOp& node) {
    std::cout << std::format("BinaryOp(span={}, type={}, aop=", spanToStr(node.span), typeToStr(node.type));

    switch (node.op_kind) {
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
    case AOP_BAND: 
        std::cout << "BAND";
        break;
    case AOP_BOR: 
        std::cout << "BOR";
        break;
    case AOP_BXOR: 
        std::cout << "BXOR";
        break;
    }

    std::cout << ", lhs=";
    node.lhs->Accept(this);

    std::cout << ", rhs=";
    node.rhs->Accept(this);

    std::cout << ')';
}

void AstPrinter::Visit(AstUnaryOp& node) {
    std::cout << std::format("UnaryOp(span={}, type={}, aop=", spanToStr(node.span), typeToStr(node.type));
    
    switch (node.op_kind) {
    case AOP_NEG:
        std::cout << "NEG";
        break;
    }

    std::cout << ", operand=";
    node.operand->Accept(this);
    std::cout << ')';
}

void AstPrinter::Visit(AstAddrOf& node) {
    std::cout << std::format("AddrOf(span={}, type={}, elem=", spanToStr(node.span), typeToStr(node.type));
    node.elem->Accept(this);
    std::cout << ", const=" << boolStr(node.is_const) << ')';
}

void AstPrinter::Visit(AstDeref& node) {
    std::cout << std::format("Deref(span={}, type={}, ptr=", spanToStr(node.span), typeToStr(node.type));
    node.ptr->Accept(this);
    std::cout << ')';
}

void AstPrinter::Visit(AstCall& node) {
    std::cout << std::format("Call(span={}, type={}, func=", spanToStr(node.span), typeToStr(node.type));

    node.func->Accept(this);

    std::cout << ", args = [";

    for (int i = 0; i < node.args.size(); i++) {
        if (i > 0) {
            std::cout << ", ";
        }

        node.args[i]->Accept(this);
    }

    std::cout << "])";
}

/* -------------------------------------------------------------------------- */

void AstPrinter::Visit(AstIdent& node) {
    std::cout << std::format(
        "Identifier(span={}, type={}, name={})",
        spanToStr(node.span),
        typeToStr(node.type),
        node.Name()
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

