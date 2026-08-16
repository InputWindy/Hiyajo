# Shader

Shader source parsing, compilation, caching, and material parameters.

- `ShaderLoader.h` — parse `.shader` files and compile legacy packages.
- `ShaderCompiler.h` — GLSL reflection / SPIR-V compilation.
- `ShaderCache.h` — on-disk bytecode + reflection cache.
- `MaterialParamMap.h` — case-insensitive material parameter storage.

Aggregate: `Shader.h`. See [Render module](../README.md) and [RHI](../RHI/CONTRACT.md).
