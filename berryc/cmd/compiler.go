package cmd

import (
	"berryc/common"
	"berryc/report"
	"berryc/syntax"
	"berryc/walk"
	"os"
)

type Compiler struct {
	rep report.Reporter
}

func NewCompiler(rep report.Reporter) *Compiler {
	return &Compiler{rep: rep}
}

func (c *Compiler) Compile(srcPath, outPath string) bool {
	report.SetGlobalReporter(c.rep)

	c.compileFile(srcPath, outPath)

	return report.NoErrors()
}

func (c *Compiler) compileFile(srcPath, outPath string) {
	file, err := os.Open(srcPath)
	if err != nil {
		report.Error(err)
		return
	}
	defer file.Close()

	mod := common.NewModule("main")
	srcFile, err := mod.AddSourceFile(srcPath)
	if err != nil {
		report.Error(err)
		return
	}

	defer report.Catch()
	p := syntax.NewParser(srcFile, file)
	p.Parse()

	w := walk.NewWalker(srcFile)
	for _, def := range srcFile.Defs {
		w.WalkDef(def)
	}
}
