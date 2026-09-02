# UI

UI engine layer: hosts the Dear ImGui context (CPU side) and drives it from the engine stages (IInit/ITick/IShutdown); the UI render feature draws the produced draw data over the scene before present

## Code files
- [Public/UI.h](Public/UI.h) — 层声明（挂载 stage 接口）
- [Private/UI.cpp](Private/UI.cpp) — 实现

## Related docs
- [API.md](API.md) - API documentation
