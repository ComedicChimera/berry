#ifndef TYPES_H_INC
#define TYPES_H_INC

#include <unordered_map>

#include "base.hpp"

struct TypeContext;

// TypeKind enumerates the possible variants of Type.
enum TypeKind {
    TYPE_INT,   // Integral type
    TYPE_FLOAT, // Floating-point/real type
    TYPE_BOOL,  // Boolean type
    TYPE_UNIT,  // Unit/void type
    TYPE_PTR,   // Pointer type
    TYPE_FUNC,  // Function type
    TYPE_ARRAY, // Array Type
    TYPE_UNTYP  // Untyped
};

// Type represents a Berry data type.
struct Type {
    // GetKind returns the kind of the type.
    inline virtual TypeKind GetKind() const = 0;

    // ToString returns the printable string representation of the type.
    virtual std::string ToString() const = 0;

    // Inner returns the most concrete representation of the type.  For most
    // types, this will be an identity map, but for types such "untyped", this
    // function can extract their evaluated type.  Most type checking is done
    // wrt a types inner type.
    virtual Type* Inner();

    /* ---------------------------------------------------------------------- */

    // IMPORTANT: The following methods should only be called by type context.
    // They provide the type-specific implementations of comparison without
    // taking into account general type conversion rules.  For instance,
    // impl_Cast(a) only returns true if there exists a specific casting rule
    // from a to b (ex: i32 to f64, but not from i32 to i32).  Furthermore, all
    // this function expect inner type unwrapping has already occurred before
    // they are called.

    virtual bool impl_Equal(TypeContext* tctx, Type* other) = 0;
    virtual bool impl_SuperType(TypeContext* tctx, Type* sub);
    virtual bool impl_Cast(TypeContext* tctx, Type* dest);
};

/* -------------------------------------------------------------------------- */

// IntType represents a Berry integer type.
struct IntType : public Type {
    // The integer's size in bits.
    uint bit_size;

    // Whether the integer is signed.
    bool is_signed;

    IntType(uint bit_size_, bool is_signed_)
    : bit_size(bit_size_), is_signed(is_signed_) {}

    inline TypeKind GetKind() const override { return TYPE_INT; }
    std::string ToString() const override;
    
    bool impl_Equal(TypeContext *tctx, Type *other) override;
    bool impl_Cast(TypeContext *tctx, Type *dest) override;
};

// FloatType represents a Berry floating-point/real type.
struct FloatType : public Type {
    // The float's size in bits.
    uint bit_size;

    FloatType(uint bit_size_) : bit_size(bit_size_) {}

    inline TypeKind GetKind() const override { return TYPE_FLOAT; }
    std::string ToString() const override;

    bool impl_Equal(TypeContext *tctx, Type *other) override;
    bool impl_Cast(TypeContext *tctx, Type *dest) override;
};

// BoolType represents a Berry boolean type.
struct BoolType : public Type {
    inline TypeKind GetKind() const override { return TYPE_BOOL; }
    std::string ToString() const override;
    
    bool impl_Equal(TypeContext *tctx, Type *other) override;
    bool impl_Cast(TypeContext *tctx, Type *dest) override;
};

// UnitType represents a Berry unit/void type.
struct UnitType : public Type {
    inline TypeKind GetKind() const override { return TYPE_UNIT; }
    std::string ToString() const override;

    bool impl_Equal(TypeContext *tctx, Type *other) override;
};

// PointerType represents a Berry pointer type.
struct PointerType : public Type {
    // elem_type is the type pointed to (the T in *T).
    Type* elem_type;

    // immut indicates whether the pointer is immutable.
    bool immut;

    PointerType(Type* elem_type, bool immut = false)
    : elem_type(elem_type)
    , immut(immut)
    {}
    
    inline TypeKind GetKind() const override { return TYPE_PTR; }
    std::string ToString() const override;
    
    bool impl_Equal(TypeContext *tctx, Type *other) override;
    bool impl_Cast(TypeContext *tctx, Type *dest) override;
};

// FuncType represents a Berry function type.
struct FuncType : public Type {
    // param_types is the list of parameter types the function accepts.
    std::span<Type*> param_types;

    // return_type is the return type of the function.
    Type* return_type;

    FuncType(std::span<Type*> param_types, Type* return_type)
    : param_types(param_types)
    , return_type(return_type)
    {}
    
    inline TypeKind GetKind() const override { return TYPE_FUNC; }
    std::string ToString() const override;

    bool impl_Equal(TypeContext* tctx, Type *other) override;
};

// ArrayType represents a Berry array type.
struct ArrayType : public Type {
    // elem_type is the array's element type.
    Type* elem_type;

    ArrayType(Type* elem_type)
    : elem_type(elem_type)
    {}

    inline TypeKind GetKind() const override { return TYPE_ARRAY; }
    std::string ToString() const override;

    bool impl_Equal(TypeContext* tctx, Type *other) override;
};

/* -------------------------------------------------------------------------- */

// UntypedKind enumerates the different kinds of untypeds.
enum UntypedKind {
    UK_INT,     // integer literal (int only)
    UK_FLOAT,   // float literal (float only)
    UK_NUM,     // number literal (int or float)
};

// Untyped represents a value which does not have an immediate concrete type.
// They are used specifically for literals and offer some additional flexibility
// beyond that of their concrete type counterparts.
struct Untyped : public Type {
    Untyped(TypeContext* parent, UntypedKind kind);

    inline TypeKind GetKind() const override { return TYPE_UNTYP; }
    std::string ToString() const override;
    Type* Inner() override;

    bool impl_Equal(TypeContext* tctx, Type* other) override;

private:
    // fren :)
    friend class TypeContext;

    // key is the union-find key of the untyped.
    uint64_t key;

    // parent is the parent type context of the untyped.
    TypeContext* parent;

    // concrete_type is the internal concrete type of the untyped. This
    // will be nullptr until the untyped is resolved. 
    Type* concrete_type;
};

/* -------------------------------------------------------------------------- */

// TypeCtxFlags stores all possible type context flags.
typedef int TypeCtxFlags;
enum {
    TC_DEFAULT = 0,
    TC_INFER = 1
};

// TypeContext is the state used for type checking and inference.
class TypeContext {
    // unt_uf is the union-find used to "merge" untyped instances.
    std::vector<Untyped*> unt_uf;

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
    std::unordered_map<uint64_t, untypedTableEntry> unt_table;

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
    void AddUntyped(Untyped* ut, UntypedKind kind);

    // GetAbstractUntypedStr gets the string representation of an abstract
    // untyped (abstract = undetermined/not concrete).
    std::string GetAbstractUntypedStr(const Untyped* ut);

    // GetConcreteType returns the concrete type of ut.  If it doesn't have
    // a concrete type, then nullptr is returned.
    Type* GetConcreteType(Untyped* ut);

    // InferAll infers a final type for all declared untypeds.
    void InferAll();

    // Clear clears/resets the state of the type context.
    void Clear();

private:
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
    return type->GetKind() == TYPE_INT || type->GetKind() == TYPE_FLOAT;
}

/* -------------------------------------------------------------------------- */

// Global Instances for Primitive Types.

inline IntType prim_i8_type { 8, true };
inline IntType prim_u8_type { 8, false };
inline IntType prim_i16_type { 16, true };
inline IntType prim_u16_type { 16, false };
inline IntType prim_i32_type { 32, true };
inline IntType prim_u32_type { 32, false };
inline IntType prim_i64_type { 64, true };
inline IntType prim_u64_type { 64, false };
inline FloatType prim_f32_type { 32 };
inline FloatType prim_f64_type { 64 };
inline BoolType prim_bool_type;
inline UnitType prim_unit_type;

#endif