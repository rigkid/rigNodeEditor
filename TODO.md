# rigNodeEditor TODO

## Composite node library (deferred follow-up)

Export In / Export Out nodes + "Collapse to node" (right-click / File menu) already
turn a sub-graph into a single group node inside the current graph. The next step is
making those composites reusable across graphs and projects:

- Save a selected group node (its `nested` graph + `publishes` interface) to a
  library file. Suggested format: one JSON document per composite reusing the
  `NodeSerializers.cpp` graph schema (`rig.node.graph`) plus metadata
  (name, category, description). Suggested location: `data/user/nodes/*.node.json`
  with an app-provided search path.
- List saved composites in the "Add node" menu under a "Library" category, spawning
  a deep copy of the stored group (remap ids through `NodeGraphData::allocId`, same
  approach as `ungroup`'s id lifting).
- Editing a spawned composite is a copy edit (no live sync back to the library
  file); a "Update library entry" action can overwrite the source file explicitly.
- Versioning: store the catalog/schema version in the file so stale composites can
  be migrated or rejected with a clear message.

Related pieces that already exist:

- `rig::node::syncGroupInterfaces` (rigNodeComponent/NodeGraph.cpp) keeps a group's
  outer pins in lockstep with the Export In / Export Out nodes inside it.
- `graph.input` / `graph.output` catalog entries (NodeCatalog.cpp, category "Graph")
  carry `name` / `type` params that define the outer pin label and datatype.
