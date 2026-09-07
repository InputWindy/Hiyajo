# DrawTriangleFeature — API 文档

渲染 feature（`namespace Maho`）：只在 `IRender` 阶段绘制一个全屏三角形，验证动态渲染 + 管线构建。

## FDrawTriangleFeature <class : FLayer<IRender>>

只挂载 `IRender` 阶段。构造函数声明跨 feature 依赖——**在 Scene 清屏之后绘制**：

```cpp
FDrawTriangleFeature::FDrawTriangleFeature()
{
    AddDependency(std::type_index(typeid(IRender)), "FScene", std::type_index(typeid(IRender)));
}
```

#### 接口

| 签名 | 说明 |
|------|------|
| `void Render(FRender& R) override` | 懒构建管线 + 记录绘制命令 |

`Render` 行为：
1. `R.GetRHI()` / `R.GetShaderCompiler()` / `Scene::GetScene()` 任一为空则返回。
2. 首次：用内嵌 GLSL（`#version 460`，`gl_VertexIndex` 全屏三角形）经 `FShaderCompilerServer::CompileStage` 编译 VS/FS，`RHI->CreateShaderModule` / `CreatePipelineLayout`（空布局）/ `CreateGraphicsPipeline`（dynamic rendering，`RenderPass = nullptr`）。
3. 每帧：`R.GetFrameCommandList()`，从 Scene 取颜色/深度 attachment（LoadOp Load），`BeginRendering -> BindGraphicsPipeline -> SetViewport/SetScissor -> Draw(3) -> EndRendering`。

## CreateLayer <导出函数>

```cpp
extern "C" MAHO_DRAWTRIANGLEFEATURE_API Maho::FLayerBase* CreateLayer()
{
    return Maho::FDrawTriangleFeature::CreateLayer();
}
```

- [README](../../../README.md) — 项目走读 · [Scene](../Scene/API.md) — 场景目标 · [Render 子系统](../../../../../Plugins/Engine/Render/API.md) — 宿主
