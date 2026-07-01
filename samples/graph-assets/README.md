# Graph Asset Samples

This directory mirrors the migrated graph fixtures in current tree-sitter asset
syntax. The `.d.gs` files are copied next to the samples so the graphs can be
opened directly from this directory.

Open a sample in the graph editor:

```powershell
.\build\Release\gs.exe serve -i samples\graph-assets\ability_branching.gs -I samples\graph-assets\ue_core.d.gs -I samples\graph-assets\htn_nodes.d.gs -I samples\graph-assets\task_nodes.d.gs -I samples\graph-assets\levelscript_nodes.d.gs -I samples\graph-assets\mixed_declarations.d.gs -p 8080
```

Good observation targets:

- `minimal.gs` - smallest source-to-graph example.
- `graph_as_node.gs` - graph-as-node interaction.
- `ability_branching.gs` - fan-out and mixed declaration imports.
- `levelscript_complex.gs` - multiple events, layout annotations, and data binds.
- `task_complex.gs` - schema, annotations, events, and functions.
- `all_features.gs` - constants, annotations, parameters, nodes, events, and functions.

`invalid_connection.gs` intentionally keeps a duplicate connection shape for
diagnostic checks.
