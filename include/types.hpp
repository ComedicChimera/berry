#ifndef TYPES_H_INC
#define TYPES_H_INC

#include <unordered_map>

#include "map_view.hpp"

struct TypeContext;

// TypeKind enumerates the possible variants of Type.
enum TypeKind {
    TYPE_INT,       // Integral type
    TYPE_FLOAT,     // Floating-point/real type
    TYPE_BOOL,      // Boolean type
    TYPE_UNIT,      // Unit/void type

    TYPE_PTR,       // Pointer type
    TYPE_FUNC,      // Function type
    TYPE_ARRAY,     // Array Type (fixed size)
    TYPE_SLICE,     // Slice Type
    TYPE_STRING,    // String Type
    
    TYPE_NAMED,     // Named Type 
    TYPE_ALIAS,     // Alias Type
    TYPE_STRUCT,    // Struct Type
    TYPE_ENUM,      // Simple Enum Type (no value-storing variants)

    TYPE_UNTYP,     // Untyped

    TYPES_COUNT
};

struct Type;

// StructField is a field in a struct type.
struct StructField {
    std::string_view name;
    Type* type;
    bool exported;

    StructField(std::string_view name_, Type* type_, bool exported_)
    : name(name_)
    , type(type_)
    , exported(exported_)
    {}
};

// Method contains the shared type information for a method.
struct Method {
    size_t parent_id;
    size_t decl_number;

    std::string_view name;
    Type* signature;
    bool exported;

    llvm::Value* llvm_value;

    Method(size_t parent_id_, std::string_view name_, Type* sig_, bool exported_)
    : parent_id(parent_id_)
    , decl_number(0)
    , name(name_)
    , signature(sig_)
    , exported(exported_)
    , llvm_value(nullptr)
    {}
};

// MethodTable is a collection of methods keyed by name.
typedef std::unordered_map<std::string_view, Method*> MethodTable;

struct FactoryFunc {
    size_t parent_id;
    size_t decl_number;

    Type* signature;
    bool exported;

    llvm::Value* llvm_value;

    FactoryFunc(size_t parent_id_, Type* sig_, bool exported_)
    : parent_id(parent_id_)
    , decl_number(0)
    , signature(sig_)
    , exported(exported_)
    , llvm_value(nullptr)
    {}
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
            Type* elem_type;
        } ty_Ptr;
        struct {
            std::span<Type*> param_types;
            Type* return_type;
        } ty_Func;
        struct {
            Type* elem_type;
            uint64_t len;
        } ty_Array;
        struct {
            Type* elem_type;
        } ty_Slice;

        struct {
            size_t mod_id;
            std::string_view mod_name;
            std::string_view name;
            Type* type;

            MethodTable* methods;
            FactoryFunc* factory;
        } ty_Named;
        struct {
            std::span<StructField> fields;
            MapView<size_t> name_map;
            llvm::Type* llvm_type;
        } ty_Struct;
        struct {
            MapView<uint64_t> tag_map;
        } ty_Enum;

        struct {
            size_t key;
            Type* concrete_type;
            TypeContext* parent;
        } ty_Untyp;
    };

    friend class TypeContext;
    std::string ToString() const;
    Type* Inner();
    Type* FullUnwrap();
};

/* -------------------------------------------------------------------------- */

// UntypedKind enumerates the different kinds of untypeds.
enum UntypedKind {
    UK_INT,     // integer literal (int only)
    UK_FLOAT,   // float literal (float only)
    UK_NUM,     // number literal (int or float)

    UK_NULL,    // null literal (can be any type)
};

// TypeConvResult indicates the result of a particular type conversion (cast or
// coerce) It is used by the checker to determine a) if a conversion is valid
// and b) if it requires a type cast (explicit or implicit).  The goal is to
// make sure that the backend always has a cast HIR node when a cast might
// occur: implicit type conversions don't have a cast node in the AST but should
// in the HIR.
enum TypeConvResult {
    TY_CONV_FAIL,   // Type conversion is not possible
    TY_CONV_CAST,   // Type cast *might* be required
    TY_CONV_EQ      // No type conversion required
};

// TypeContext is the state used for type checking and inference.
class TypeContext {
    // unt_uf is the union-find used to "merge" untyped instances.
    std::vector<Type*> unt_uf;

    // untypedTableEntry is an entry in the untyped table.
    struct untypedTableEntry {
        // key is the union-find key of the associated untyped.
        size_t key;

        // kind the associated untyped kind.
        UntypedKind kind;

        // concrete_type is inferred concrete type (may be nullptr).
        Type* concrete_type;
    };

    // unt_table is the table of untyped information used during type inference.
    // It stores the kind and the inferred type of the untyped.
    std::vector<untypedTableEntry> unt_table;

public:
    // Flags controlling behavior of type context.
    bool infer_enabled = false;   // Enable inferences based on comparisons.
    bool unsafe_enabled = false;  // Enable unsafe type conversions. 

    TypeContext() = default;

    TypeContext(const TypeContext& other)
    : unt_uf(other.unt_uf)
    , unt_table(other.unt_table)
    , infer_enabled(other.infer_enabled)
    , unsafe_enabled(other.unsafe_enabled)
    {}

    TypeContext(TypeContext&& other)
    : unt_uf(std::move(other.unt_uf))
    , unt_table(std::move(other.unt_table))
    , infer_enabled(other.infer_enabled)
    , unsafe_enabled(other.unsafe_enabled)
    {}

    // Important: All of the comparison functions can change their behavior
    // depending on the context flags.  For example, if TC_INFER is set then the
    // functions are assumed to be "asserts" (ex: Equal -> MustEqual) that can
    // infer types (ie: compilation requires that Equal(a, b) is true).

    // Equal returns whether types a and b are equal.
    inline bool Equal(Type* a, Type* b) {
        return innerEqual(a->Inner(), b->Inner());
    }

    // SubType returns whether sub is a subtype of super.
    inline TypeConvResult SubType(Type* sub, Type* super) {
        return innerSubType(sub->Inner(), super->Inner());
    }

    // Cast returns whether src can be cast to dest.
    inline bool Cast(Type* src, Type* dest) {
        return innerCast(src->Inner(), dest->Inner());
    }

    // IsNumberType returns whether type is a number type. 
    bool IsNumberType(Type* type);

    // IsIntType returns whether type is an integer type.
    bool IsIntType(Type* type);

    // IsNullType returns whether type is an untyped null.
    bool IsNullType(Type* type);

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
    TypeConvResult innerSubType(Type *sub, Type *super);

    // innerCast returns whether inner-unwrapped src can be cast to
    // inner-unwrapped dest.
    bool innerCast(Type *src, Type *dest);

    /* ---------------------------------------------------------------------- */

    // tryConcrete returns whether inner-unwrapped type other can be valid
    // concrete type for the untyped with key key.
    bool tryConcrete(size_t key, Type *other);

    // find returns the untyped table entry corresponding to key.
    untypedTableEntry& find(size_t key);

    // find returns the untyped table entry corresponding to key and stores its
    // union-find rank (length of the path) into rank.
    untypedTableEntry& find(size_t key, int* rank);

    // tryUnion attempts to merge untypeds with keys a and b.
    bool tryUnion(size_t a, size_t b);
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
inline Type prim_string_type { .kind{ TYPE_STRING }, .ty_Slice{ &prim_u8_type } };

inline Type prim_ptr_u8_type { .kind{ TYPE_PTR }, .ty_Ptr{ &prim_u8_type } };

inline Type* platform_int_type { nullptr };
inline Type* platform_uint_type { nullptr };

Type* AllocType(Arena& arena, TypeKind kind);

#endif