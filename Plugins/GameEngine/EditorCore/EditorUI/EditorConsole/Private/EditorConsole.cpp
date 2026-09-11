// 本 TU 的 include 链会带进 <windows.h>：先关掉 min/max 宏（先例：Asset/TextureImageCodec.cpp）。
#ifndef NOMINMAX
#	define NOMINMAX
#endif

#include "EditorConsole.h"

#include "ConsoleVariable.h"

#include <UIClipboard.h>
#include <UITheme.h>
#include <UIView.h>
#include <UIViewRegistry.h>
#include <Widgets/FUIBox.h>
#include <Widgets/FUIButton.h>
#include <Widgets/FUIInputText.h>
#include <Widgets/FUIPanel.h>
#include <Widgets/FUIPopup.h>
#include <Widgets/FUISelectable.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace Maho
{

namespace
{

/** 稳定节点 Id：同级唯一即可（事件路由走根→目标的 Id 路径）。 */
constexpr const char* kIdToolbar  = "EditorConsole.Toolbar";
constexpr const char* kIdFilter   = "EditorConsole.Filter";
constexpr const char* kIdLines    = "EditorConsole.Lines";
constexpr const char* kIdMenu     = "EditorConsole.ContextMenu";
constexpr const char* kIdMenuClear = "EditorConsole.ContextMenu.Clear";
constexpr const char* kIdSuggest  = "EditorConsole.Suggest";
constexpr const char* kIdCmdRow   = "EditorConsole.Command";
constexpr const char* kIdCvar     = "EditorConsole.Cvar";

const char* LevelName(ELogLevel Level)
{
	switch (Level)
	{
	case ELogLevel::Trace:    return "Trace";
	case ELogLevel::Debug:    return "Debug";
	case ELogLevel::Info:     return "Info";
	case ELogLevel::Warn:     return "Warning";
	case ELogLevel::Error:    return "Error";
	case ELogLevel::Critical: return "Critical";
	}
	return "Info";
}

// Severity -> text color. Mirrors spdlog's default terminal palette (the `%^%l%$`
// ANSI colors): trace=white, debug=cyan, info=green, warn=yellow, error=red,
// critical=magenta -- so the panel reads the same way as the 黑框 stdout sink.
// Values are softened toward the dark panel background for readability. Written as an
// instance style override on the line's `Normal` group, so it wins over the type's
// hovered/pressed text tokens for every state (same reach as the old per-line
// text-color push).
UI::FUIColor LevelColor(ELogLevel Level)
{
	switch (Level)
	{
	case ELogLevel::Trace:    return { 0.85f, 0.85f, 0.85f, 1.0f }; // white
	case ELogLevel::Debug:    return { 0.35f, 0.85f, 0.90f, 1.0f }; // cyan
	case ELogLevel::Info:     return { 0.30f, 0.85f, 0.40f, 1.0f }; // green
	case ELogLevel::Warn:     return { 0.95f, 0.85f, 0.25f, 1.0f }; // yellow
	case ELogLevel::Error:    return { 0.90f, 0.35f, 0.35f, 1.0f }; // red
	case ELogLevel::Critical: return { 0.90f, 0.30f, 0.90f, 1.0f }; // magenta
	}
	return { 0.85f, 0.85f, 0.85f, 1.0f };
}

/** 逐行节点的 Id：按行号生成，只增不减（行数变化不改变既有行的身份）。 */
UI::FUIName MakeLineId(std::size_t Index)
{
	return UI::FUIName(std::string("EditorConsole.Line.") + std::to_string(Index));
}

/** 自动补全候选行的 Id（最多 8 行，逐帧重建故可复用同一串）。 */
UI::FUIName MakeSuggestId(int Index)
{
	return UI::FUIName(std::string("EditorConsole.Suggest.") + std::to_string(Index));
}

/** 取（必要时新建）子节点，并报告它是否是本帧新建的 —— 新建才需要播种默认配置
 *  （输入框初值 / 事件订阅；逐帧重来会累积订阅）。 */
template <typename T>
T& Ensure(UI::FUIBuilder& Parent, UI::FUIName Id, bool& bOutCreated)
{
	bOutCreated = (Parent.FindChild(Id) == nullptr);
	return Parent.AddItem<T>(Id);
}

} // namespace

void FEditorConsole::Init(FExampleEditor&)
{
	if (ListenerId != 0)
	{
		return;
	}
	// Producer thread pushes into a mutex-guarded deque; the panel drains it on
	// the frame thread in Update, so the block is bounded by MaxLines (drop head).
	if (FLog* Log = GetLog())
	{
		ListenerId = Log->OnLog.Bind([this](const FLogMessage& Msg)
		{
			std::lock_guard<std::mutex> Lock(LinesMutex);
			if (Lines.size() >= MaxLines)
			{
				Lines.pop_front();
				++DroppedCount;
			}
			Lines.push_back({ Msg.Level, Msg.Category, Msg.Message });
		});
	}
}

UI::FUIView* FEditorConsole::EnsureView(FExampleEditor& Editor)
{
	if (View != nullptr)
	{
		return View.get();
	}

	// 注册表由 UI 插件发布；插件未起来（或已关）时返回 nullptr，下一帧再试。
	UI::FUIViewRegistry* Registry = UI::GetUIViewRegistry();
	if (Registry == nullptr)
	{
		return nullptr;
	}

	std::unique_ptr<UI::FUIView> NewView = std::make_unique<UI::FUIView>(UI::FUIName("EditorConsole"));
	// 外壳开窗（含宿主自己的 dockspace id）由宿主通用循环做；标题即旧窗口名。
	// 关闭框归宿主所有：旧版也没有绑定关闭语义，故这里不订阅 `WindowClosed`。
	NewView->SetWindowShell(true, "Console", UI::FUIVector2{}, UI::FUIVector2{}, UI::EUIShellFlags::NoCollapse);
	NewView->SetRenderContext(Editor.GetUIRenderContext());
	Registry->RegisterView(*NewView);
	View = std::move(NewView);
	return View.get();
}

void FEditorConsole::ExecuteCvarLine()
{
	// 命令行回显：直接进日志环形缓冲（与 Log 层同一条数据流，下一帧可见）。
	const auto EchoLog = [this](ELogLevel Level, std::string Text)
	{
		std::lock_guard<std::mutex> Lock(LinesMutex);
		if (Lines.size() >= MaxLines)
		{
			Lines.pop_front();
			++DroppedCount;
		}
		Lines.push_back({ Level, std::string("CVar"), std::move(Text) });
	};

	std::string Line = CvarBuffer;
	CvarBuffer[0] = '\0';

	const auto FirstNonSpace = [](std::string_view S)
	{
		std::size_t I = 0;
		while (I < S.size() && (S[I] == ' ' || S[I] == '\t')) { ++I; }
		return I;
	};
	const auto LastNonSpace = [](std::string_view S)
	{
		std::size_t I = S.size();
		while (I > 0 && (S[I - 1] == ' ' || S[I - 1] == '\t')) { --I; }
		return I;
	};

	const std::size_t Begin = FirstNonSpace(Line);
	const std::size_t End = LastNonSpace(Line);
	Line = (Begin < End) ? Line.substr(Begin, End - Begin) : std::string();

	if (Line.empty())
	{
		return;
	}

	// 历史（↑ 列表）：只记真正执行过的行；与上一条完全相同不重复记。执行完即收起列表。
	if (History.empty() || History.back() != Line)
	{
		History.push_back(Line);
		while (History.size() > MaxHistory) { History.pop_front(); }
	}
	HistoryIndex = -1;
	// 候选列表的走动状态一并作废：它钉住的过滤词属于执行掉的这一段输入，挂着会让列表带着
	// 刚执行完的那批候选留在空框上。
	SuggestIndex = -1;
	SuggestNeedle[0] = '\0';
	SuggestOffset = 0;

	// "name [value]": a bare name prints the current value, "name value" sets it.
	const std::size_t Space = Line.find_first_of(" \t");
	const std::string Name = Line.substr(0, Space);
	std::string Value = (Space == std::string::npos) ? std::string() : Line.substr(Space + 1);
	// "name   value" would otherwise keep the extra separators in the stored string.
	const std::size_t VBeg = Value.find_first_not_of(" \t");
	Value = (VBeg == std::string::npos) ? std::string() : Value.substr(VBeg);

	if (ConsoleVariable::IConsoleVariable* CVar = ConsoleVariable::FConsoleVariable::Get().Find(Name))
	{
		if (!Value.empty())
		{
			CVar->Set(Value);
		}
		EchoLog(ELogLevel::Info, Name + " = " + CVar->GetString());
	}
	else
	{
		EchoLog(ELogLevel::Error, "Unknown cvar: " + Name);
	}
}

void FEditorConsole::FillFromList(const std::string& Text)
{
	std::strncpy(CvarBuffer, Text.c_str(), sizeof(CvarBuffer) - 1);
	CvarBuffer[sizeof(CvarBuffer) - 1] = '\0';
	// 记成"刚选过的名字"：与候选补全同一条守卫，避免补全列表压在它自己那一行上重开。
	std::strncpy(CvarPickedName, CvarBuffer, sizeof(CvarPickedName) - 1);
	CvarPickedName[sizeof(CvarPickedName) - 1] = '\0';
	CvarAuthoritative = true;   // 本帧缓冲是权威值：把它写回节点（挡住节点旧文本的回读）
	CvarPendingFocus = true;    // 焦点留在命令行：可以接着打字 / 回车执行
}

void FEditorConsole::FillFromHistory()
{
	if (HistoryIndex < 0 || HistoryIndex >= static_cast<int>(History.size()))
	{
		return;
	}
	FillFromList(History[static_cast<std::size_t>(HistoryIndex)]);
	CvarDropdownOpen = false;   // 补全列表与历史列表互斥
}

void FEditorConsole::StepHistory(int Step)
{
	// 守卫：过滤框正在收键盘（那时 ↑ 属于过滤框，不该翻命令历史）、历史空。命令框自己有没有在
	// 收键盘由调用方在分派处挡（见 `Update` 里 ↑/↓ 的总闸）。
	if (FilterEditing || History.empty())
	{
		return;
	}

	if (HistoryIndex < 0)
	{
		// ↑ 展开：高亮最新一条（列表最下一行）。
		if (Step >= 0) { return; }
		HistoryIndex = static_cast<int>(History.size()) - 1;
	}
	else
	{
		const int Next = HistoryIndex + Step;
		if (Next < 0 || Next >= static_cast<int>(History.size())) { return; }   // 到头停住
		HistoryIndex = Next;
	}
	FillFromHistory();
}

void FEditorConsole::StepSuggest(int Step, const std::vector<std::string>& Matches)
{
	if (Matches.empty())
	{
		return;
	}
	const int Count = static_cast<int>(Matches.size());
	if (SuggestIndex < 0)
	{
		// 进入：↑ 落在最下一行（贴着命令行那头，与历史列表同向），↓ 落在最上一行。
		SuggestIndex = (Step < 0) ? Count - 1 : 0;
		// 钉住过滤词：这一步之后框里就是整条候选名，不能再拿它当过滤词（见 `SuggestNeedle`）。
		std::strncpy(SuggestNeedle, CvarBuffer, sizeof(SuggestNeedle) - 1);
		SuggestNeedle[sizeof(SuggestNeedle) - 1] = '\0';
	}
	else
	{
		const int Next = SuggestIndex + Step;
		if (Next < 0 || Next >= Count) { return; }   // 到头停住
		SuggestIndex = Next;
	}
	// 高亮即填回命令框。列表**不**收：钉住的过滤词让候选照旧，可以接着走。
	FillFromList(Matches[static_cast<std::size_t>(SuggestIndex)]);
}

void FEditorConsole::Update(FExampleEditor& Editor)
{
	UI::FUIView* PanelView = EnsureView(Editor);
	if (PanelView == nullptr)
	{
		return;
	}

	// -- 值回读 --------------------------------------------------------------
	// 翻译期后端原地改写节点的值（用户输入的真值在节点上），故先收回缓冲，再按缓冲
	// 声明 —— 用户编辑因此跨帧存活。建议行刚点选的那一帧除外：缓冲才是真值
	// （CvarAuthoritative），回读会把陈旧的节点文本压回去。
	FilterEditing = false;
	if (const auto* FilterNode = dynamic_cast<const UI::FUIInputText*>(PanelView->Find(UI::FUIName(kIdFilter))))
	{
		std::strncpy(FilterBuffer, FilterNode->GetValue().c_str(), sizeof(FilterBuffer) - 1);
		FilterBuffer[sizeof(FilterBuffer) - 1] = '\0';
		FilterEditing = FilterNode->GetState().bPressed;   // 同上：过滤框的活跃位
	}

	CvarEditing = false;
	if (const auto* CvarNode = dynamic_cast<const UI::FUIInputText*>(PanelView->Find(UI::FUIName(kIdCvar))))
	{
		CvarEditing = CvarNode->GetState().bPressed;   // 旧 IsItemActive()：后端写回的活跃位
		if (!CvarAuthoritative)
		{
			std::strncpy(CvarBuffer, CvarNode->GetValue().c_str(), sizeof(CvarBuffer) - 1);
			CvarBuffer[sizeof(CvarBuffer) - 1] = '\0';
		}
	}

	// Case-insensitive filter needle, computed once per frame from the box text.
	std::string NeedleLower;
	for (const char* p = FilterBuffer; *p; ++p)
	{
		NeedleLower += static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
	}
	const bool HasFilter = !NeedleLower.empty();

	// Builds the full display line (category + level + message) for a log entry.
	const auto BuildLine = [](const FLogEntry& E) -> std::string
	{
		std::string S;
		if (!E.Category.empty())
		{
			S += E.Category;
			S += ": ";
		}
		S += LevelName(E.Level);
		S += ": ";
		S += E.Message;
		return S;
	};

	// -- 快照：只取可见（过滤后）的行，并结清本帧的头丢弃计数 ----------------
	std::vector<FLogEntry> Snapshot;
	std::size_t DroppedThisFrame = 0;
	{
		std::lock_guard<std::mutex> Lock(LinesMutex);
		DroppedThisFrame = DroppedCount;
		DroppedCount = 0;
		if (!HasFilter)
		{
			Snapshot.assign(Lines.begin(), Lines.end());
		}
		else
		{
			for (const FLogEntry& E : Lines)
			{
				std::string T = BuildLine(E);
				for (char& c : T)
				{
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				}
				if (T.find(NeedleLower) != std::string::npos)
				{
					Snapshot.push_back(E);
				}
			}
		}
	}

	// -- CVar 命令执行 -------------------------------------------------------
	// Run 按钮 / 回车的事件在 `Update` 之前抽干，此刻缓冲刚回读完毕：先执行（会清空缓冲），
	// 再把清空后的文本写回节点（CvarAuthoritative）。
	if (CvarRunRequested)
	{
		CvarRunRequested = false;
		ExecuteCvarLine();
		CvarAuthoritative = true;
	}

	// -- 复制（Ctrl+C）-------------------------------------------------------
	// 日志面板自身声明了 Ctrl+C / Ctrl+A 两条快捷键（见下面的 `OnShortcut`）：Ctrl+A 只把选中
	// 区间拉到全部可见行，Ctrl+C 落到这里复制该区间（"复制全部" = Ctrl+A 之后再 Ctrl+C）。
	if (CopyRequested)
	{
		std::string Text;
		if (SelAnchor >= 0 && SelEnd >= 0)
		{
			const int Lo = SelAnchor < SelEnd ? SelAnchor : SelEnd;
			const int Hi = SelAnchor > SelEnd ? SelAnchor : SelEnd;
			for (int i = Lo; i <= Hi && i < static_cast<int>(Snapshot.size()); ++i)
			{
				Text += BuildLine(Snapshot[static_cast<std::size_t>(i)]);
				Text += '\n';
			}
		}
		UI::SetUIClipboardText(Text);
		CopyRequested = false;
	}

	// -- 全选（Ctrl+A）-------------------------------------------------------
	// 只改选中区间：行的选中态由 SelAnchor/SelEnd 驱动（区间越界由下面的声明按快照裁剪）。
	if (SelectAllRequested)
	{
		SelectAllRequested = false;
		SelAnchor = Snapshot.empty() ? -1 : 0;
		SelEnd = static_cast<int>(Snapshot.size()) - 1;
	}

	// 上一帧实测的行高：旧 `GetTextLineHeight()` 的替代（从头丢弃补偿要用它）。
	float LineH = UI::GetUITheme().FontSize * 1.5f;
	if (!LineIds.empty())
	{
		if (const UI::FUIBuilder* Probe = PanelView->Find(LineIds.back()))
		{
			if (Probe->GetRect().H > 0.f) { LineH = Probe->GetRect().H; }
		}
	}

	// Auto-scroll to the newest line ONLY if the user is already pinned to the bottom,
	// so a manual scroll-up to read a long back-log is not fought every frame.
	// （滚动量是翻译期写回节点的运行期状态，这里读到的是上一帧的值 —— 与旧版在
	//   BeginChild 之后读 GetScrollY() 的时序等价。）
	bool NearBottom = true;
	if (const UI::FUIBuilder* LinesProbe = PanelView->Find(UI::FUIName(kIdLines)))
	{
		NearBottom = LinesProbe->GetScrollY() >= LinesProbe->GetScrollMaxY() - 1.0f;
	}

	// -- 拖拽扩选（读上一帧的命中状态）---------------------------------------
	// 按下一行后按钮被 ImGui 捕获在该行（活跃位此后恒真），指针扫过其它行只改"悬停位"，
	// 故"有行活跃 + 另一行悬停"等价旧版 `IsMouseDragging && SelAnchor >= 0` 的逐行扩选。
	{
		int Pressed = -1;
		int Hovered = -1;
		for (std::size_t i = 0; i < Snapshot.size() && i < LineIds.size(); ++i)
		{
			const UI::FUIBuilder* LineNode = PanelView->Find(LineIds[i]);
			if (LineNode == nullptr) { continue; }
			if (LineNode->GetState().bPressed) { Pressed = static_cast<int>(i); }
			if (LineNode->GetState().bHovered) { Hovered = static_cast<int>(i); }
		}
		if (Pressed >= 0 && Hovered >= 0 && Hovered != Pressed)
		{
			if (SelAnchor < 0) { SelAnchor = Pressed; }
			SelEnd = Hovered;
		}
	}

	// -- CVar 自动补全候选 ---------------------------------------------------
	// While the user is typing, gather every registered cvar whose name string-matches
	// the box text (case-insensitive substring).
	// 过滤词：平时就是命令框里的文本；正在用 ↑/↓ 走候选时钉住开始走动那一段（见
	// `SuggestNeedle`）—— 走动会把候选名整条填回框，跟着框走的话过滤词就变成那个完整名字，
	// 候选只剩它自己、再被下面"完整名字即收"清空，列表当场塌掉。
	const char* NeedleSource = (SuggestIndex >= 0 && SuggestNeedle[0] != '\0') ? SuggestNeedle : CvarBuffer;
	const bool HasTyping = (NeedleSource[0] != '\0');
	std::vector<std::string> Matches;
	if (HasTyping)
	{
		std::string Needle;
		for (const char* p = NeedleSource; *p; ++p)
		{
			Needle += static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
		}
		ConsoleVariable::FConsoleVariable::Get().VisitAll([&](ConsoleVariable::IConsoleVariable& CVar)
		{
			std::string Name(CVar.GetName());
			std::string Lower = Name;
			for (char& C : Lower)
			{
				C = static_cast<char>(std::tolower(static_cast<unsigned char>(C)));
			}
			if (Lower.find(Needle) != std::string::npos)
			{
				Matches.push_back(std::move(Name));
			}
		});
		std::sort(Matches.begin(), Matches.end());
	}

	// Once the box holds a complete cvar name, drop the list (don't re-show the single
	// exact match the user just picked). 正在走候选时除外：那时框里本来就是高亮那条完整名。
	if (SuggestIndex < 0 && Matches.size() == 1 && Matches.front() == CvarBuffer)
	{
		Matches.clear();
	}

	// 列表开合：旧版还看"鼠标是否悬在列表上"，新树的事件不携带指针位置，故只按
	// "输入框活跃 + 有候选" 判定（点选后输入框失活 -> 下一帧自动收）。
	// 例外：刚点选的那条名字还留在框里时**不重开**。点选后焦点交回输入框（`CvarPendingFocus`），
	// 输入框重新变成活跃项并一直保持，若仍按"活跃 + 有字"判定，列表在点选后的每一帧都会重开
	// —— 点谁谁来，用户离不开这张表（编辑器的"卡死"观感）。改字即解除守卫。
	const bool bPickedNameIntact = (std::strcmp(CvarBuffer, CvarPickedName) == 0);
	if (!CvarDropdownOpen)
	{
		CvarDropdownOpen = CvarEditing && HasTyping && !bPickedNameIntact;
	}
	else if (!HasTyping || !CvarEditing)
	{
		CvarDropdownOpen = false;
	}
	if (Matches.empty())
	{
		CvarDropdownOpen = false;
	}

	// 历史列表（↑）的收合：输入框不再是活跃项（焦点走了）或历史空了就收；改字由
	// `OnTextChanged` 订阅收。与补全列表互斥，故这里顺手把补全关掉。
	if (HistoryIndex >= 0 && (!CvarEditing || History.empty()))
	{
		HistoryIndex = -1;
	}
	if (HistoryIndex >= 0)
	{
		CvarDropdownOpen = false;
	}

	// ↑/↓ 的一步：回调在事件抽干时就跑（那时候选还没算出来），故只记下"要走一步"，由本帧的
	// 两张列表状态分派 —— 候选列表在显示就走候选，否则走历史。
	// 总闸：命令框必须正拿着键盘（光标在框里）。箭头键是**输入框正在收键盘**时才轮到命令行的那
	// 一组键，框没进入编辑态时它属于视图导航/滚动，这里直接丢弃这一步（也不动任何行走状态）。
	if (PendingStep != 0)
	{
		const int Step = PendingStep;
		PendingStep = 0;
		if (!CvarEditing)
		{
			// 丢弃这一步：不动任何行走状态。
		}
		else if (CvarDropdownOpen && !Matches.empty())
		{
			StepSuggest(Step, Matches);
		}
		else
		{
			// 没候选可走（空框 / 没有匹配）：这一步属于历史列表，候选的走动状态让位（两个列表
			// 互斥）。历史那条路自带空历史的守卫。
			SuggestIndex = -1;
			SuggestNeedle[0] = '\0';
			SuggestOffset = 0;
			StepHistory(Step);
		}
	}

	// 走动状态的作废：候选列表收掉了（焦点走了 / 没候选）就不能再挂着高亮，否则高亮会在列表
	// 重开时凭空落在某个候选上。
	if (SuggestIndex >= 0 && (!CvarDropdownOpen || Matches.empty() || !CvarEditing))
	{
		SuggestIndex = -1;
		SuggestNeedle[0] = '\0';
		SuggestOffset = 0;
	}

	// 弹层行：↑ 展开的历史列表优先（两者互斥），否则是输入中的候选名字。
	// 上限：候选 8 行（旧版一屏）；历史 10 行（"最近输入的 10 条"）。
	const bool bHistoryList = (HistoryIndex >= 0);
	std::vector<std::string> Rows;
	if (bHistoryList)
	{
		Rows.assign(History.begin(), History.end());   // 最新一条在末尾 = 列表最下一行
	}
	else
	{
		Rows = Matches;
	}
	const int MaxRows = bHistoryList ? static_cast<int>(MaxHistory) : 8;
	const int ShowRows = std::min(static_cast<int>(Rows.size()), MaxRows);

	// 高亮行（历史 = `HistoryIndex`，候选 = `SuggestIndex`）。显示行号 = 起点 + 行内偏移，高亮/
	// 点选都按它折算。历史一屏放得下 10 条，起点恒为 0；候选一屏 8 行且可以多过一屏，起点跨帧
	// 记在 `SuggestOffset` 里 —— 只在高亮要走出窗口时挪一格（见该成员：逐帧按"把高亮钉在窗口
	// 边上"重算，内容会跟着按键反向滑动，↑ 看起来也是往下）。
	const int Highlight = bHistoryList ? HistoryIndex : SuggestIndex;
	int RowOffset = 0;
	if (!bHistoryList && Highlight >= 0)
	{
		const int MaxOffset = std::max(static_cast<int>(Rows.size()) - ShowRows, 0);
		if (SuggestOffset < Highlight - (ShowRows - 1)) { SuggestOffset = Highlight - (ShowRows - 1); }   // 走出下沿
		else if (SuggestOffset > Highlight) { SuggestOffset = Highlight; }                               // 走出上沿
		SuggestOffset = std::min(std::max(SuggestOffset, 0), MaxOffset);
		RowOffset = SuggestOffset;
	}

	// ---- 声明期：只改本视图的树 -------------------------------------------
	UI::FUIEditScope Scope = PanelView->Edit();
	UI::FUIBuilder& Root = Scope.GetRoot();
	Root.Layout().SetDirection(UI::EUIDirection::Column);
	Root.Layout().SetSpacing(4.f);

	bool bNew = false;

	// 工具栏行：只留过滤框。旧版这一行还有个 Clear 按钮，现在清空走日志面板的右键菜单
	// （`kIdMenuClear`）。行本身仍在外壳的 WindowBg 上，故行内只放控件。
	{
		UI::FUIBox& Toolbar = Ensure<UI::FUIBox>(Root, UI::FUIName(kIdToolbar), bNew);
		Toolbar.Layout().SetDirection(UI::EUIDirection::Row);
		Toolbar.Layout().SetSpacing(6.f);
		Toolbar.Layout().VerticalAlign = UI::EUIAlign::Stretch;   // 行内控件等高
		Toolbar.Layout().SetSize(UI::FUILength::Fill(), UI::FUILength::Content());

		UI::FUIInputText& FilterBox = Ensure<UI::FUIInputText>(Toolbar, UI::FUIName(kIdFilter), bNew);
		FilterBox.SetHint("Filter logs...");
		FilterBox.SetMaxLength(sizeof(FilterBuffer) - 1);
		if (bNew)
		{
			FilterBox.SetValue(FilterBuffer);   // 之后以用户输入为准（不再逐帧压回）
		}
		FilterBox.Layout().SetSize(UI::FUILength::Fixed(220.f), UI::FUILength::Content());
	}

	// 日志主体：旧版是"底部预留一行命令框高度"的带边框子窗口。
	// 高度 Fill = 同级的其余项都是内容高，主体自然拿到"剩余空间"。
	UI::FUIPanel& LinesPanel = Ensure<UI::FUIPanel>(Root, UI::FUIName(kIdLines), bNew);
	LinesPanel.SetChrome(UI::EUIPanelChrome::Full);
	LinesPanel.SetScrollable(true);
	LinesPanel.Layout().SetSize(UI::FUILength::Fill(), UI::FUILength::Fill());
	LinesPanel.Layout().SetSpacing(0.f);   // 旧 PushStyleVar(ItemSpacing, 0)

	// 右键菜单区域：面板矩形内的右键由翻译期回写（纯几何命中，见 `EUIInputFlags::ContextMenu`
	// —— 滚动容器的 item 会被内容子窗口挡掉，菜单要的恰是整个矩形）。这里读上一帧那次右键的
	// 指针位置当锚点：`FUIPopup` 把弹层左下角贴到锚点左上角，锚点是零尺寸的点 ⇒ 左下角在指针处。
	LinesPanel.SetContextMenu(true);
	if (LinesPanel.GetState().bSecondaryClicked)
	{
		ContextMenuAnchor = LinesPanel.GetState().PointerPos;
		bContextMenuOpen = true;
	}

	// 面板级快捷键（声明式，见 `UI::FUIKeyChord`）：Ctrl+C 复制选中区间、Ctrl+A 全选可见行。
	// 挂在日志面板上 = "面板在翻译（可见）时才响应"；后端的守卫还要求键盘焦点在本视图窗口
	// 且当前没有文本输入在收键盘，故在过滤框/命令行里打字不会误触发。
	// 两条键共用一个回调并按载荷（`ToString()`）认领：`Shortcut` 事件是本节点的多播，命中哪一条
	// 都会送到该节点的全部订阅者，逐键各挂一个回调的话按 Ctrl+A 会顺手把复制也跑一遍
	// （复制的是上一段旧选区），按 Ctrl+C 则顺手全选。
	if (bNew)
	{
		const UI::FUIKeyChord CopyChord{ 'C', UI::EUIModifiers::Ctrl };
		const UI::FUIKeyChord SelectAllChord{ 'A', UI::EUIModifiers::Ctrl };
		const UI::FUITextEventHandler PanelChord = [this, Copy = CopyChord.ToString(), SelectAll = SelectAllChord.ToString()](
			UI::FUIBuilder&, std::string_view Pressed)
		{
			const std::string Chord(Pressed);
			if (Chord == Copy)           { CopyRequested = true; }
			else if (Chord == SelectAll) { SelectAllRequested = true; }
		};
		LinesPanel.OnShortcut(CopyChord, PanelChord);
		LinesPanel.OnShortcut(SelectAllChord, PanelChord);
	}

	// 逐行一个 FUISelectable。旧版的 PushID(i) 是为了让同文本的行不撞 Id；这里的行 Id
	// 本来就带行号，天然唯一。逐帧重建子节点：行数（受过滤器影响）每帧都可能变。
	while (LineIds.size() < Snapshot.size())
	{
		LineIds.push_back(MakeLineId(LineIds.size()));
	}

	const int LineCount = static_cast<int>(Snapshot.size());
	const int SelLo = (SelAnchor >= 0 && SelEnd >= 0) ? (SelAnchor < SelEnd ? SelAnchor : SelEnd) : -1;
	const int SelHi = (SelAnchor >= 0 && SelEnd >= 0) ? (SelAnchor > SelEnd ? SelAnchor : SelEnd) : -1;

	LinesPanel.ResetChildren();
	for (int i = 0; i < LineCount; ++i)
	{
		const FLogEntry& E = Snapshot[static_cast<std::size_t>(i)];
		UI::FUISelectable& LineNode = LinesPanel.AddItem<UI::FUISelectable>(LineIds[static_cast<std::size_t>(i)]);
		LineNode.SetLabel(BuildLine(E));
		LineNode.SetSpanAll(true);
		LineNode.Style()[UI::EUIState::Normal].Text = LevelColor(E.Level);
		// 选中高亮走基类的选中位（解析样式按它取 Selected 组）。
		LineNode.SetSelected(i >= SelLo && i <= SelHi);
		LineNode.OnSelected([this, i](UI::FUIBuilder& Node)
		{
			// 修饰键随命中那一刻的事件一起送到（`BroadcastEvent` 先落到节点再派发）：
			// Shift = 从锚点扩选，其余 = 重新落锚点。
			if (UI::HasModifier(Node.GetLastModifiers(), UI::EUIModifiers::Shift))
			{
				if (SelAnchor < 0) { SelAnchor = i; }
				SelEnd = i;
				return;
			}
			SelAnchor = i;
			SelEnd = i;
		});
	}

	// 命令行行：只有 CVar 输入框（回车提交执行）。旧版这一行右侧还有个 Run 按钮，
	// 与回车走同一段执行逻辑，已删。
	UI::FUIBox& CmdRow = Ensure<UI::FUIBox>(Root, UI::FUIName(kIdCmdRow), bNew);
	CmdRow.Layout().SetDirection(UI::EUIDirection::Row);
	CmdRow.Layout().SetSpacing(6.f);
	CmdRow.Layout().VerticalAlign = UI::EUIAlign::Stretch;
	CmdRow.Layout().SetSize(UI::FUILength::Fill(), UI::FUILength::Content());

	bool bNewCvar = false;
	UI::FUIInputText& CvarBox = Ensure<UI::FUIInputText>(CmdRow, UI::FUIName(kIdCvar), bNewCvar);
	CvarBox.SetHint("cvar, e.g. r.MaxFPS 120");
	CvarBox.SetMaxLength(sizeof(CvarBuffer) - 1);
	if (bNewCvar || CvarAuthoritative)
	{
		CvarBox.SetValue(CvarBuffer);
	}
	if (bNewCvar)
	{
		// 回车提交：后端只在真按了回车时报告（文本改动另走 TextChanged）。
		CvarBox.OnSubmitted([this](UI::FUIBuilder&, std::string_view) { CvarRunRequested = true; });
		// 一开始改字就收起 ↑ 历史列表（它只服务于"直接挑一条"），候选的走动状态也一并作废
		// （它钉住的过滤词属于上一段输入）。
		CvarBox.OnTextChanged([this](UI::FUIBuilder&, std::string_view)
		{
			HistoryIndex = -1;
			SuggestIndex = -1;
			SuggestNeedle[0] = '\0';
			SuggestOffset = 0;
		});
		// ↑/↓：命名键，走声明式快捷键（后端把 ↑ 映射成 UpArrow；命名键不受"输入框在收键盘"
		// 那道守卫限制，否则输入框里的 ↑ 永远不命中）。回调此刻还不知道候选有没有、有几个，
		// 故只记下这一步，分派留给 `Update` 里那段 —— 有候选就走候选，没有就走历史。
		// 两条键共用一个回调并按载荷（`FUIKeyChord::ToString()`）认领：同一个 `Shortcut` 事件是
		// 本节点的**多播**，命中任意一条快捷键都会把它送给该节点的全部订阅者，逐键各挂一个回调
		// 的话按 ↑ 会先跑 ↑ 的那个、再跑 ↓ 的那个 —— `PendingStep` 被后者盖成 +1，"往上翻历史"
		// 就永远走了"往下、列表关着即返回"那条路（历史列表只剩 ↓ 语义、且从不打开）。
		const UI::FUIKeyChord UpChord = UI::FUIKeyChord::NamedKey(UI::EUIKey::Up);
		const UI::FUIKeyChord DownChord = UI::FUIKeyChord::NamedKey(UI::EUIKey::Down);
		const UI::FUITextEventHandler StepChord = [this, Up = UpChord.ToString(), Down = DownChord.ToString()](
			UI::FUIBuilder&, std::string_view Pressed)
		{
			const std::string Chord(Pressed);
			if (Chord == Up)        { PendingStep = -1; }
			else if (Chord == Down) { PendingStep = 1; }
		};
		CvarBox.OnShortcut(UpChord, StepChord);
		CvarBox.OnShortcut(DownChord, StepChord);
	}
	CvarBox.Layout().SetSize(UI::FUILength::Fill(), UI::FUILength::Content());

	// 弹层（候选补全 / ↑ 历史共用同一个窗口 —— 两者互斥，故不会同时出现）：旧版是浮在输入框
	// 上方的独立窗口。`FUIPopup` 在正常流里零尺寸，后端为它开第二个窗口，故不再占版面；
	// 锚点取输入框矩形（上一帧的，与全树同序读回）。
	UI::FUIPopup& SuggestPopup = Ensure<UI::FUIPopup>(Root, UI::FUIName(kIdSuggest), bNew);
	const bool bShowSuggest = (bHistoryList || CvarDropdownOpen) && ShowRows > 0;
	SuggestPopup.SetAnchor(CvarBox.GetRect());
	SuggestPopup.SetOpen(bShowSuggest);
	if (bNew)
	{
		// 用户点外部 / Esc 关掉：后端把这条事件送到所有者线程，这里落回业务状态。
		SuggestPopup.OnClosed([this](UI::FUIBuilder&)
		{
			CvarDropdownOpen = false;
			HistoryIndex = -1;
		});
	}
	SuggestPopup.ResetChildren();
	if (bShowSuggest)
	{
		for (int i = 0; i < ShowRows; ++i)
		{
			const int RowIndex = RowOffset + i;   // 行号是折算过的显示下标（见 `RowOffset`）
			const std::string& Text = Rows[static_cast<std::size_t>(RowIndex)];
			UI::FUISelectable& Row = SuggestPopup.AddItem<UI::FUISelectable>(MakeSuggestId(i));
			Row.SetLabel(Text);
			Row.SetSpanAll(true);
			// 高亮：历史/候选都按显示行号比一次（选中态走基类的选中位，解析样式按它取 Selected 组）。
			Row.SetSelected(RowIndex == Highlight);
			Row.OnSelected([this, Name = Text, RowIndex, bIsHistory = bHistoryList](UI::FUIBuilder&)
			{
				if (bIsHistory)
				{
					// 历史条：高亮跟着走，再由与键盘同一条填充路径写回缓冲。
					HistoryIndex = RowIndex;
					FillFromHistory();
					return;
				}
				// 候选条：填回并收起列表。键盘走动不走这里 —— `StepSuggest` 只填不收敛列表
				// （它钉住过滤词），点选则是"挑完了"，钉住的过滤词一并作废。
				SuggestIndex = -1;
				SuggestNeedle[0] = '\0';
				SuggestOffset = 0;
				FillFromList(Name);
				CvarDropdownOpen = false;
			});
		}
	}

	// 右键菜单：日志区域的 Clear 入口（旧版是工具栏上的 Clear 按钮）。锚点就是右键那一刻的
	// 指针（零尺寸点锚点），故菜单左下角正落在指针处。菜单项目前只有 Clear，后续项按同一模式加。
	UI::FUIPopup& MenuPopup = Ensure<UI::FUIPopup>(Root, UI::FUIName(kIdMenu), bNew);
	if (bNew)
	{
		// 点外部 / Esc 关掉：与候选列表同一路径（后端入队，所有者线程落回业务状态）。
		MenuPopup.OnClosed([this](UI::FUIBuilder&) { bContextMenuOpen = false; });
	}
	MenuPopup.SetAnchor(UI::FUIRect{ ContextMenuAnchor.X, ContextMenuAnchor.Y, 0.f, 0.f });
	MenuPopup.SetOpen(bContextMenuOpen);
	// 宽度 = 日志面板宽度的三分之一（弹层窗口宽度 ≈ 本值 + 自身内边距）。面板矩形是上一帧的
	// （与全树同序读回），首帧还没有矩形时留默认宽度。
	const float LinesWidth = LinesPanel.GetRect().W;
	if (LinesWidth > 0.f) { MenuPopup.SetMeasureWidth(LinesWidth / 3.f); }
	MenuPopup.ResetChildren();
	if (bContextMenuOpen)
	{
		UI::FUISelectable& ClearItem = MenuPopup.AddItem<UI::FUISelectable>(UI::FUIName(kIdMenuClear));
		ClearItem.SetLabel("Clear");
		ClearItem.SetSpanAll(true);
		ClearItem.OnSelected([this](UI::FUIBuilder&)
		{
			std::lock_guard<std::mutex> Lock(LinesMutex);
			Lines.clear();
			SelAnchor = -1;
			SelEnd = -1;
			bContextMenuOpen = false;
		});
	}

	// 焦点请求：一次性，翻译后自动清除（`FUIBuilder::RequestKeyboardFocus`）。
	if (CvarPendingFocus)
	{
		CvarPendingFocus = false;
		CvarBox.RequestKeyboardFocus();
	}

	CvarAuthoritative = false;

	// -- Sticky-bottom + 头丢弃补偿 -----------------------------------------
	//   (1) thumb pinned to the bottom -> follow the newest line (stays at bottom).
	//   (2) thumb scrolled up -> hold the view where the user put it; each line popped
	//       off the head (MaxLines cap) is compensated so the reading position stays put.
	bool bScrolledToBottom = false;
	if (NearBottom && !Snapshot.empty())
	{
		LinesPanel.RequestScrollToBottom();
		bScrolledToBottom = true;
	}
	if (DroppedThisFrame > 0 && !bScrolledToBottom)
	{
		LinesPanel.SetScrollY(LinesPanel.GetScrollY() - static_cast<float>(DroppedThisFrame) * LineH);
	}
}

void FEditorConsole::Shutdown(FExampleEditor& Editor)
{
	(void)Editor;

	if (ListenerId != 0)
	{
		if (FLog* Log = GetLog())
		{
			Log->OnLog.Unbind(ListenerId);
		}
		ListenerId = 0;
	}

	if (View == nullptr)
	{
		return;
	}
	if (UI::FUIViewRegistry* Registry = UI::GetUIViewRegistry())
	{
		Registry->UnregisterView(*View);
	}
	View.reset();
}

} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_EDITORCONSOLE_API Maho::FLayerBase* CreateLayer()
{
	return Maho::FEditorConsole::CreateLayer();
}
