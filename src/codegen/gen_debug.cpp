#include "codegen.hpp"

#define PATH_SEPARATOR "/"

void DebugGenerator::PushDisable() {
    if (disable_count == -1) {
        return;
    }

    no_emit = true;
    disable_count++;
}

void DebugGenerator::PopDisable() {
    if (disable_count == -1) {
        return;
    }

    if ((--disable_count) == 0) {
        no_emit = false;
    }
}

/* -------------------------------------------------------------------------- */

void DebugGenerator::EmitFileInfo(SourceFile &src_file)
{
    if (no_emit) {
        return;
    }

    auto last_sep = src_file.abs_path.rfind(PATH_SEPARATOR);
    llvm::DIFile* di_file;
    if (last_sep == std::string::npos) {
        di_file = db.createFile(src_file.abs_path, ".");
    } else {
        auto file_dir = src_file.abs_path.substr(0, last_sep);
        auto file_name = src_file.abs_path.substr(last_sep + 1);

        di_file = db.createFile(file_name, file_dir);
    }

    // TODO: multiple compile units kekw?
    db.createCompileUnit(
        llvm::dwarf::DW_LANG_C, 
        di_file,
        "berryc-v0.0.1",
        false,
        "",
        0
    );

    src_file.llvm_di_file = di_file;
}

void DebugGenerator::SetCurrentFile(SourceFile& src_file) {
    if (no_emit) {
        return;
    }

    Assert(src_file.llvm_di_file != nullptr, "file debug scope not created");

    curr_file = src_file.llvm_di_file;
}

void DebugGenerator::FinishModule() {
    db.finalize();
}

/* -------------------------------------------------------------------------- */

void DebugGenerator::BeginFuncBody(AstDef* fd, llvm::Function* ll_func) {
    if (no_emit) {
        return;
    }

    Assert(curr_file != nullptr, "function debug info missing enclosing file");

    auto call_conv = llvm::dwarf::DW_CC_normal;
    for (auto& meta_tag : fd->metadata) {
        if (meta_tag.name == "callconv") {
            if (meta_tag.value == "win64") {
                call_conv = llvm::dwarf::DW_CC_LLVM_Win64;
            } else if (meta_tag.value == "stdcall") {
                // TOOD: borland stdcall?
                call_conv = llvm::dwarf::DW_CC_BORLAND_stdcall;
            }    
        }
    }

    auto* sub = db.createFunction(
        curr_file,
        fd->an_Func.symbol->name,
        ll_func->getLinkage() == llvm::GlobalValue::ExternalLinkage ? "external" : "private",
        curr_file,
        fd->span.start_line,
        llvm::dyn_cast<llvm::DISubroutineType>(GetDIType(fd->an_Func.symbol->type, call_conv)),
        fd->span.start_line,
        llvm::DINode::FlagPrototyped,
        llvm::DISubprogram::SPFlagDefinition
    );

    ll_func->setSubprogram(sub);

    lexical_blocks.push_back(sub);
}

void DebugGenerator::EndFuncBody() {
    if (no_emit) {
        return;
    }

    Assert(lexical_blocks.size() > 0, "end func body missing corresponding begin func body call");

    lexical_blocks.pop_back();
}

void DebugGenerator::EmitGlobalVariableInfo(AstDef* node, llvm::GlobalVariable* ll_gv) {
    if (no_emit) {
        return;
    }

    Assert(curr_file != nullptr, "current debug file is null");

    bool is_external = ll_gv->getLinkage() == llvm::GlobalValue::ExternalLinkage;

    auto* ll_di_gv = db.createGlobalVariableExpression(
        curr_file,
        node->an_GlobalVar.symbol->name,
        is_external ? "external" : "private",
        curr_file,
        node->span.start_line,
        GetDIType(node->an_GlobalVar.symbol->type),
        !is_external
    );

    ll_gv->addDebugInfo(ll_di_gv);
}

void DebugGenerator::EmitLocalVariableInfo(AstStmt* node, llvm::Value* ll_var) {
    if (no_emit) {
        return;
    }

    Assert(!lexical_blocks.empty(), "local debug variable without enclosing lexical block");
    auto* scope = lexical_blocks.back();

    auto* di_local = db.createAutoVariable(
        scope,
        node->an_LocalVar.symbol->name,
        curr_file,
        node->span.start_line,
        GetDIType(node->an_LocalVar.symbol->type),
        true
    );

    db.insertDeclare(
        ll_var, 
        di_local, 
        db.createExpression(), 
        GetDebugLoc(scope, node->span), 
        irb.GetInsertBlock()
    );
}

/* -------------------------------------------------------------------------- */

void DebugGenerator::SetDebugLocation(const TextSpan& span) {
    if (no_emit) {
        return;
    }

    Assert(curr_file != nullptr, "current debug file is null");

    llvm::DIScope* scope;
    if (lexical_blocks.empty()) {
        scope = curr_file;
    } else {
        scope = lexical_blocks.back();
    }

    irb.SetCurrentDebugLocation(GetDebugLoc(scope, span));
}

void DebugGenerator::ClearDebugLocation() {
    if (no_emit) {
        return;
    }

    irb.SetCurrentDebugLocation(llvm::DebugLoc());
}

llvm::DILocation* DebugGenerator::GetDebugLoc(llvm::DIScope* scope, const TextSpan& span) {
    return llvm::DILocation::get(
        scope->getContext(),
        span.start_line,
        span.start_col,
        scope
    );
}

/* -------------------------------------------------------------------------- */

llvm::DIType* DebugGenerator::GetDIType(Type* type, uint call_conv) {
    type = type->Inner();

    switch (type->kind) {
    case TYPE_UNIT: 
        return prim_type_table[0];
    case TYPE_BOOL:
        return prim_type_table[11];
    case TYPE_INT:
    return prim_type_table[(type->ty_Int.bit_size >> 2) - (uint)type->ty_Int.is_signed];    
    case TYPE_FLOAT:
        return prim_type_table[(type->ty_Float.bit_size >> 3) + 2];
    case TYPE_PTR:
        // TODO: set pointer size based on target
        return db.createPointerType(GetDIType(type->ty_Ptr.elem_type), 64);
    case TYPE_FUNC:
    {
        auto& func_type = type->ty_Func;

        std::vector<llvm::Metadata*> di_param_types ( func_type.param_types.size() + 1);
        for (int i = 0; i < func_type.param_types.size(); i++) {
            di_param_types[i + 1] = GetDIType(func_type.param_types[i]);
        }

        if (func_type.return_type->Inner()->kind == TYPE_UNIT) {
            di_param_types[0] = nullptr;
        } else {

            di_param_types[0] = GetDIType(func_type.return_type);
        }

        return db.createSubroutineType(
            db.getOrCreateTypeArray(di_param_types),
            llvm::DINode::FlagZero,
            call_conv
        );
    }
    // TODO: arrays
    }

    Panic("unsupported type");
    return nullptr;
}

void DebugGenerator::buildTypeTable() {
    prim_type_table[0] = db.createBasicType("unit", 1, llvm::dwarf::DW_ATE_boolean);
    prim_type_table[11] = db.createBasicType("bool", 1, llvm::dwarf::DW_ATE_boolean);

    prim_type_table[1] = db.createBasicType("i8", 8, llvm::dwarf::DW_ATE_signed);
    prim_type_table[2] = db.createBasicType("u8", 8, llvm::dwarf::DW_ATE_unsigned);
    prim_type_table[3] = db.createBasicType("i16", 16, llvm::dwarf::DW_ATE_signed);
    prim_type_table[4] = db.createBasicType("u16", 16, llvm::dwarf::DW_ATE_unsigned);
    prim_type_table[7] = db.createBasicType("i32", 32, llvm::dwarf::DW_ATE_signed);
    prim_type_table[8] = db.createBasicType("u32", 32, llvm::dwarf::DW_ATE_unsigned);
    prim_type_table[15] = db.createBasicType("i64", 64, llvm::dwarf::DW_ATE_signed);
    prim_type_table[16] = db.createBasicType("u64", 64, llvm::dwarf::DW_ATE_unsigned);

    prim_type_table[6] = db.createBasicType("f32", 32, llvm::dwarf::DW_ATE_float);
    prim_type_table[10] = db.createBasicType("f64", 64, llvm::dwarf::DW_ATE_float);
}