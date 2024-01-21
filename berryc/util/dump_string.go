package util

import (
	"io"
	"strings"
)

type Dumper interface {
	Dump(w io.Writer)
}

func DumpString(d Dumper) string {
	var sb strings.Builder
	d.Dump(&sb)
	return sb.String()
}
