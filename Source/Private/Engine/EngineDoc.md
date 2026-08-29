# Engine (Private)

## Code Files

*(none -- the Engine layer is fully header-only, no .cpp implementation)*

## Notes

`Engine/Layer.h` is pure templates + inline implementation (FLayerBase / FLayer / commands / DispatchInstance are all in the header). There is no Private-side compilation unit, so there is no implementation algorithm dictionary.

- The cross-platform primitives the layer depends on (DLL loading, fatal errors) are in `Core`'s `Assembly.cpp` / `Fatal.cpp` -- see `../Core/CoreDoc.md`.
- Parallel execution is in `Core/Schedulers.h` + `Core/ThreadPool.h` (header-only templates + inline).

## Related Docs

- [../../Public/Engine/EngineDoc.md](../../Public/Engine/EngineDoc.md) -- layer architecture (Public)
- [../../Public/Core/CoreDoc.md](../../Public/Core/CoreDoc.md) -- Core infrastructure concepts
- [../../PrivateDoc.md](../../PrivateDoc.md) -- Private layer
