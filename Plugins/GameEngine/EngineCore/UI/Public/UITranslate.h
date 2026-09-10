#pragma once

#include "UIApi.h"
#include "UITypes.h"
#include "UIView.h"

#include <cstdint>

namespace Maho { namespace UI {

/** 宿主/渲染特性调用的翻译入口参数。
 *  两处调用点：
 *    1) 编辑器面板：宿主已 `Begin(窗口)` 后调 `TranslateView(View)`（内容区 = 当前窗口内容区）
 *    2) 游戏叠加层：`UIFeature` 在 NewFrame 与 Render 之间调 `TranslateRegisteredViews(Desc)` */
struct FUIViewFrameDesc
{
	void*   ImGuiContext = nullptr;   // 目标上下文（opaque，编辑器与游戏各一个）
	float   DisplayWidth = 0.f;       // 无外壳路径的显示区尺寸
	float   DisplayHeight = 0.f;
	bool    bDrawDebug = false;       // 绘制节点矩形与状态（调试用）
	std::uint32_t DockSpaceId = 0;    // 非 0 时对外壳视图首帧 `SetNextWindowDockID`（宿主 dockspace）
	FUIName OnlyView{};               // 非 None 时只翻译该视图
};

/** 在宿主已 `Begin` 的窗口内翻译一个视图（编辑器面板路径）。
 *  调用者须持有 `FUIView` 的所有权线程上下文；翻译内部取共享锁。 */
MAHO_UI_API void TranslateView(FUIView& View);

/** 遍历注册表，翻译所有 render context 匹配的视图（游戏叠加层路径）。返回翻译的视图数。 */
MAHO_UI_API std::uint32_t TranslateRegisteredViews(const FUIViewFrameDesc& Desc);

}} // namespace Maho::UI
