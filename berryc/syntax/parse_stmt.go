package syntax

import (
	"berryc/common"
	"berryc/dtypes"
	"berryc/report"
)

func (p *Parser) parseBlock() common.AstNode {
	startSpan := p.wantAndGet(TOK_LBRACE).Span

	var stmts []common.AstNode
	for !p.has(TOK_RBRACE) {
		stmts = append(stmts, p.parseStmt())
	}

	endSpan := p.wantAndGet(TOK_RBRACE).Span

	return &common.AstBlock{
		AstBase: common.AstBase{
			Span: report.SpanOver(startSpan, endSpan),
		},
		Stmts: stmts,
	}
}

func (p *Parser) parseStmt() (stmt common.AstNode) {
	switch p.tok.Kind {
	case TOK_LET:
		stmt = p.parseVarDecl()
	default:
		stmt = p.parseExpr()
	}

	p.want(TOK_SEMICOLON)
	return
}

/* -------------------------------------------------------------------------- */

func (p *Parser) parseVarDecl() *common.AstLocalVar {
	startSpan := p.tok.Span
	p.want(TOK_LET)

	nameIdent := p.wantAndGet(TOK_IDENT)

	var typ dtypes.Type
	if p.has(TOK_COLON) {
		typ = p.parseTypeExt()
	}

	var init common.AstExpr
	if typ == nil || p.has(TOK_ASSIGN) {
		init = p.parseInitializer()
	}

	return &common.AstLocalVar{
		AstBase: common.AstBase{
			Span: report.SpanOver(startSpan, p.tok.Span),
		},
		Symbol: &common.Symbol{
			Name:    nameIdent.Value,
			FileID:  p.srcFile.ID,
			Span:    nameIdent.Span,
			Kind:    common.SK_VALUE,
			Type:    typ,
			Mutable: true,
		},
		Initializer: init,
	}
}
