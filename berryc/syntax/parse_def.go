package syntax

import (
	"berryc/common"
	"berryc/dtypes"
	"berryc/report"
)

func (p *Parser) parseMetadata(meta common.Metadata) {
	p.want(TOK_ATSIGN)

	if p.has(TOK_LBRACKET) {
		p.next()

		p.parseMetaTag(meta)
		for p.has(TOK_COMMA) {
			p.next()
			p.parseMetaTag(meta)
		}

		p.want(TOK_RBRACKET)
	} else {
		p.parseMetaTag(meta)
	}
}

func (p *Parser) parseMetaTag(meta common.Metadata) {
	tag_name := p.wantAndGet(TOK_IDENT)
	tag := &common.MetaTag{TagName: tag_name.Value, TagSpan: tag_name.Span}

	if p.has(TOK_LPAREN) {
		p.next()

		tag_value := p.wantAndGet(TOK_STRLIT)

		p.want(TOK_RPAREN)

		tag.Value = tag_value.Value
		tag.ValueSpan = tag_value.Span
	}

	if _, ok := meta[tag.TagName]; ok {
		p.errorOn(tag_name.Span, "multiple meta tags with same name")
	}

	meta[tag.TagName] = tag
}

/* -------------------------------------------------------------------------- */

func (p *Parser) parseFuncDef(meta common.Metadata) {
	startSpan := p.tok.Span
	p.want(TOK_FUNC)

	ident := p.wantAndGet(TOK_IDENT)

	p.want(TOK_LPAREN)
	var params []*common.Symbol
	if !p.has(TOK_RPAREN) {
		params = p.parseFuncParams()
	}
	p.want(TOK_RPAREN)

	var returnType dtypes.Type = dtypes.GlobUnitType
	if !p.has(TOK_SEMICOLON) && !p.has(TOK_LBRACE) {
		returnType = p.parseTypeLabel()
	}

	var endSpan *report.TextSpan
	var body common.AstNode
	switch p.tok.Kind {
	case TOK_SEMICOLON:
		endSpan = p.tok.Span
		p.next()
	case TOK_LBRACE:
		body = p.parseBlock()
		endSpan = body.GetSpan()
	}

	funcType := &dtypes.FuncType{ReturnType: returnType}
	for _, param := range params {
		funcType.Params = append(funcType.Params, param.Type)
	}

	sym := &common.Symbol{
		Name:    ident.Value,
		FileID:  p.srcFile.ID,
		Span:    ident.Span,
		Kind:    common.SK_FUNC,
		Type:    funcType,
		Mutable: false,
	}
	p.declareGlobal(sym)

	funcDef := &common.AstFuncDef{
		AstDefBase: common.AstDefBase{
			Span:     report.SpanOver(startSpan, endSpan),
			Metadata: meta,
		},
		Symbol:     sym,
		Params:     params,
		ReturnType: returnType,
		Body:       body,
	}

	p.srcFile.Defs = append(p.srcFile.Defs, funcDef)
}

func (p *Parser) parseFuncParams() (params []*common.Symbol) {
	for {
		identList := p.parseIdentList(TOK_COMMA)
		paramType := p.parseTypeExt()

		for _, ident := range identList {
			sym := &common.Symbol{
				Name:    ident.Value,
				FileID:  p.srcFile.ID,
				Span:    ident.Span,
				Kind:    common.SK_VALUE,
				Type:    paramType,
				Mutable: true,
			}

			params = append(params, sym)
		}

		if p.has(TOK_COMMA) {
			p.next()
		} else {
			break
		}
	}

	return
}

/* -------------------------------------------------------------------------- */

func (p *Parser) parseGlobalVar(meta common.Metadata) {
	localVar := p.parseVarDecl()
	if localVar.Symbol.Type == nil {
		p.errorOn(localVar.Symbol.Span, "global variable missing type label")
	}

	p.want(TOK_SEMICOLON)

	p.declareGlobal(localVar.Symbol)
	globalVarDecl := &common.AstGlobalVar{
		AstDefBase: common.AstDefBase{
			Span:     localVar.Span,
			Metadata: meta,
		},
		VarDecl: localVar,
	}
	p.srcFile.Defs = append(p.srcFile.Defs, globalVarDecl)
}
