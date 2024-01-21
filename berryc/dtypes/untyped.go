package dtypes

import (
	"fmt"
	"io"
)

type UntypedKind uint8

const (
	UK_NULL UntypedKind = iota
	UK_NUMBER
	UK_INT
	UK_FLOAT
)

// Should NEVER be created directly!
type Untyped struct {
	ID    int
	table *UntypedTable
	ctype Type
}

func (ut *Untyped) Dump(w io.Writer) {
	if ut.ctype != nil {
		ut.ctype.Dump(w)
		return
	}

	switch ut.table.find(ut.ID).kind {
	case UK_NULL:
		fmt.Fprintf(w, "untyped null")
	case UK_NUMBER:
		fmt.Fprintf(w, "untyped number")
	case UK_FLOAT:
		fmt.Fprintf(w, "untyped float")
	case UK_INT:
		fmt.Fprintf(w, "untyped int")
	}
}

func (ut *Untyped) Inner() Type {
	if ut.ctype != nil {
		return ut.ctype
	}

	entry := ut.table.find(ut.ID)
	if entry.ctype != nil {
		return entry.ctype
	}

	return ut
}

func (ut *Untyped) Kind() UntypedKind {
	return ut.table.find(ut.ID).kind
}

func (ut *Untyped) IsConcrete() bool {
	if ut.ctype != nil {
		return true
	}

	return ut.table.find(ut.ID).ctype != nil
}

// ctype MUST be concrete
func (ut *Untyped) MakeConcrete(ctype Type) {
	if ut.ctype != nil {
		return
	}

	entry := ut.table.find(ut.ID)
	entry.ctype = ctype

	ut.ctype = ctype
}

// ctype MUST be concrete
func (ut *Untyped) Compatible(ctype Type) bool {
	switch ut.Kind() {
	case UK_NULL:
		return true
	case UK_FLOAT:
		{
			_, ok := ctype.(*FloatType)
			return ok
		}
	case UK_INT:
		{
			_, ok := ctype.(*IntType)
			return ok
		}
	case UK_NUMBER:
		{
			_, ok := ctype.(*IntType)
			if !ok {
				_, ok = ctype.(*FloatType)
			}

			return ok
		}
	default:
		panic("unreachable")
	}
}

/* -------------------------------------------------------------------------- */

type untypedTableEntry struct {
	kind  UntypedKind
	ctype Type
}

type UntypedTable struct {
	uf      []int
	entries map[int]*untypedTableEntry
}

func NewUntypedTable() *UntypedTable {
	return &UntypedTable{
		entries: make(map[int]*untypedTableEntry),
	}
}

func (ut *UntypedTable) NewUntyped(kind UntypedKind) *Untyped {
	id := len(ut.uf)

	ut.uf = append(ut.uf, id)
	ut.entries[id] = &untypedTableEntry{kind: kind}

	return &Untyped{
		ID:    id,
		table: ut,
	}
}

func (ut *UntypedTable) DefaultAll() {
	for _, entry := range ut.entries {
		if entry.ctype != nil {
			continue
		}

		if entry.kind == UK_FLOAT {
			entry.ctype = GlobF32Type
		} else if entry.kind == UK_NUMBER || entry.kind == UK_INT {
			// TODO: platform dependent integers...
			entry.ctype = GlobI64Type
		}
	}
}

func (ut *UntypedTable) Reset() {
	ut.uf = nil

	clear(ut.entries)
}

/* -------------------------------------------------------------------------- */

func (ut *UntypedTable) find(id int) *untypedTableEntry {
	for ut.uf[id] != id {
		id = ut.uf[id]
	}

	return ut.entries[id]
}

func (ut *UntypedTable) union(a, b int) bool {
	aentry, aid, arank := ut.findWithRank(a)
	bentry, bid, brank := ut.findWithRank(b)

	which, compat := dominantKind(aentry.kind, bentry.kind)
	if !compat {
		return false
	}

	var dominant *untypedTableEntry
	if which == 0 {
		dominant = aentry
	} else {
		dominant = bentry
	}

	if arank > brank {
		ut.uf[bid] = aid

		ut.entries[aid] = dominant
		delete(ut.entries, bid)
	} else {
		ut.uf[aid] = bid

		ut.entries[bid] = dominant
		delete(ut.entries, aid)
	}

	return true
}

func (ut *UntypedTable) findWithRank(id int) (*untypedTableEntry, int, int) {
	rank := 0
	for ut.uf[id] != id {
		id = ut.uf[id]
		rank++
	}

	return ut.entries[id], id, rank
}

func dominantKind(a, b UntypedKind) (int, bool) {
	if a == UK_NULL {
		return 1, true
	} else if b == UK_NULL {
		return 0, true
	}

	if a == UK_NUMBER {
		return 1, true
	} else if b == UK_NUMBER {
		return 0, true
	}

	return 0, a == b
}
