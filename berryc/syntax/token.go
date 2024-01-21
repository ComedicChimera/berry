package syntax

import (
	"berryc/report"
	"fmt"
	"io"
)

type TokenKind uint8

const (
	TOK_LET TokenKind = iota
	TOK_FUNC
	TOK_AS
	TOK_NULL

	TOK_I8
	TOK_U8
	TOK_I16
	TOK_U16
	TOK_I32
	TOK_U32
	TOK_I64
	TOK_U64
	TOK_F32
	TOK_F64
	TOK_BOOL

	TOK_PLUS
	TOK_MINUS
	TOK_STAR
	TOK_FSLASH
	TOK_MOD

	TOK_AMP
	TOK_PIPE
	TOK_CARRET

	TOK_ASSIGN

	TOK_LPAREN
	TOK_RPAREN
	TOK_LBRACKET
	TOK_RBRACKET
	TOK_LBRACE
	TOK_RBRACE
	TOK_COMMA
	TOK_DOT
	TOK_SEMICOLON
	TOK_COLON
	TOK_ATSIGN

	TOK_IDENT
	TOK_INTLIT
	TOK_FLOATLIT
	TOK_RUNELIT
	TOK_STRLIT
	TOK_BOOLLIT

	TOK_EOF
)

type Token struct {
	Kind  TokenKind
	Value string
	Span  *report.TextSpan
}

func (tok *Token) Dump(w io.Writer) {
	fmt.Fprintf(w, "Token(%d, \"%s\", [%d, %d])\n", tok.Kind, tok.Value, tok.Span.StartLine, tok.Span.StartCol)
}
