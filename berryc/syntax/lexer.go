package syntax

import (
	"berryc/report"
	"bufio"
	"io"
	"strings"
	"unicode"
)

type Lexer struct {
	file        *bufio.Reader
	modName     string
	displayPath string

	tokBuff *strings.Builder

	line, col           int
	startLine, startCol int

	ahead rune
}

func NewLexer(file io.Reader, modName, displayPath string) *Lexer {
	return &Lexer{
		file:        bufio.NewReader(file),
		modName:     modName,
		displayPath: displayPath,
		tokBuff:     &strings.Builder{},
		line:        1, col: 1,
		startLine: 1, startCol: 1,
		ahead: 0,
	}
}

func (l *Lexer) NextToken() *Token {
	for l.peek() {
		switch l.ahead {
		case '\t', ' ', '\r', '\n':
			l.skip()
		case '/':
			l.mark()
			l.read()

			if l.peek() {
				if l.ahead == '/' {
					l.skipLineComment()
					continue
				} else if l.ahead == '*' {
					l.skipBlockComment()
					continue
				}
			}

			return l.makeToken(TOK_FSLASH)
		case '\'':
			return l.lexRuneLit()
		case '"':
			return l.lexStringLit()
		case '+':
			return l.lexSingle(TOK_PLUS)
		case '-':
			return l.lexSingle(TOK_MINUS)
		case '*':
			return l.lexSingle(TOK_STAR)
		case '%':
			return l.lexSingle(TOK_MOD)
		case '&':
			return l.lexSingle(TOK_AMP)
		case '|':
			return l.lexSingle(TOK_PIPE)
		case '^':
			return l.lexSingle(TOK_CARRET)
		case '=':
			return l.lexSingle(TOK_ASSIGN)
		case '(':
			return l.lexSingle(TOK_LPAREN)
		case ')':
			return l.lexSingle(TOK_RPAREN)
		case '{':
			return l.lexSingle(TOK_LBRACE)
		case '}':
			return l.lexSingle(TOK_RBRACE)
		case '[':
			return l.lexSingle(TOK_LBRACKET)
		case ']':
			return l.lexSingle(TOK_RBRACKET)
		case '.':
			return l.lexSingle(TOK_DOT)
		case ',':
			return l.lexSingle(TOK_COMMA)
		case ':':
			return l.lexSingle(TOK_COLON)
		case ';':
			return l.lexSingle(TOK_SEMICOLON)
		case '@':
			return l.lexSingle(TOK_ATSIGN)
		default:
			if unicode.IsLetter(l.ahead) || l.ahead == '_' {
				return l.lexIdentOrKeyword()
			} else if '0' <= l.ahead && l.ahead <= '9' {
				return l.lexNumberLit()
			} else {
				l.mark()
				l.error("unknown character")
			}
		}
	}

	return &Token{Kind: TOK_EOF}
}

/* -------------------------------------------------------------------------- */

func (l *Lexer) lexNumberLit() *Token {
	l.mark()

	isFloat := false
	base := 10

	if l.ahead == '0' {
		l.read()

		if l.peek() {
			switch l.ahead {
			case 'b':
				base = 2
				l.read()
			case 'o':
				base = 8
				l.read()
			case 'x':
				base = 16
				l.read()
			}
		}
	}

	switch base {
	case 2:
		for l.peek() {
			if l.ahead == '0' || l.ahead == '1' {
				l.read()
			} else if l.ahead == '_' {
				l.skip()
			} else {
				break
			}
		}
	case 8:
		for l.peek() {
			if '0' <= l.ahead && l.ahead < '9' {
				l.read()
			} else if l.ahead == '_' {
				l.skip()
			} else {
				break
			}
		}
	case 10:
		isFloat = l.readBase10NumLit()
	case 16:
		isFloat = l.readBase16NumLit()
	}

	if isFloat {
		return l.makeToken(TOK_FLOATLIT)
	} else {
		return l.makeToken(TOK_INTLIT)
	}
}

func (l *Lexer) readBase10NumLit() (isFloat bool) {
	hasExp := false
	expectDigit := false

	for l.peek() {
		switch l.ahead {
		case '.':
			if hasExp {
				l.error("exponent in decimal")
			} else if isFloat {
				l.error("multiple decimals in float literal")
			}

			isFloat = true
			l.read()
		case 'e', 'E':
			if hasExp {
				l.error("multiple exponents in decimal")
			}

			isFloat = true
			hasExp = true
			l.read()

			if l.peek() && l.ahead == '-' {
				l.read()

				expectDigit = true
			}
		case '_':
			l.skip()
		default:
			if '0' <= l.ahead && l.ahead <= '9' {
				l.read()
				expectDigit = false
			} else if expectDigit {
				l.error("expected digit")
			} else {
				return
			}
		}
	}

	if expectDigit {
		l.error("expected digit")
	}

	return
}

func (l *Lexer) readBase16NumLit() (isFloat bool) {
	hasExp := false
	expectDigit := false

	for l.peek() {
		switch l.ahead {
		case '.':
			if hasExp {
				l.error("exponent in decimal")
			} else if isFloat {
				l.error("multiple decimals in float literal")
			}

			isFloat = true
			l.read()
		case 'p', 'P':
			if hasExp {
				l.error("multiple exponents in decimal")
			}

			isFloat = true
			hasExp = true
			l.read()

			if l.peek() && l.ahead == '-' {
				l.read()

				expectDigit = true
			}
		case '_':
			l.skip()
		default:
			if '0' <= l.ahead && l.ahead <= '9' || 'a' <= l.ahead && l.ahead < 'g' || 'A' <= l.ahead && l.ahead < 'G' {
				l.read()
				expectDigit = false
			} else if expectDigit {
				l.error("expected digit")
			} else {
				return
			}
		}
	}

	if expectDigit {
		l.error("expected digit")
	}

	return
}

/* -------------------------------------------------------------------------- */

var keywordPatterns = map[string]TokenKind{
	"func": TOK_FUNC,
	"let":  TOK_LET,
	"as":   TOK_AS,

	"i8":   TOK_I8,
	"u8":   TOK_U8,
	"i16":  TOK_I16,
	"u16":  TOK_U16,
	"i32":  TOK_I32,
	"u32":  TOK_U32,
	"i64":  TOK_I64,
	"u64":  TOK_U64,
	"f32":  TOK_F32,
	"f64":  TOK_F64,
	"bool": TOK_BOOL,

	"null":  TOK_NULL,
	"true":  TOK_BOOLLIT,
	"false": TOK_BOOLLIT,
}

func (l *Lexer) lexIdentOrKeyword() *Token {
	l.mark()
	l.read()

	for l.peek() && (unicode.IsLetter(l.ahead) || unicode.IsDigit(l.ahead) || l.ahead == '_') {
		l.read()
	}

	if kkind, ok := keywordPatterns[l.tokBuff.String()]; ok {
		return l.makeToken(kkind)
	}

	return l.makeToken(TOK_IDENT)
}

/* -------------------------------------------------------------------------- */

func (l *Lexer) lexSingle(kind TokenKind) *Token {
	l.mark()
	l.read()
	return l.makeToken(kind)
}

/* -------------------------------------------------------------------------- */

func (l *Lexer) lexStringLit() *Token {
	l.mark()
	l.skip()

	for l.peek() {
		switch l.ahead {
		case '"':
			l.skip()
			return l.makeToken(TOK_STRLIT)
		case '\n':
			l.error("string literal contains new line")
		case '\\':
			l.readEscapeCode()
		default:
			l.read()
		}
	}

	l.error("unclosed string literal")
	return nil
}

func (l *Lexer) lexRuneLit() *Token {
	l.mark()
	l.skip()

	if !l.peek() {
		l.error("unclosed rune literal")
	} else if l.ahead == '\'' {
		l.error("empty rune literal")
	} else if l.ahead == '\\' {
		l.readEscapeCode()
	} else {
		l.read()
	}

	if !l.peek() {
		l.error("unclosed rune literal")
	} else if l.ahead != '\'' {
		l.error("rune literal contains multiple characters")
	} else {
		l.skip()
	}

	return l.makeToken(TOK_RUNELIT)
}

func (l *Lexer) readEscapeCode() {
	l.read()

	if !l.peek() {
		l.error("expected escape code")
	}

	switch l.ahead {
	case 'a', 'b', 'f', 'n', 'r', 't', 'v', '0', '\'', '"':
		l.read()
	default:
		l.error("invalid escape code")
	}
}

/* -------------------------------------------------------------------------- */

func (l *Lexer) skipLineComment() {
	l.tokBuff.Reset()
	l.skip()

	for l.peek() {
		l.skip()

		if l.ahead == '\n' {
			break
		}
	}
}

func (l *Lexer) skipBlockComment() {
	l.tokBuff.Reset()
	l.skip()

	for l.peek() {
		l.skip()

		if l.ahead == '*' {
			if l.peek() && l.ahead == '/' {
				l.skip()
				break
			}
		}
	}
}

/* -------------------------------------------------------------------------- */

func (l *Lexer) makeToken(kind TokenKind) *Token {
	tok := &Token{
		Kind:  kind,
		Value: l.tokBuff.String(),
		Span:  l.getSpan(),
	}

	l.tokBuff.Reset()

	return tok
}

func (l *Lexer) mark() {
	l.startLine = l.line
	l.startCol = l.col
}

func (l *Lexer) error(msg string) {
	report.Throw(&report.SourceError{
		Message: msg,
		Info: &report.SourceInfo{
			ModName:     l.modName,
			DisplayPath: l.displayPath,
			Span:        l.getSpan(),
		},
	})
}

func (l *Lexer) getSpan() *report.TextSpan {
	return &report.TextSpan{
		StartLine: l.startLine, StartCol: l.startCol,
		EndLine: l.line, EndCol: l.col,
	}
}

/* -------------------------------------------------------------------------- */

func (l *Lexer) peek() bool {
	r, _, err := l.file.ReadRune()
	if err != nil {
		if err == io.EOF {
			return false
		}

		report.Throw(err)
	}

	l.ahead = r
	l.file.UnreadRune()

	return true
}

func (l *Lexer) read() bool {
	r, _, err := l.file.ReadRune()
	if err != nil {
		if err == io.EOF {
			return false
		}

		report.Throw(err)
	}

	l.tokBuff.WriteRune(r)
	l.updatePos(r)

	return true
}

func (l *Lexer) skip() bool {
	r, _, err := l.file.ReadRune()
	if err != nil {
		if err == io.EOF {
			return false
		}

		report.Throw(err)
	}

	l.updatePos(r)

	return true
}

func (l *Lexer) updatePos(r rune) {
	switch r {
	case '\n':
		l.line++
		l.col = 1
	case '\t':
		l.col += 4
	default:
		l.col += 1
	}
}
