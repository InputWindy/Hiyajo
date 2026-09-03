# Scene — API 文档

全局渲染资源 feature（`namespace Maho::Scene`）。跨帧持有共享离屏目标（SceneColor / SceneDepth），其他渲染 feature 经 `Scene::GetScene()` 读取。swapchain 尺寸变化时重建目标。

## FScene <class : FLayer<IBeginRender, IRender, IEndRender, IPresent>>

渲染场景 feature，挂载全部四个渲染阶段。

#### 接口

| 签名 | 说明 |
|------|------|
| `FScene()` | 构造；注册全局 `GScene` |
| `FRDGTextureRef GetSceneColor() const` | 共享颜色目标（离屏） |
| `FRDGTextureRef GetSceneDepth() const` | 共享深度目标 |
| `void BeginRender(FRender& R) override` | `EnsureTargets(R)`：按当前 swapchain 尺寸确保/重建目标 + 转换图像布局 |
| `void Render(FRender& R) override` | 场景 pass 头：清空 SceneColor（0.15/0.25/0.45）+ SceneDepth，后续 feature 以 LoadOp Load 叠加 |
| `void EndRender(FRender& R) override` | 无操作 |
| `void Present(FRender& R) override` | `R.Present(SceneColor)`：blit 颜色目标到 swapchain 后缓冲 |

## GetScene <function>

跨 DLL 全局访问器（`MAHO_SCENE_API`），返回 `FScene*`。渲染 feature 经它拿共享目标：

```cpp
Scene::FScene* S = Scene::GetScene();
```

- [README](../../../README.md) — 项目走读 · [Render 子系统](../../../../../Plugins/Engine/Render/API.md) — 宿主
