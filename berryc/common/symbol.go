package common

import (
	"berryc/dtypes"
	"berryc/report"
)

type SymbolKind uint8

const (
	SK_VALUE SymbolKind = iota
	SK_FUNC
)

type Symbol struct {
	Name   string
	FileID int
	Span   *report.TextSpan

	Kind SymbolKind
	Type dtypes.Type

	Mutable bool
}
