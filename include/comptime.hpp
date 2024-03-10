#ifndef COMPTIME_H_INC
#define COMPTIME_H_INC

#include "ast.hpp"

enum ConstKind {
    CONST_I8,
    CONST_U8,
    CONST_I16,
    CONST_U16,
    CONST_I32 ,
    CONST_U32,
    CONST_I64,
    CONST_U64 ,
    CONST_F32 ,
    CONST_F64 ,
    CONST_BOOL,
    CONST_PTR,
    CONST_ARRAY ,
    CONST_STRUCT,

    CONSTS_COUNT
};

struct ConstValue {
    ConstKind kind;
    union {
        int8_t v_i8;
        uint8_t v_u8;
        int16_t v_i16 ;
        uint16_t v_u16;
        int32_t v_i32;
        uint32_t v_u32;
        int64_t v_i64;
        uint64_t v_u64;
        float v_f32 ;
        double v_f64 ;
        bool v_bool;
        ConstValue* v_ptr;
        std::vector<ConstValue*> v_comp;
    };
};

class ComptimeEvaluator {
public:
    ConstValue* Evaluate(AstExpr* expr);
};

#endif