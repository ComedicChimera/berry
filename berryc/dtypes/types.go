package dtypes

import (
	"fmt"
	"io"
)

type Type interface {
	Dump(w io.Writer)
	Inner() Type
}

/* -------------------------------------------------------------------------- */

type IntType struct {
	BitSize int
	Signed  bool
}

func (it *IntType) Dump(w io.Writer) {
	if it.Signed {
		fmt.Fprint(w, 'i')
	} else {
		fmt.Fprint(w, 'u')
	}

	fmt.Fprint(w, it.BitSize)
}

func (it *IntType) Inner() Type {
	return it
}

type FloatType struct {
	BitSize int
}

func (ft *FloatType) Dump(w io.Writer) {
	fmt.Fprintf(w, "f%d", ft.BitSize)
}

func (ft *FloatType) Inner() Type {
	return ft
}

type BoolType struct{}

func (bt *BoolType) Dump(w io.Writer) {
	fmt.Fprint(w, "bool")
}

func (bt *BoolType) Inner() Type {
	return bt
}

type UnitType struct{}

func (ut *UnitType) Dump(w io.Writer) {
	fmt.Fprint(w, "()")
}

func (ut *UnitType) Inner() Type {
	return ut
}

/* -------------------------------------------------------------------------- */

type PointerType struct {
	ElemType Type
	Const    bool
}

func (pt *PointerType) Dump(w io.Writer) {
	fmt.Print(w, '*')

	if pt.Const {
		fmt.Print(w, "const ")
	}

	pt.ElemType.Dump(w)
}

func (pt *PointerType) Inner() Type {
	return pt
}

/* -------------------------------------------------------------------------- */

type FuncType struct {
	Params     []Type
	ReturnType Type
}

func (ft *FuncType) Dump(w io.Writer) {
	if len(ft.Params) == 0 {
		fmt.Fprint(w, "()")
	} else if len(ft.Params) == 1 {
		ft.Params[0].Dump(w)
	} else {
		fmt.Fprint(w, "(")

		for i, param := range ft.Params {
			if i > 0 {
				fmt.Fprint(w, ", ")
			}

			param.Dump(w)
		}

		fmt.Fprint(w, ")")
	}

	fmt.Fprint(w, " -> ")
	ft.ReturnType.Dump(w)
}

func (ft *FuncType) Inner() Type {
	return ft
}
