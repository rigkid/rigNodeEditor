# nodes

Hero for **rigNodeEditor**: color-wash `CNodeGraph` (LFO -> Map -> Brightness <- Tint ->
Color Out + Color Ref) driving a pulsing circle fill. Node Editor fills the central dock.

Artist guide (controls, full catalog, recipes): [docs/nodes.md](https://github.com/rigkid/RigKit/blob/main/docs/nodes.md).

```bash
cmake -S packs/rigNodeEditor/examples/nodes -B packs/rigNodeEditor/examples/nodes/build
cmake --build packs/rigNodeEditor/examples/nodes/build --target nodes
./packs/rigNodeEditor/examples/nodes/build/bin/nodes
```

Regenerate the README preview (writes `img/preview.png` then quits):

```bash
./packs/rigNodeEditor/examples/nodes/build/bin/nodes --capture-preview
```
