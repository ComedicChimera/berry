package report

import (
	"fmt"
	"io"
)

type LogLevel uint8

const (
	LOG_LEVEL_SILENT LogLevel = iota
	LOG_LEVEL_ERROR
	LOG_LEVEL_WARN
	LOG_LEVEL_ALL
)

type DisplayReporter struct {
	Out   io.Writer
	Level LogLevel
}

func (dr *DisplayReporter) ReportError(err error) {
	if dr.Level < LOG_LEVEL_ERROR {
		return
	}

	fmt.Fprint(dr.Out, "error: ")

	if serr, ok := err.(*SourceError); ok {
		serr.Dump(dr.Out)
		fmt.Fprint(dr.Out, "\n\n")

		// TODO: source highlighting
	} else {
		fmt.Fprintf(dr.Out, "%v\n\n", err)
	}
}
