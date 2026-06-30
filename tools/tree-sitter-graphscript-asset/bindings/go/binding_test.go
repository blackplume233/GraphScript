package tree_sitter_graphscript_asset_test

import (
	"testing"

	tree_sitter "github.com/smacker/go-tree-sitter"
	"github.com/tree-sitter/tree-sitter-graphscript_asset"
)

func TestCanLoadGrammar(t *testing.T) {
	language := tree_sitter.NewLanguage(tree_sitter_graphscript_asset.Language())
	if language == nil {
		t.Errorf("Error loading GraphscriptAsset grammar")
	}
}
