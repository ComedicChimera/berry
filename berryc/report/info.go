package report

import (
	"fmt"
	"io"
	"strings"
)

type TextSpan struct {
	StartLine, StartCol int
	EndLine, EndCol     int
}

type SourceInfo struct {
	ModName     string
	DisplayPath string
	Span        *TextSpan
}

func SpanOver(start, end *TextSpan) *TextSpan {
	return &TextSpan{
		StartLine: start.StartLine,
		StartCol:  start.StartCol,
		EndLine:   end.EndLine,
		EndCol:    end.EndCol,
	}
}

/* -------------------------------------------------------------------------- */

type SourceError struct {
	Message string
	Info    *SourceInfo
}

func (serr *SourceError) Error() string {
	b := strings.Builder{}
	serr.Dump(&b)
	return b.String()
}

func (serr *SourceError) Dump(w io.Writer) {
	fmt.Fprintf(
		w, "[%s] %s:%d:%d: %s",
		serr.Info.ModName, serr.Info.DisplayPath,
		serr.Info.Span.StartLine, serr.Info.Span.StartCol,
		serr.Message,
	)
}
