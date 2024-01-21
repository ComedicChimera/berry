package dtypes

func MustSubtype(super, sub Type) bool {
	return MustEqual(super, sub)
}
