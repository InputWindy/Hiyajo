#pragma once

#include "UIApi.h"
#include "UITypes.h"

#include <cstdint>
#include <functional>

namespace Maho { namespace UI {

/** 解析结果（ImGui 实现里 NativeHandle 即 ImTextureID / ImFont*）。 */
struct FUIResolvedResource
{
	bool          bValid = false;
	FUIName       Name{};              // 回指引用（缓存键 / 诊断）
	std::uintptr_t NativeHandle = 0;
	std::uint32_t Width = 0;
	std::uint32_t Height = 0;
};

/** 查询某项 FName 引用的渲染资源：
 *  - bIsFont = true  -> 字体资源
 *  - bIsFont = false -> 纹理资源（图标 / 图片 / 渲染目标镜像）
 *  bValid=false 表示暂未就绪（加载中 / 不存在）：后端按缺省外观绘，不阻塞帧。 */
using FUIResourceResolver = std::function<FUIResolvedResource(const FUIName& Resource, bool bIsFont)>;

/** 渲染侧注入解析能力（先例：`FResourceSystem::SetReadback`）。
 *  UI 插件因此**不依赖** `Resource`/`Render`：引用进树，能力由拥有者注入。 */
MAHO_UI_API void SetUIResourceResolver(FUIResourceResolver Resolver);
MAHO_UI_API bool HasUIResourceResolver();

/** 统一解析入口（后端调用）：未注入解析器时返回 `bValid=false` 的结果，不抛不崩。
 *  解析器在锁外调用，允许它回头再进 UI 的公开 API。 */
MAHO_UI_API FUIResolvedResource ResolveUIResource(const FUIName& Resource, bool bIsFont);

// -- 字体图集登记（N 已定：启动一次性全烘，运行期不改图集）---------------------------------
// 烘制归持有 ImGui 上下文与图集的那一侧（游戏侧 `UIFeature` / 编辑器侧 `FExampleEditor`），
// UI 插件只按 `(上下文, 字体引用, 档位)` 取用。档位来自 `FUITheme::FontSizeSteps`；
// `Size <= 0` 表示"任意字号都用这一份"（兜底条目）。
//
// **必须带上下文记账**：编辑器与游戏各有独立 ImGui 上下文与图集，同一个 `ImFont*` 只能喂给
// 造它的那个上下文；只按 `(引用, 档位)` 记账会让后烘的一侧覆盖前一侧的条目，另一侧随即拿到
// 别家图集的字体（字形纹理坐标错乱）。因此 `Context` 是键的一部分，`ImGuiContext*` 以 opaque
// 指针传入。

/** 登记一份已烘好的字体图集条目（`ImFont*` 以 opaque 句柄传入）。 */
MAHO_UI_API void RegisterUIFont(void* Context, const FUIName& Font, float Size, void* NativeFont);

/** 清空某个上下文的登记（该 ImGui 上下文销毁前调用；`Context` 为 nullptr 时清空全部）。 */
MAHO_UI_API void ClearUIFonts(void* Context = nullptr);

/** 取已登记的条目：先按 `(Context, Font, SnapFontSize(Size))` 精确命中，再退到 `(Context, Font, 0)` 兜底。
 *  未命中返回 nullptr（后端用 ImGui 缺省字体绘制）。 */
MAHO_UI_API void* FindUIFont(void* Context, const FUIName& Font, float Size);

/** 启动全烘：把**当前 ImGui 上下文**按"主题字体 × 档位"各烘一份并登记（主题字体为 None 时即
 *  后端缺省字体）。由持有上下文的宿主在 `CreateContext()` 之后、**首次取图集数据之前**调用一次
 *  —— 图集在首次取数据时才真正烘成，之后新增的条目进不去本帧图集。命名字体（如主题 `FontMono`）
 *  由宿主自行再调 `RegisterUIFont(当前上下文, …)` 补登记。 */
MAHO_UI_API void BakeUIThemeFonts();

}} // namespace Maho::UI
