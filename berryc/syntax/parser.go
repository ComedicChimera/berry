package syntax

import (
	"berryc/common"
	"berryc/report"
	"fmt"
	"io"
)

type Parser struct {
	srcFile  *common.SourceFile
	lexer    *Lexer
	tok      *Token
	prevSpan *report.TextSpan
}

func NewParser(srcFile *common.SourceFile, file io.Reader) *Parser {
	return &Parser{
		srcFile: srcFile,
		lexer:   NewLexer(file, srcFile.Parent.Name, srcFile.DisplayPath),
	}
}

func (p *Parser) Parse() {
	p.next()

	for !p.has(TOK_EOF) {
		meta := common.Metadata{}
		if p.has(TOK_ATSIGN) {
			p.parseMetadata(meta)
		}

		switch p.tok.Kind {
		case TOK_FUNC:
			p.parseFuncDef(meta)
		case TOK_LET:
			p.parseGlobalVar(meta)
		default:
			p.reject()
		}
	}
}

/* -------------------------------------------------------------------------- */

func (p *Parser) declareGlobal(sym *common.Symbol) {
	if _, ok := p.srcFile.Parent.SymbolTable[sym.Name]; ok {
		p.errorOn(sym.Span, "multiple symbols with name '%s' defined in same scope", sym.Name)
	}

	p.srcFile.Parent.SymbolTable[sym.Name] = sym
}

/* -------------------------------------------------------------------------- */

func (p *Parser) parseIdentList(delim TokenKind) (toks []*Token) {
	toks = append(toks, p.wantAndGet(TOK_IDENT))

	for p.has(delim) {
		p.next()

		toks = append(toks, p.wantAndGet(TOK_IDENT))
	}

	return
}

func (p *Parser) parseInitializer() common.AstExpr {
	p.want(TOK_ASSIGN)

	return p.parseExpr()
}

/* -------------------------------------------------------------------------- */

func (p *Parser) next() {
	if p.tok != nil {
		p.prevSpan = p.tok.Span
	}

	p.tok = p.lexer.NextToken()
}

func (p *Parser) has(kind TokenKind) bool {
	return p.tok.Kind == kind
}

func (p *Parser) want(kind TokenKind) {
	if p.has(kind) {
		p.next()
	} else {
		p.reject()
	}
}

func (p *Parser) wantAndGet(kind TokenKind) *Token {
	if p.has(kind) {
		tok := p.tok
		p.next()
		return tok
	} else {
		p.reject()
		return nil
	}
}

func (p *Parser) reject() {
	if p.tok.Kind == TOK_EOF {
		p.error("unexpected end of file")
	} else {
		p.error("unexpected token: %s", p.tok.Value)
	}
}

func (p *Parser) error(msg string, a ...any) {
	p.errorOn(p.tok.Span, msg, a...)
}

func (p *Parser) errorOn(span *report.TextSpan, msg string, a ...any) {
	report.Throw(&report.SourceError{
		Message: fmt.Sprintf(msg, a...),
		Info: &report.SourceInfo{
			ModName:     p.srcFile.Parent.Name,
			DisplayPath: p.srcFile.DisplayPath,
			Span:        span,
		},
	})
}
