#ifndef TARGET_H_INC
#define TARGET_H_INC

#include "types.hpp"

#include "llvm/IR/LLVMContext.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/IR/DataLayout.h"

struct TargetPlatform {
    std::string os_name;
    std::string arch_name;

    size_t arch_size;
    std::string str_arch_size;

    bool debug;
    std::string str_debug;

    llvm::LLVMContext ll_context;
    llvm::Triple ll_triple;
    llvm::DataLayout ll_layout;

    // Used by frontend only.
    uint64_t GetComptimeSizeOf(Type* type);
    uint64_t GetComptimeAlignOf(Type* type);

private:
    llvm::Type* getLLVMType(Type* type);
};


TargetPlatform& GetTargetPlatform();

#endif