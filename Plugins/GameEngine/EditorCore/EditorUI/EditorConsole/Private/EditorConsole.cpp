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
constexpr const char* kIdBtnCopy  = "EditorConsole.Button.Copy";
constexpr const char* kIdBtnCopyAll = "EditorConsole.Button.CopyAll";
constexpr const char* kIdBtnClear = "EditorConsole.Button.Clear";
constexpr const char* kIdLines    = "EditorConsole.Lines";
constexpr const char* kIdSuggest  = "EditorConsole.Suggest";
constexpr const char* kIdCmdRow   = "EditorConsole.Command";
constexpr const char* kIdCvar     = "EditorConsole.Cvar";
constexpr const char* kIdBtnRun   = "EditorConsole.Button.Run";

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
	if (const auto* FilterNode = dynamic_cast<const UI::FUIInputText*>(PanelView->Find(UI::FUIName(kIdFilter))))
	{
		std::strncpy(FilterBuffer, FilterNode->GetValue().c_str(), sizeof(FilterBuffer) - 1);
		FilterBuffer[sizeof(FilterBuffer) - 1] = '\0';
	}

	bool bCvarEditing = false;
	if (const auto* CvarNode = dynamic_cast<const UI::FUIInputText*>(PanelView->Find(UI::FUIName(kIdCvar))))
	{
		bCvarEditing = CvarNode->GetState().bPressed;   // 旧 IsItemActive()：后端写回的活跃位
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

	// -- 复制（工具栏按钮 / Ctrl+C）-------------------------------------------
	// 工具栏 "Copy" 复制选中区间、"Copy All" 复制全部可见行；日志面板自身声明了
	// Ctrl+C / Ctrl+A 两条快捷键（见下面的 `OnShortcut`），命中后落到同一对标志上。
	if (CopyRequested || CopyAllRequested)
	{
		std::string Text;
		if (CopyAllRequested)
		{
			for (const FLogEntry& E : Snapshot)
			{
				Text += BuildLine(E);
				Text += '\n';
			}
		}
		else if (SelAnchor >= 0 && SelEnd >= 0)
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
		CopyAllRequested = false;
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
	const bool HasTyping = (CvarBuffer[0] != '\0');
	std::vector<std::string> Matches;
	if (HasTyping)
	{
		std::string Needle;
		for (const char* p = CvarBuffer; *p; ++p)
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
	// exact match the user just picked).
	if (Matches.size() == 1 && Matches.front() == CvarBuffer)
	{
		Matches.clear();
	}

	// 列表开合：旧版还看"鼠标是否悬在列表上"，新树的事件不携带指针位置，故只按
	// "输入框活跃 + 有候选" 判定（点选后输入框失活 -> 下一帧自动收）。
	if (!CvarDropdownOpen)
	{
		CvarDropdownOpen = bCvarEditing && HasTyping;
	}
	else if (!HasTyping || !bCvarEditing)
	{
		CvarDropdownOpen = false;
	}
	if (Matches.empty())
	{
		CvarDropdownOpen = false;
	}

	// 候选行上限（旧版：一屏 8 行，超出靠列表滚动）。
	constexpr int MaxRows = 8;
	const int ShowRows = static_cast<int>(Matches.size()) < MaxRows
		? static_cast<int>(Matches.size()) : MaxRows;

	// ---- 声明期：只改本视图的树 -------------------------------------------
	UI::FUIEditScope Scope = PanelView->Edit();
	UI::FUIBuilder& Root = Scope.GetRoot();
	Root.Layout().SetDirection(UI::EUIDirection::Column);
	Root.Layout().SetSpacing(4.f);

	bool bNew = false;

	// 工具栏行：过滤框 + Clear。旧版把这一行画在窗口自身的背景上（不是 MenuBar），
	// 现在由 `FUIPanel` 外壳的 WindowBg 承担，行内只放控件。
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

		UI::FUIButton& CopyButton = Ensure<UI::FUIButton>(Toolbar, UI::FUIName(kIdBtnCopy), bNew);
		CopyButton.SetLabel("Copy");
		if (bNew)
		{
			CopyButton.OnClick([this](UI::FUIBuilder&) { CopyRequested = true; });
		}

		UI::FUIButton& CopyAllButton = Ensure<UI::FUIButton>(Toolbar, UI::FUIName(kIdBtnCopyAll), bNew);
		CopyAllButton.SetLabel("Copy All");
		if (bNew)
		{
			CopyAllButton.OnClick([this](UI::FUIBuilder&) { CopyAllRequested = true; });
		}

		UI::FUIButton& ClearButton = Ensure<UI::FUIButton>(Toolbar, UI::FUIName(kIdBtnClear), bNew);
		ClearButton.SetLabel("Clear");
		if (bNew)
		{
			ClearButton.OnClick([this](UI::FUIBuilder&)
			{
				std::lock_guard<std::mutex> Lock(LinesMutex);
				Lines.clear();
				SelAnchor = -1;
				SelEnd = -1;
			});
		}
	}

	// 日志主体：旧版是"底部预留一行命令框高度"的带边框子窗口。
	// 高度 Fill = 同级的其余项都是内容高，主体自然拿到"剩余空间"。
	UI::FUIPanel& LinesPanel = Ensure<UI::FUIPanel>(Root, UI::FUIName(kIdLines), bNew);
	LinesPanel.SetChrome(UI::EUIPanelChrome::Full);
	LinesPanel.SetScrollable(true);
	LinesPanel.Layout().SetSize(UI::FUILength::Fill(), UI::FUILength::Fill());
	LinesPanel.Layout().SetSpacing(0.f);   // 旧 PushStyleVar(ItemSpacing, 0)

	// 面板级快捷键（声明式，见 `UI::FUIKeyChord`）：Ctrl+C 复制选中区间、Ctrl+A 全选可见行。
	// 挂在日志面板上 = "面板在翻译（可见）时才响应"；后端的守卫还要求键盘焦点在本视图窗口
	// 且当前没有文本输入在收键盘，故在过滤框/命令行里打字不会误触发。
	if (bNew)
	{
		LinesPanel.OnShortcut({ 'C', UI::EUIModifiers::Ctrl },
			[this](UI::FUIBuilder&, std::string_view) { CopyRequested = true; });
		LinesPanel.OnShortcut({ 'A', UI::EUIModifiers::Ctrl },
			[this](UI::FUIBuilder&, std::string_view) { SelectAllRequested = true; });
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

	// 命令行行：CVar 输入框 + Run。旧版靠输入框回车提交执行；新输入控件也带回车提交
	// （`OnSubmitted`，见下），Run 按钮保留为同一段执行逻辑的显式入口。
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
		// 回车提交：后端只在真按了回车时报告（文本改动另走 TextChanged），与 Run 同路。
		CvarBox.OnSubmitted([this](UI::FUIBuilder&, std::string_view) { CvarRunRequested = true; });
	}
	CvarBox.Layout().SetSize(UI::FUILength::Fill(), UI::FUILength::Content());

	UI::FUIButton& RunButton = Ensure<UI::FUIButton>(CmdRow, UI::FUIName(kIdBtnRun), bNew);
	RunButton.SetLabel("Run");
	if (bNew)
	{
		RunButton.OnClick([this](UI::FUIBuilder&) { CvarRunRequested = true; });
	}

	// 候选弹层：旧版是浮在输入框上方的独立窗口。`FUIPopup` 在正常流里零尺寸，后端为它
	// 开第二个窗口，故不再占版面；锚点取输入框矩形（上一帧的，与全树同序读回）。
	UI::FUIPopup& SuggestPopup = Ensure<UI::FUIPopup>(Root, UI::FUIName(kIdSuggest), bNew);
	const bool bShowSuggest = CvarDropdownOpen && ShowRows > 0;
	SuggestPopup.SetAnchor(CvarBox.GetRect());
	SuggestPopup.SetOpen(bShowSuggest);
	if (bNew)
	{
		// 用户点外部 / Esc 关掉：后端把这条事件送到所有者线程，这里落回业务状态。
		SuggestPopup.OnClosed([this](UI::FUIBuilder&) { CvarDropdownOpen = false; });
	}
	SuggestPopup.ResetChildren();
	if (bShowSuggest)
	{
		for (int i = 0; i < ShowRows; ++i)
		{
			UI::FUISelectable& Row = SuggestPopup.AddItem<UI::FUISelectable>(MakeSuggestId(i));
			Row.SetLabel(Matches[static_cast<std::size_t>(i)]);
			Row.SetSpanAll(true);
			Row.OnSelected([this, Name = Matches[static_cast<std::size_t>(i)]](UI::FUIBuilder&)
			{
				std::strncpy(CvarBuffer, Name.c_str(), sizeof(CvarBuffer) - 1);
				CvarBuffer[sizeof(CvarBuffer) - 1] = '\0';
				CvarDropdownOpen = false;
				CvarPendingFocus = true;    // 下一帧翻译把键盘焦点交回输入框
				CvarAuthoritative = true;   // 本帧缓冲是权威值：把名字写回节点
			});
		}
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
