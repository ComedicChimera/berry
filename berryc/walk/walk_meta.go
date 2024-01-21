package walk

import (
	"berryc/common"

	"slices"
)

type metaFormat struct {
	Arity       int
	ValidValues []string
}

var validMetatags = map[string]metaFormat{
	"extern":   {Arity: 0},
	"abientry": {Arity: 0},
	"callconv": {Arity: 1, ValidValues: []string{"c", "win64"}},
}

func (w *Walker) validateMetadata(meta common.Metadata) {
	for _, tag := range meta {
		if metaFmt, ok := validMetatags[tag.TagName]; ok {
			if metaFmt.Arity == 0 {
				if len(tag.Value) > 0 {
					w.error(tag.TagSpan, "metadata tag '%s' does not take a value", tag.TagName)
				}
			} else if len(metaFmt.ValidValues) > 0 {
				if !slices.Contains(metaFmt.ValidValues, tag.Value) {
					w.error(tag.ValueSpan, "'%s' is not a valid value for metadata tag '%s'", tag.Value, tag.TagName)
				}
			}
		} else {
			w.error(tag.TagSpan, "unsupported metadata tag: '%s'", tag.TagName)
		}
	}
}
