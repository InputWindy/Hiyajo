#pragma once

#include "UIApi.h"
#include "UIStyle.h"
#include "UITypes.h"

#include <Core/Delegate.h>

#include <functional>
#include <vector>

namespace Maho { namespace UI {

/** 全局主题 token：组件类型默认样式与节点实例都未覆盖时的最终回退来源。
 *  编辑器主题面板直接改它（改完调一次 `NotifyThemeChanged`）。 */
struct MAHO_UI_API FUITheme
{
	FUIColor PanelFill{ 0.10f, 0.10f, 0.11f, 1.f };
	FUIColor PanelStroke{ 0.22f, 0.22f, 0.24f, 1.f };
	FUIColor ControlFill{ 0.16f, 0.16f, 0.18f, 1.f };
	FUIColor ControlHover{ 0.22f, 0.22f, 0.25f, 1.f };
	FUIColor ControlPress{ 0.28f, 0.28f, 0.31f, 1.f };
	// 字段类控件的底与描边：输入框/滑条/拖拽/颜色选择，以及弹层的窗口底。与 `Control*` 分开 ——
	// 那支是"按钮/控件"的中灰，这支是编辑器输入框那层近黑。
	FUIColor FieldFill{ 0.027f, 0.027f, 0.027f, 1.f };
	FUIColor FieldHover{ 0.055f, 0.055f, 0.055f, 1.f };
	FUIColor FieldStroke{ 0.16f, 0.16f, 0.16f, 1.f };
	FUIColor Accent{ 0.26f, 0.59f, 0.98f, 1.f };
	FUIColor Text{ 0.86f, 0.86f, 0.88f, 1.f };
	FUIColor TextDisabled{ 0.45f, 0.45f, 0.47f, 1.f };
	FUIColor Border{ 0.28f, 0.28f, 0.30f, 1.f };
	FUIColor Separator{ 0.24f, 0.24f, 0.26f, 1.f };

	float Radius = 4.f;
	float StrokeWidth = 1.f;
	float FontSize = 14.f;                       // 缺省字号（须是档位集合里的一档）

	/** 字号档位集合：图集按 `(字体资源 FUIName, 档位)` 各烘一份，节点上的任意字号绘制前吸附最近档。
	 *  改档位集合 = 改主题，但图集只在启动烘制 -> **重启后生效**。 */
	std::vector<float> FontSizeSteps{ 11.f, 13.f, 14.f, 16.f, 20.f, 28.f };

	// 资源引用（FName）：主题可整体替换字体与内置图标
	FUIName Font{};                              // 全局缺省字体（None = 后端缺省字体）
	FUIName FontMono{};                          // 等宽字体（日志/控制台）
	FUIName IconChevron{};                       // 折叠/树展开箭头
	FUIName IconCheck{};                         // 复选框勾
	FUIName IconFolder{};                        // 内容浏览器目录
	FUIName IconAsset{};                         // 内容浏览器资产
	FUIName IconSearch{};                        // 搜索框
	FUIName IconClose{};                         // 关闭/清空按钮
	FUIName IconDragHandle{};                    // 拖拽手柄
};

/** 唯一实例（本 DLL 内；与 `GetUIViewRegistry()` 同形，无 `static Get()`）。 */
MAHO_UI_API FUITheme& GetUITheme();

MAHO_UI_API FSubscriptionID SubscribeThemeChanged(std::function<void()> Handler);
MAHO_UI_API void NotifyThemeChanged();

/** 主题换代计数器：主题被改写并 `NotifyThemeChanged()` 后递增。
 *  组件库的类型默认样式按它惰性重建（静态样式缓存不会吃到换代前的旧 token）。 */
MAHO_UI_API std::uint32_t GetUIThemeStamp();

/** 任意字号吸附到最近档位（不修改输入值；集合为空时原样返回）。越档时只记一条诊断。 */
MAHO_UI_API float SnapFontSize(float Size);

/** 把主题 token 同步到后端自带控件的配色（ImGui 内部控件；实现活在翻译后端里）。 */
MAHO_UI_API void ApplyThemeToImGuiStyle();

}} // namespace Maho::UI
