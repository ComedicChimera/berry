package common

import "path/filepath"

type Module struct {
	ID   int
	Name string

	SymbolTable map[string]*Symbol

	Files []*SourceFile
}

type SourceFile struct {
	ID     int
	Parent *Module

	AbsPath     string
	DisplayPath string

	Defs []AstDef
}

/* -------------------------------------------------------------------------- */

var modIDCounter int
var fileIDCounter int

func NewModule(name string) *Module {
	modIDCounter++

	return &Module{
		ID:          modIDCounter,
		Name:        name,
		SymbolTable: make(map[string]*Symbol),
	}
}

func (m *Module) AddSourceFile(srcPath string) (*SourceFile, error) {
	fileIDCounter++

	absPath, err := filepath.Abs(srcPath)
	if err != nil {
		return nil, err
	}

	// TOOD: better display path computation
	displayPath := filepath.Clean(srcPath)

	srcFile := &SourceFile{
		ID:     fileIDCounter,
		Parent: m,

		AbsPath:     absPath,
		DisplayPath: displayPath,
	}

	m.Files = append(m.Files, srcFile)

	return srcFile, nil
}
