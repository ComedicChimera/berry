package common

import (
	"berryc/dtypes"
	"berryc/report"
)

type AstNode interface {
	GetSpan() *report.TextSpan
}

type AstBase struct {
	Span *report.TextSpan
}

func (ab *AstBase) GetSpan() *report.TextSpan {
	return ab.Span
}

/* -------------------------------------------------------------------------- */

type MetaTag struct {
	TagName string
	TagSpan *report.TextSpan

	Value     string
	ValueSpan *report.TextSpan
}

type Metadata map[string]*MetaTag

type AstDef interface {
	AstNode

	GetMetadata() Metadata
}

type AstDefBase struct {
	Span     *report.TextSpan
	Metadata Metadata
}

func (ad *AstDefBase) GetSpan() *report.TextSpan {
	return ad.Span
}

func (ad *AstDefBase) GetMetadata() Metadata {
	return ad.Metadata
}

/* -------------------------------------------------------------------------- */

type AstExpr interface {
	AstNode

	GetType() dtypes.Type
	IsMutable() bool
}

type AstExprBase struct {
	Span *report.TextSpan
	Type dtypes.Type
}

func (ae *AstExprBase) GetSpan() *report.TextSpan {
	return ae.Span
}

func (ae *AstExprBase) GetType() dtypes.Type {
	return ae.Type
}

func (ae *AstExprBase) IsMutable() bool {
	return false
}
