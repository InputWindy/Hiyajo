#pragma once

#include "UIApi.h"
#include "FUIBuilder.h"
#include "UICanvas.h"
#include "UIEvent.h"
#include "UITypes.h"

#include <Core/Delegate.h>

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

namespace Maho { namespace UI {

class FUIView;

/** 树的所有权/线程模式：决定 `Edit()` 是真的取独占锁，还是只走一次无竞争锁。 */
enum class EUIOwnership : std::uint8_t
{
	SameThread,     // 编辑器面板：构建与翻译同线程
	CrossThread     // 游戏：所有者线程构建，渲染线程翻译
};

/** 外壳的窗口标志：UI 自有位掩码（公开头不出现 ImGui 类型），由翻译器映射为 `ImGuiWindowFlags`。 */
enum class EUIShellFlags : std::uint32_t
{
	None            = 0,
	NoCollapse      = 1u << 0,
	NoMove          = 1u << 1,
	NoResize        = 1u << 2,
	NoScrollbar     = 1u << 3,
	NoTitleBar      = 1u << 4,
	NoSavedSettings = 1u << 5
};

MAHO_UI_API EUIShellFlags operator|(EUIShellFlags A, EUIShellFlags B);
MAHO_UI_API EUIShellFlags operator&(EUIShellFlags A, EUIShellFlags B);
MAHO_UI_API bool HasFlag(EUIShellFlags Value, EUIShellFlags Flag);

/** 可选的真窗口外壳：宿主"按视图通用循环"照着开窗，窗口内的树照常翻译。
 *  停靠（dock id）不在其中 —— 宿主开窗时统一 `SetNextWindowDockID(自己的 dockspace id)`。 */
struct FUIViewShell
{
	bool          bEnabled = false;      // false = 无外壳（自绘窗口外观 / HUD / 浮层）
	std::string   Title;                 // 窗口标题（须稳定 —— ImGui 的 ini 记忆按它做键）
	FUIVector2    DefaultPos{};          // 首次打开的位置（像素）
	FUIVector2    DefaultSize{};         // 首次打开的大小；0 = 由内容决定
	EUIShellFlags Flags = EUIShellFlags::None;
	/** 锚点比例（0..1，0 = 不用）：游戏侧 HUD 的"按显示区比例"语义。
	 *  位置只在首次生效（其后可拖动），尺寸每帧按比例设 —— 与旧 `FUIWidget` 的
	 *  `AnchorX/Y`（Once）+ `SizeX/Y`（Always）逐字对齐。编辑器面板不用（交给 ini 记忆）。 */
	FUIVector2    PosFraction{};
	FUIVector2    SizeFraction{};
};

/** 结构/样式变更作用域（RAII）。跨线程模式下取独占锁，与翻译线程的共享读互斥。 */
class MAHO_UI_API FUIEditScope
{
public:
	explicit FUIEditScope(FUIView& InView);
	~FUIEditScope();

	FUIEditScope(const FUIEditScope&) = delete;
	FUIEditScope& operator=(const FUIEditScope&) = delete;

	FUIBuilder& GetRoot() const;

private:
	FUIView*                            View;
	std::unique_lock<std::shared_mutex> Lock;
};

/** 持久带状态 UI 树的所有者：根 + 显示区 + 外壳声明 + 事件抽干。 */
class MAHO_UI_API FUIView
{
public:
	explicit FUIView(FUIName InId, EUIOwnership InOwnership = EUIOwnership::SameThread);
	~FUIView();

	FUIView(const FUIView&) = delete;
	FUIView& operator=(const FUIView&) = delete;

	[[nodiscard]] FUIName GetId() const { return Id; }
	[[nodiscard]] EUIOwnership GetOwnership() const { return Ownership; }
	[[nodiscard]] FUIBuilder& GetRoot() { return *RootNode; }
	[[nodiscard]] const FUIBuilder& GetRoot() const { return *RootNode; }

	/** 变更作用域：结构/样式改动一律在其内；翻译线程只取共享锁，故两者互斥。 */
	[[nodiscard]] FUIEditScope Edit();

	/** 显示区域尺寸（面板 = 内容区，游戏 = 视口）。翻译前由宿主设置。 */
	void SetDisplaySize(float W, float H);
	[[nodiscard]] FUIVector2 GetDisplaySize() const;

	/** 翻译目标 ImGui 上下文（编辑器 / 游戏各一个）；翻译入口按它筛选视图。 */
	void SetRenderContext(void* InContext);
	[[nodiscard]] void* GetRenderContext() const;

	void SetWindowShell(bool bEnabled, std::string Title,
						FUIVector2 DefaultPos = {}, FUIVector2 DefaultSize = {},
						EUIShellFlags Flags = EUIShellFlags::None);
	[[nodiscard]] const FUIViewShell& GetWindowShell() const;

	/** 外壳锚点比例（0..1）：游戏侧 HUD 的位置/尺寸按显示区比例给，宿主翻译时逐帧换算。
	 *  与 `SetWindowShell` 同一类（结构变更），跨线程下应在 `Edit()` 区间内调用。 */
	void SetShellFractions(FUIVector2 PosFraction, FUIVector2 SizeFraction);

	/** 翻译线程：用户点了外壳关闭按钮（或 Esc）时调用 —— 只入队，不改结构。 */
	void RequestCloseWindow();

	/** 外壳关闭请求（多播）：所有者线程 `DrainEvents()` 期间触发，回调内可 `Edit()`。 */
	TMulticastEvent<void(FUIView&)> WindowClosed;

	/** 所有者线程：抽干翻译线程入队的交互事件并按路径派发回调。
	 *  调用时不得持有 `Edit()` 作用域（回调内会再次 `Edit()`）。 */
	void DrainEvents();

	/** 翻译线程：入队一条交互事件（锁保护）。 */
	void PushEvent(FUIEventRecord Record);

	/** 按 Id 查节点（递归，深度优先）。 */
	[[nodiscard]] FUIBuilder* Find(FUIName InId) const;

	// -- 翻译器内部使用 --
	[[nodiscard]] std::shared_mutex& GetTreeMutex() { return TreeMutex; }

private:
	friend class FUIEditScope;

	FUIBuilder* FindPath(const std::vector<FUIName>& Path) const;

	FUIName      Id;
	EUIOwnership Ownership;
	std::unique_ptr<FUICanvas> RootNode;
	mutable std::shared_mutex TreeMutex;

	FUIVector2   DisplaySize{ 1280.f, 720.f };
	void*        RenderContext = nullptr;
	FUIViewShell WindowShell;

	mutable std::mutex EventMutex;
	std::vector<FUIEventRecord> PendingEvents;
	bool bCloseRequested = false;
};

}} // namespace Maho::UI
