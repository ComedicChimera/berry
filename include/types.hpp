#ifndef TYPES_H_INC
#define TYPES_H_INC

#include <unordered_map>

#include "base.hpp"
#include "arena.hpp"

struct TypeContext;

// TypeKind enumerates the possible variants of Type.
enum TypeKind {
    TYPE_INT,   // Integral type
    TYPE_FLOAT, // Floating-point/real type
    TYPE_BOOL,  // Boolean type
    TYPE_UNIT,  // Unit/void type
    TYPE_UNTYP, // Untyped

    TYPE_PTR,   // Pointer type
    TYPE_FUNC,  // Function type
    TYPE_ARRAY, // Array Type
    
    TYPE_NAMED,     // Named Type 
    TYPE_STRUCT,    // Struct Type

    TYPES_COUNT
};

// StructField is a field in a struct type.
struct StructField {
    std::string_view name;
    Type* type;
};

// Type represents a Berry data type.
struct Type {
    TypeKind kind;

    union {
        struct {
            int bit_size;
            bool is_signed;
        } ty_Int;
        struct {
            int bit_size;
        } ty_Float;
        struct {
            uint64_t key;
            Type* concrete_type;
            TypeContext* parent;
        } ty_Untyp;

        struct {
            Type* elem_type;
        } ty_Ptr;
        struct {
            std::span<Type*> param_types;
            Type* return_type;
        } ty_Func;
        struct {
            Type* elem_type;
        } ty_Array;

        struct {
            size_t mod_id;
            std::string_view mod_name;
            std::string_view name;
            Type* type;
        } ty_Named;
        struct {
            std::span<StructField> fields;
        } ty_Struct;
    };

    friend class TypeContext;
    std::string ToString() const;
    Type* Inner();
};

/* -------------------------------------------------------------------------- */

// UntypedKind enumerates the different kinds of untypeds.
enum UntypedKind {
    UK_INT,     // integer literal (int only)
    UK_FLOAT,   // float literal (float only)
    UK_NUM,     // number literal (int or float)
};

// TypeCtxFlags stores all possible type context flags.
typedef int TypeCtxFlags;
enum {
    TC_DEFAULT = 0,
    TC_INFER = 1
};

// TypeContext is the state used for type checking and inference.
class TypeContext {
    // unt_uf is the union-find used to "merge" untyped instances.
    std::vector<Type*> unt_uf;

    // untypedTableEntry is an entry in the untyped table.
    struct untypedTableEntry {
        // key is the union-find key of the associated untyped.
        uint64_t key;

        // kind the associated untyped kind.
        UntypedKind kind;

        // concrete_type is inferred concrete type (may be nullptr).
        Type* concrete_type;
    };

    // unt_table is the table of untyped information used during type inference.
    // It stores the kind and the inferred type of the untyped.
    std::vector<untypedTableEntry> unt_table;

public:
    // flags contains the context flags.
    TypeCtxFlags flags;

    TypeContext() : flags(TC_DEFAULT) {}

    // Important: All of the comparison functions can change their behavior
    // depending on the context flags.  For example, if TC_INFER is set then the
    // functions are assumed to be "asserts" (ex: Equal -> MustEqual) that can
    // infer types (ie: compilation requires that Equal(a, b) is true).

    // Equal returns whether types a and b are equal.
    bool Equal(Type* a, Type* b);

    // SubType returns whether sub is a subtype of super.
    bool SubType(Type* sub, Type* super);

    // Cast returns whether src can be cast to dest.
    bool Cast(Type* src, Type* dest);

    // IsNumberType returns whether type is a number type. 
    bool IsNumberType(Type* type);

    // IsIntType returns whether type is an integer type.
    bool IsIntType(Type* type);

    /* ---------------------------------------------------------------------- */

    // AddUntyped adds a new untyped to the type context.
    void AddUntyped(Type* ut, UntypedKind kind);

    // InferAll infers a final type for all declared untypeds.
    void InferAll();

    // Clear clears/resets the state of the type context.
    void Clear();

private:
    // fren :)
    friend struct Type;

    std::string untypedToString(const Type* ut);

    // getConcreteType returns the concrete type of ut.  If it doesn't have a
    // concrete type, then nullptr is returned.
    Type* getConcreteType(const Type* ut);

    /* ---------------------------------------------------------------------- */

    // innerEqual returns whether inner-unwrapped types a and b are equal.
    bool innerEqual(Type *a, Type *b);
    
    // innerSubType returns whether inner-unwrapped sub is a subtype of
    // inner-unwrapped super.
    bool innerSubType(Type *sub, Type *super);

    // Cast returns whether inner-unwrapped src can be cast to inner-unwrapped
    // dest.
    bool innerCast(Type *src, Type *dest);

    /* ---------------------------------------------------------------------- */

    // tryConcrete returns whether inner-unwrapped type other can be valid
    // concrete type for the untyped with key key.
    bool tryConcrete(uint64_t key, Type *other);

    // find returns the untyped table entry corresponding to key.
    untypedTableEntry& find(uint64_t key);

    // find returns the untyped table entry corresponding to key and stores its
    // union-find rank (length of the path) into rank.
    untypedTableEntry& find(uint64_t key, int* rank);

    // tryUnion attempts to merge untypeds with keys a and b.
    bool tryUnion(uint64_t a, uint64_t b);
};

// innerIsNumberType is helper to check if an inner-unwrapped type is numeric.
inline bool innerIsNumberType(Type* type) {
    return type->kind == TYPE_INT || type->kind == TYPE_FLOAT;
}

/* -------------------------------------------------------------------------- */

// Global Instances for Primitive Types.

inline Type prim_i8_type { .kind{ TYPE_INT }, .ty_Int{ 8, true } };
inline Type prim_u8_type { .kind{ TYPE_INT }, .ty_Int{ 8, false } };
inline Type prim_i16_type { .kind{ TYPE_INT }, .ty_Int{ 16, true } };
inline Type prim_u16_type { .kind{ TYPE_INT }, .ty_Int{ 16, false } };
inline Type prim_i32_type { .kind{ TYPE_INT }, .ty_Int{ 32, true } };
inline Type prim_u32_type { .kind{ TYPE_INT }, .ty_Int{ 32, false } };
inline Type prim_i64_type { .kind{ TYPE_INT }, .ty_Int{ 64, true } };
inline Type prim_u64_type { .kind{ TYPE_INT }, .ty_Int{ 64, false } };
inline Type prim_f32_type { .kind{ TYPE_FLOAT }, .ty_Float{ 32 } };
inline Type prim_f64_type { .kind{ TYPE_FLOAT }, .ty_Float{ 64 } };
inline Type prim_bool_type { .kind { TYPE_BOOL }};
inline Type prim_unit_type { .kind { TYPE_UNIT }};
inline Type prim_string_type { .kind{ TYPE_ARRAY}, .ty_Array{ &prim_u8_type } };

Type* AllocType(Arena& arena, TypeKind kind);

#endif