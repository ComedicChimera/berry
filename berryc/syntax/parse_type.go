package syntax

import "berryc/dtypes"

func (p *Parser) parseTypeExt() dtypes.Type {
	p.want(TOK_COLON)
	return p.parseTypeLabel()
}

func (p *Parser) parseTypeLabel() dtypes.Type {
	switch p.tok.Kind {
	case TOK_I8:
		p.next()
		return dtypes.GlobI8Type
	case TOK_U8:
		p.next()
		return dtypes.GlobU8Type
	case TOK_I16:
		p.next()
		return dtypes.GlobI16Type
	case TOK_U16:
		p.next()
		return dtypes.GlobU16Type
	case TOK_I32:
		p.next()
		return dtypes.GlobI32Type
	case TOK_U32:
		p.next()
		return dtypes.GlobU32Type
	case TOK_I64:
		p.next()
		return dtypes.GlobI64Type
	case TOK_U64:
		p.next()
		return dtypes.GlobU64Type
	case TOK_F32:
		p.next()
		return dtypes.GlobF32Type
	case TOK_F64:
		p.next()
		return dtypes.GlobF64Type
	case TOK_BOOL:
		p.next()
		return dtypes.GlobBoolType
	case TOK_STAR:
		p.next()

		return &dtypes.PointerType{
			ElemType: p.parseTypeLabel(),
			// TODO: const
		}
	default:
		p.reject()
		return nil
	}
}
