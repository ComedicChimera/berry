package dtypes

func MustEqual(a, b Type) bool {
	a = a.Inner()
	b = b.Inner()

	autype, aok := a.(*Untyped)
	butype, bok := b.(*Untyped)
	if aok && bok {
		return autype.table.union(autype.ID, butype.ID)
	} else if aok {
		if autype.Compatible(b) {
			autype.MakeConcrete(b)
			return true
		} else {
			return false
		}
	} else if bok {
		if butype.Compatible(a) {
			butype.MakeConcrete(a)
			return true
		} else {
			return false
		}
	}

	switch v := a.(type) {
	case *IntType:
		if bint, ok := b.(*IntType); ok {
			return v.BitSize == bint.BitSize && v.Signed == bint.Signed
		}
	case *FloatType:
		if bfloat, ok := b.(*FloatType); ok {
			return v.BitSize == bfloat.BitSize
		}
	case *BoolType:
		{
			_, ok := b.(*BoolType)
			return ok
		}
	case *UnitType:
		{
			_, ok := b.(*UnitType)
			return ok
		}
	case *PointerType:
		if bptr, ok := b.(*PointerType); ok {
			return MustEqual(v.ElemType, bptr.ElemType) && v.Const == bptr.Const
		}
	case *FuncType:
		if bfn, ok := b.(*FuncType); ok {
			if len(v.Params) != len(bfn.Params) {
				return false
			}

			for i, aparam := range v.Params {
				if !MustEqual(aparam, bfn.Params[i]) {
					return false
				}
			}

			return MustEqual(v.ReturnType, bfn.ReturnType)
		}
	}

	return false
}
