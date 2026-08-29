<!-- mahogen -->
# Private

## Sub Layers

- [Core](Core/CoreDoc.md)
- [Engine](Engine/EngineDoc.md)
<!-- mahogen end -->

## Concept -- Implementation Layer

Engine implementation code. The core is almost entirely header-only; only two `.cpp` files exist under `Core/`. The Engine layer has no Private compilation units (Layer.h is fully header-only).

- [Core/Assembly.cpp](Core/Assembly.cpp) -- dynamic loading primitive (`FAssembly`)
- [Core/Fatal.cpp](Core/Fatal.cpp) -- crash fallback
- [Engine/EngineDoc.md](Engine/EngineDoc.md) -- Engine layer (no implementation, explanation page)

## Related Docs

- [Core/CoreDoc.md](Core/CoreDoc.md) -- implementation algorithm dictionary
- [Engine/EngineDoc.md](Engine/EngineDoc.md) -- Engine layer notes
- [../Public/PublicDoc.md](../Public/PublicDoc.md) -- interface layer
- [../SourceDoc.md](../SourceDoc.md) -- source root
