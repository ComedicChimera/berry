package main

import (
	"berryc/cmd"
	"berryc/report"
	"fmt"
	"os"
)

func main() {
	if len(os.Args) != 2 {
		fmt.Fprintf(os.Stderr, "error: usage: berryc <filename>\n")
		os.Exit(1)
	}

	srcFile := os.Args[1]

	rep := &report.DisplayReporter{Out: os.Stdout, Level: report.LOG_LEVEL_ALL}
	c := cmd.NewCompiler(rep)

	if !c.Compile(srcFile, "out.exe") {
		os.Exit(1)
	}
}
