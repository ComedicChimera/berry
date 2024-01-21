package dtypes

func MustCast(dest, src Type) bool {
	if sutype, sok := src.Inner().(*Untyped); sok {
		if sutype.Kind() == UK_NULL {
			sutype.MakeConcrete(dest)
			return true
		}

		if sutype.Compatible(dest) {
			sutype.MakeConcrete(dest)
			return true
		}

		return IsNumberType(dest)
	}

	if IsNumberType(src) && IsNumberType(dest) {
		return true
	}

	// TODO: pointer casting?

	return MustEqual(dest, src)
}
