#include "ast.hpp"

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

void AstFuncDef::Print() const {
    std::cout << std::format("FuncDef(span={}, meta=", spanToStr(span));
    printMetadata(metadata);

    std::cout << ", name=" << symbol->name << ", type=" << typeToStr(symbol->type) << ", params=[";

    for (int i = 0; i < params.size(); i++) {
        if (i > 0) {
            std::cout << ", ";
        }

        std::cout << std::format(
            "Param(span={}, type={}, name={})", 
            spanToStr(params[i]->span), 
            typeToStr(params[i]->type),
            params[i]->name
        );
    }

    std::cout << "], body=";
    if (body) {
        body->Print();
    } else {
        std::cout << "<empty>";
    }

    std::cout << ')';
}

void AstGlobalVarDef::Print() const {
    std::cout << std::format("GlobalVarDef(span={}, meta=", spanToStr(span));
    printMetadata(metadata);

    std::cout << ", var=";
    var_def->Print();

    std::cout << ')';
}

/* -------------------------------------------------------------------------- */

void AstBlock::Print() const {
    std::cout << std::format("Block(span={}, stmts=[", spanToStr(span));

    for (int i = 0; i < stmts.size(); i++) {
        if (i > 0) {
            std::cout << ", ";
        }

        stmts[i]->Print();
    }

    std::cout << "])";
}

void AstLocalVarDef::Print() const {
    std::cout << std::format("LocalVarDef(span={}, type={}, name={}, init=", spanToStr(span), typeToStr(symbol->type), symbol->name);

    if (init) {
        init->Print();
    } else {
        std::cout << "<empty>";
    }

    std::cout << ')';
}

/* -------------------------------------------------------------------------- */

void AstCast::Print() const {
    std::cout << std::format("Cast(span={}, type={}, src=", spanToStr(span), typeToStr(type));
    src->Print();
    std::cout << ')';
}

void AstBinaryOp::Print() const {
    std::cout << std::format("BinaryOp(span={}, type={}, aop=", spanToStr(span), typeToStr(type));

    switch (op_kind) {
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
    lhs->Print();

    std::cout << ", rhs=";
    rhs->Print();

    std::cout << ')';
}

void AstUnaryOp::Print() const {
    std::cout << std::format("UnaryOp(span={}, type={}, aop=", spanToStr(span), typeToStr(type));
    
    switch (op_kind) {
    case AOP_NEG:
        std::cout << "NEG";
        break;
    }

    std::cout << ", operand=";
    operand->Print();
    std::cout << ')';
}

void AstAddrOf::Print() const {
    std::cout << std::format("AddrOf(span={}, type={}, elem=", spanToStr(span), typeToStr(type));
    elem->Print();
    std::cout << ", const=" << boolStr(is_const) << ')';
}

void AstDeref::Print() const {
    std::cout << std::format("Deref(span={}, type={}, ptr=", spanToStr(span), typeToStr(type));
    ptr->Print();
    std::cout << ')';
}

void AstCall::Print() const {
    std::cout << std::format("Call(span={}, type={}, func=", spanToStr(span), typeToStr(type));

    func->Print();

    std::cout << ", args = [";

    for (int i = 0; i < args.size(); i++) {
        if (i > 0) {
            std::cout << ", ";
        }

        args[i]->Print();
    }

    std::cout << "])";
}

/* -------------------------------------------------------------------------- */

void AstIdent::Print() const {
    std::cout << std::format(
        "Identifier(span={}, type={}, name={})",
        spanToStr(span),
        typeToStr(type),
        Name()
    );
}

void AstBoolLit::Print() const {
    std::cout << std::format(
        "BoolLit(span={}, type={}, value={})",
        spanToStr(span),
        typeToStr(type),
        value
    );
}

void AstFloatLit::Print() const {
    std::cout << std::format(
        "FloatLit(span={}, type={}, value={})",
        spanToStr(span),
        typeToStr(type),
        value
    );
}

void AstIntLit::Print() const {
    std::cout << std::format(
        "IntLit(span={}, type={}, value={})",
        spanToStr(span),
        typeToStr(type),
        value
    );
}

void AstNullLit::Print() const {
    std::cout << std::format("Null(span={}, type={})", spanToStr(span), typeToStr(type));
}

