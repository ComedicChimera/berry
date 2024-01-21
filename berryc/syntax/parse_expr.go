package syntax

import (
	"berryc/common"
	"berryc/dtypes"
	"berryc/report"
	"slices"
	"strconv"
)

func (p *Parser) parseExpr() common.AstExpr {
	binOp := p.parseBinaryOp(0)

	if p.has(TOK_AS) {
		p.next()

		destType := p.parseTypeLabel()

		return &common.AstCast{
			AstExprBase: common.AstExprBase{
				Span: report.SpanOver(binOp.GetSpan(), p.prevSpan),
				Type: destType,
			},
			SrcExpr: binOp,
		}
	}

	return binOp
}

/* -------------------------------------------------------------------------- */

var tokKindToOpKind = map[TokenKind]common.AstOpKind{
	TOK_PLUS:   common.AOP_ADD,
	TOK_MINUS:  common.AOP_SUB,
	TOK_STAR:   common.AOP_MUL,
	TOK_FSLASH: common.AOP_DIV,
	TOK_MOD:    common.AOP_MOD,
	TOK_AMP:    common.AOP_BAND,
	TOK_PIPE:   common.AOP_BOR,
	TOK_CARRET: common.AOP_BXOR,
}

var predTable = [][]TokenKind{
	{TOK_PIPE},
	{TOK_CARRET},
	{TOK_AMP},
	{TOK_STAR, TOK_FSLASH, TOK_MOD},
	{TOK_PLUS, TOK_MINUS},
}

func (p *Parser) parseBinaryOp(predLevel int) common.AstExpr {
	var lhs common.AstExpr
	if predLevel == len(predTable) {
		return p.parseUnaryOp()
	} else {
		lhs = p.parseBinaryOp(predLevel + 1)
	}

	for {
		if slices.Contains(predTable[predLevel], p.tok.Kind) {
			opKind := tokKindToOpKind[p.tok.Kind]

			rhs := p.parseBinaryOp(predLevel + 1)

			lhs = &common.AstBinaryOp{
				AstExprBase: common.AstExprBase{
					Span: report.SpanOver(lhs.GetSpan(), rhs.GetSpan()),
				},
				OpKind: opKind,
				Lhs:    lhs,
				Rhs:    rhs,
			}
		} else {
			break
		}
	}

	return lhs
}

func (p *Parser) parseUnaryOp() common.AstExpr {
	startSpan := p.tok.Span

	switch p.tok.Kind {
	case TOK_AMP:
		{
			p.next()

			// TODO: const

			elem := p.parseAtomExpr()

			return &common.AstAddrOf{
				AstExprBase: common.AstExprBase{
					Span: report.SpanOver(startSpan, elem.GetSpan()),
				},
				Elem: elem,
			}
		}
	case TOK_STAR:
		{
			p.next()

			ptr := p.parseAtomExpr()

			return &common.AstDeref{
				AstExprBase: common.AstExprBase{
					Span: report.SpanOver(startSpan, ptr.GetSpan()),
				},
				Ptr: ptr,
			}
		}
	case TOK_MINUS:
		{
			p.next()

			op := p.parseAtomExpr()

			return &common.AstUnaryOp{
				AstExprBase: common.AstExprBase{
					Span: report.SpanOver(startSpan, op.GetSpan()),
				},
				OpKind:  common.AOP_NEG,
				Operand: op,
			}
		}
	default:
		return p.parseAtomExpr()
	}
}

/* -------------------------------------------------------------------------- */

func (p *Parser) parseAtomExpr() common.AstExpr {
	root := p.parseAtom()

	for {
		switch p.tok.Kind {
		case TOK_LPAREN:
			root = p.parseCallExpr(root)
		default:
			return root
		}
	}
}

func (p *Parser) parseCallExpr(root common.AstExpr) *common.AstCall {
	startSpan := p.wantAndGet(TOK_LPAREN).Span

	var args []common.AstExpr
	if !p.has(TOK_RPAREN) {
		for {
			arg := p.parseExpr()
			args = append(args, arg)

			if p.has(TOK_COMMA) {
				p.next()
			} else {
				break
			}
		}
	}

	endSpan := p.wantAndGet(TOK_RPAREN).Span

	return &common.AstCall{
		AstExprBase: common.AstExprBase{
			Span: report.SpanOver(startSpan, endSpan),
		},
		Func: root,
		Args: args,
	}
}

func (p *Parser) parseAtom() common.AstExpr {
	switch p.tok.Kind {
	case TOK_IDENT:
		{
			identTok := p.tok
			p.next()

			return &common.AstIdent{
				AstExprBase: common.AstExprBase{
					Span: identTok.Span,
				},
				Name: identTok.Value,
			}
		}
	case TOK_INTLIT:
		{
			n, err := strconv.ParseUint(p.tok.Value, 0, 64)
			if err != nil {
				if err == strconv.ErrRange {
					p.error("integer value too large")
				} else {
					// should never happen
					panic(err)
				}
			}

			span := p.tok.Span
			p.next()

			return &common.AstIntLit{
				AstExprBase: common.AstExprBase{
					Span: span,
				},
				Value: n,
			}
		}
	case TOK_FLOATLIT:
		{
			n, err := strconv.ParseFloat(p.tok.Value, 64)
			if err != nil {
				if err == strconv.ErrRange {
					p.error("float value more than 1/2 ULP from nearest representable value")
				} else {
					// should never happen
					panic(err)
				}
			}

			span := p.tok.Span
			p.next()

			return &common.AstFloatLit{
				AstExprBase: common.AstExprBase{
					Span: span,
				},
				Value: n,
			}
		}
	case TOK_RUNELIT:
		{
			r := p.decodeRuneLit(p.tok.Value)

			span := p.tok.Span
			p.next()

			return &common.AstIntLit{
				AstExprBase: common.AstExprBase{
					Span: span,
					Type: dtypes.GlobI32Type,
				},
				Value: uint64(r),
			}
		}
	case TOK_BOOLLIT:
		{
			value := p.tok.Value[0] == 't'

			span := p.tok.Span
			p.next()

			return &common.AstBoolLit{
				AstExprBase: common.AstExprBase{
					Span: span,
					Type: dtypes.GlobBoolType,
				},
				Value: value,
			}
		}
	case TOK_LPAREN:
		{
			p.next()

			subExpr := p.parseExpr()

			p.want(TOK_RPAREN)

			return subExpr
		}
	case TOK_NULL:
		{
			span := p.tok.Span
			p.next()

			return &common.AstNullLit{
				AstExprBase: common.AstExprBase{
					Span: span,
				},
			}
		}
	default:
		p.reject()
		return nil
	}
}

func (p *Parser) decodeRuneLit(lit string) rune {
	if lit[0] == '\\' {
		switch lit[1] {
		case 'a':
			return '\a'
		case 'b':
			return '\b'
		case 'f':
			return '\f'
		case 'n':
			return '\n'
		case 'r':
			return '\r'
		case 't':
			return '\t'
		case 'v':
			return '\v'
		case '0':
			return 0
		case '\'', '"':
			return rune(lit[1])
		}
	} else {
		for _, r := range lit {
			return r
		}
	}

	// should never happen
	panic("unable to decode rune literal value")
}
