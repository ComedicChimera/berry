package dtypes

var (
	GlobI8Type   Type = &IntType{BitSize: 8, Signed: true}
	GlobU8Type   Type = &IntType{BitSize: 8, Signed: false}
	GlobI16Type  Type = &IntType{BitSize: 16, Signed: true}
	GlobU16Type  Type = &IntType{BitSize: 16, Signed: false}
	GlobI32Type  Type = &IntType{BitSize: 32, Signed: true}
	GlobU32Type  Type = &IntType{BitSize: 32, Signed: false}
	GlobI64Type  Type = &IntType{BitSize: 64, Signed: true}
	GlobU64Type  Type = &IntType{BitSize: 64, Signed: false}
	GlobF32Type  Type = &FloatType{BitSize: 32}
	GlobF64Type  Type = &FloatType{BitSize: 64}
	GlobBoolType Type = &BoolType{}
	GlobUnitType Type = &UnitType{}
)

/* -------------------------------------------------------------------------- */

func IsNumberType(typ Type) bool {
	switch v := typ.Inner().(type) {
	case *IntType:
		return true
	case *FloatType:
		return true
	case *Untyped:
		return v.Kind() != UK_NULL
	default:
		return false
	}
}

/* -------------------------------------------------------------------------- */

func MustNumberType(typ Type) bool {
	if v, ok := typ.Inner().(*Untyped); ok {
		if v.Kind() == UK_NULL {
			v.table.find(v.ID).kind = UK_NUMBER
		}

		return true
	}

	return IsNumberType(typ)
}

func MustIntType(typ Type) bool {
	switch v := typ.Inner().(type) {
	case *IntType:
		return true
	case *Untyped:
		if v.Kind() == UK_FLOAT {
			return false
		} else if v.Kind() == UK_NUMBER || v.Kind() == UK_NULL {
			v.table.find(v.ID).kind = UK_INT
		}

		return true
	default:
		return false
	}
}
