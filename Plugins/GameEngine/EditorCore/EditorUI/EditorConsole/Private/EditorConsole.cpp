#include "EditorConsole.h"

#include "ConsoleVariable.h"
#include <cctype>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
#include "imgui.h"

namespace Maho
{

namespace
{

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
// critical=magenta -- so the ImGui panel reads the same way as the 黑框 stdout sink.
// Values are softened toward the dark panel background for readability.
ImVec4 LevelColor(ELogLevel Level)
{
	switch (Level)
	{
	case ELogLevel::Trace:    return ImVec4(0.85f, 0.85f, 0.85f, 1.0f); // white
	case ELogLevel::Debug:    return ImVec4(0.35f, 0.85f, 0.90f, 1.0f); // cyan
	case ELogLevel::Info:     return ImVec4(0.30f, 0.85f, 0.40f, 1.0f); // green
	case ELogLevel::Warn:     return ImVec4(0.95f, 0.85f, 0.25f, 1.0f); // yellow
	case ELogLevel::Error:    return ImVec4(0.90f, 0.35f, 0.35f, 1.0f); // red
	case ELogLevel::Critical: return ImVec4(0.90f, 0.30f, 0.90f, 1.0f); // magenta
	}
	return ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
}

} // namespace

void FEditorConsole::Init(FExampleEditor&)
{
	if (ListenerId != 0)
	{
		return;
	}
	// Producer thread pushes into a mutex-guarded deque; the panel drains it on
	// the frame thread in Draw, so the block is bounded by MaxLines (drop head).
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

void FEditorConsole::Draw(FExampleEditor& Editor)
{
	const std::uint32_t DockId = Editor.GetEditorDockSpaceId();
	if (DockId != 0)
	{
		ImGui::SetNextWindowDockID(static_cast<ImGuiID>(DockId), ImGuiCond_FirstUseEver);
	}

	// Match this panel's chrome to the dark dock gutter from the separator fix (TabWell 14,14,16),
	// so the toolbar row (WindowBg, drawn by Begin) and the filter/CVar input frames (FrameBg)
	// read as the same dark chrome as the Viewport|Console boundary instead of lighter gray strips.
	const ImVec4 GutterChrome = ImVec4(14.0f / 255.0f, 14.0f / 255.0f, 16.0f / 255.0f, 1.0f);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, GutterChrome);
	ImGui::PushStyleColor(ImGuiCol_FrameBg, GutterChrome);

	if (!ImGui::Begin("Console", nullptr, ImGuiWindowFlags_NoCollapse))
	{
		ImGui::End();
		ImGui::PopStyleColor(2);
		return;
	}

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

	// Toolbar row: live string-match filter box + one-click Copy All / Clear. Drawn on
	// the window's own (gray) background so it reads as part of the frame chrome rather
	// than a separate dark strip (the ImGui MenuBar uses ImGuiCol_MenuBarBg, a near-black
	// band) that visually shoves the log region down. That is the "菜单栏嵌入灰色边框而
	// 不是把log框挤下去" fix.
	bool DoCopyAll = false;
	ImGui::SetNextItemWidth(220.0f);
	ImGui::InputTextWithHint("##FilterLogs", "Filter logs...", FilterBuffer, IM_ARRAYSIZE(FilterBuffer));

	ImGui::SameLine();
	if (ImGui::Button("Copy All"))
	{
		DoCopyAll = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Clear"))
	{
			std::lock_guard<std::mutex> Lock(LinesMutex);
			Lines.clear();
			SelAnchor = -1;
			SelEnd = -1;
	}

	// Case-insensitive filter needle, computed once per frame from the box text.
	std::string NeedleLower;
	for (const char* p = FilterBuffer; *p; ++p)
	{
		NeedleLower += static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
	}
	const bool HasFilter = !NeedleLower.empty();

	// -- Snapshot only the visible (filtered) lines under the lock ----------------
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

	ImGui::PushStyleColor(ImGuiCol_ChildBg, GutterChrome);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 3.0f);
	// Reserve a bottom row for the CVar command box (drawn after EndChild), so the
	// log fills the rest of the panel rather than covering the command input.
	ImGui::BeginChild("##ConsoleLines", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_Borders);
	{

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

		// Auto-scroll to the newest line ONLY if the user is already pinned to the bottom,
		// so a manual scroll-up to read a long back-log is not fought every frame.
		const bool NearBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;

		// UE/Unity per-row model: every log line is its OWN selectable, so per-level
		// color (PushStyleColor on ImGuiCol_Text) coexists with click-to-select + Ctrl+C.
		// A single InputTextMultiline (the "rich text box") cannot do per-line color.
		// Multi-line select: left-click a line seeds the anchor; drag (or Shift+click)
		// extends the range; every line in [min,max] highlights; Ctrl+C copies the range.
		const int n = static_cast<int>(Snapshot.size());
		const int SelLo = (SelAnchor >= 0 && SelEnd >= 0) ? (SelAnchor < SelEnd ? SelAnchor : SelEnd) : -1;
		const int SelHi = (SelAnchor >= 0 && SelEnd >= 0) ? (SelAnchor > SelEnd ? SelAnchor : SelEnd) : -1;
		std::vector<float> LineBottom(n);
		bool AnyLineClicked = false;

		// The per-line ID must be unique even when two lines render identical text
		// (e.g. repeated "Info: ExampleEditor: EditorCompose (pass3)" rows). That is
		// exactly why the write-up above warns about `PushID()/PopID()` in loops:
		// same text -> same ImGui ID -> "conflicting ID" programmer error. Scope each
		// Selectable under PushID(i)/PopID() so the index disambiguates duplicates.
		for (int i = 0; i < n; ++i)
		{
			const FLogEntry& E = Snapshot[i];
			const std::string Text = BuildLine(E);
			ImGui::PushID(i);
			ImGui::PushStyleColor(ImGuiCol_Text, LevelColor(E.Level));
			const bool IsSelected = (i >= SelLo && i <= SelHi);
			ImGui::Selectable(Text.c_str(), IsSelected);
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			{
				AnyLineClicked = true;
				if (ImGui::GetIO().KeyShift)
				{
					if (SelAnchor < 0) { SelAnchor = i; }
					SelEnd = i;
				}
				else
				{
					SelAnchor = i;
					SelEnd = i;
				}
			}
			ImGui::PopStyleColor();
			LineBottom[i] = ImGui::GetItemRectMax().y;
			ImGui::PopID();
		}
		ImGui::PopStyleVar();

		// Drag left across rows to extend the selection to the line under the cursor.
		if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && SelAnchor >= 0 && n > 0)
		{
			const float MouseY = ImGui::GetMousePos().y;
			int Hover = -1;
			for (int i = 0; i < n; ++i)
			{
				if (MouseY <= LineBottom[i]) { Hover = i; break; }
			}
			if (Hover < 0) { Hover = n - 1; }
			SelEnd = Hover;
		}

		// Clicking empty area (not on a line) clears the selection.
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !AnyLineClicked)
		{
			SelAnchor = -1;
			SelEnd = -1;
		}

		// Sticky-bottom policy:
		//   (1) thumb pinned to the bottom -> follow the newest line (stays at bottom).
		//   (2) thumb scrolled up -> hold the view where the user put it.
		bool ScrolledToBottom = false;
		if (NearBottom && !Snapshot.empty())
		{
			ImGui::SetScrollHereY(1.0f);
			ScrolledToBottom = true;
		}

		// Head-trim compensation. When the user is NOT auto-scrolling (scrolled up), each
		// line popped off the head (MaxLines cap) would otherwise nudge the whole visible
		// output up one row every frame. Shift the view back down by the trimmed height so
		// the reading position stays put.
		if (DroppedThisFrame > 0 && !ScrolledToBottom)
		{
			const float LineH = ImGui::GetTextLineHeight();
			ImGui::SetScrollY(ImGui::GetScrollY() - static_cast<float>(DroppedThisFrame) * LineH);
		}

		// Copy All (toolbar button) -- joins every currently-visible line.
		if (DoCopyAll)
		{
			std::string All;
			for (const FLogEntry& E : Snapshot)
			{
				All += BuildLine(E);
				All += '\n';
			}
			ImGui::SetClipboardText(All.c_str());   // editor backend routes to the OS clipboard
		}
	}
	ImGui::EndChild();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();

	// UE/Unity-style copy: Ctrl+C copies the selected line range.
	if (SelAnchor >= 0 && SelEnd >= 0)
	{
		const int lo = SelAnchor < SelEnd ? SelAnchor : SelEnd;
		const int hi = SelAnchor > SelEnd ? SelAnchor : SelEnd;
		if (lo >= 0 && hi < static_cast<int>(Snapshot.size()))
		{
			// The editor context may surface Ctrl either as io.KeyCtrl, the ImGuiMod_Ctrl
			// key, or the LeftCtrl/RightCtrl named keys -- accept any of them.
			const bool CtrlDown = ImGui::GetIO().KeyCtrl
				|| ImGui::IsKeyDown(ImGuiMod_Ctrl)
				|| ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
			if (ImGui::IsKeyPressed(ImGuiKey_C, false) && CtrlDown)
			{
				std::string Sel;
				for (int i = lo; i <= hi; ++i)
				{
					Sel += BuildLine(Snapshot[i]);
					Sel += '\n';
				}
				ImGui::SetClipboardText(Sel.c_str());
			}
		}
	}

	// CVar command line below the log area (same InputTextWithHint pattern as the filter
	// box, but at the bottom). Enter executes "name [value]": a bare name prints the
	// current value, "name value" sets it; the result is echoed back into the log list.
	auto EchoLog = [this](ELogLevel Level, std::string Text)
	{
		std::lock_guard<std::mutex> Lock(LinesMutex);
		if (Lines.size() >= MaxLines)
		{
			Lines.pop_front();
			++DroppedCount;
		}
		Lines.push_back({ Level, std::string("CVar"), std::move(Text) });
	};

	// Draw the CVar command input; capture where it landed and whether it has focus
	// (the string-match autocomplete list below its position needs both).
	if (CvarPendingFocus)
	{
		ImGui::SetKeyboardFocusHere();
		CvarPendingFocus = false;
	}
	ImGui::SetNextItemWidth(-1.0f);
	const bool Submitted = ImGui::InputTextWithHint(
		"##CvarCmd", "cvar, e.g. r.MaxFPS 120", CvarBuffer, IM_ARRAYSIZE(CvarBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
	const bool Editing = ImGui::IsItemActive();
	const ImVec2 InputMin = ImGui::GetItemRectMin();
	const ImVec2 InputSize = ImGui::GetItemRectSize();

	// Enter executes "name [value]": a bare name prints the current value, "name value"
	// sets it; the result is echoed back into the log list.
	if (Submitted)
	{
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

		if (!Line.empty())
		{
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
	}

	// While the user is typing, gather every registered cvar whose name string-matches
	// the box text (case-insensitive substring), for the autocomplete dropdown below.
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

	// Dropdown geometry (rows capped so a long list scrolls).
	constexpr int MaxRows = 8;
	const float RowH = ImGui::GetFrameHeightWithSpacing();
	const int ShowRows = static_cast<int>(Matches.size()) < MaxRows
		? static_cast<int>(Matches.size()) : MaxRows;
	const float PopupW = InputSize.x;
	const float PopupH = RowH * static_cast<float>(ShowRows);

	// Dropdown visibility. We can't gate purely on IsItemActive(): the moment the user
	// clicks a suggestion the input deactivates (ImGui releases focus on an outside click),
	// so IsItemActive flips false on exactly the frame the click must land. Persist the open
	// state and only close when the box clears, an exact name is typed, no name matches, or
	// the cursor leaves the dropdown while the input isn't focused.
	// (ImRect lives in imgui_internal.h, which we avoid pulling in; a plain AABB test over
	// the four edges only needs the public ImVec2 math.)
	const float DropLeft = InputMin.x;
	const float DropTop = InputMin.y - PopupH;
	const float DropRight = InputMin.x + PopupW;
	const float DropBottom = InputMin.y;
	const ImVec2 MousePos = ImGui::GetIO().MousePos;
	const bool MouseOverDropdown = MousePos.x >= DropLeft && MousePos.x <= DropRight
		&& MousePos.y >= DropTop && MousePos.y <= DropBottom;
	if (!CvarDropdownOpen)
	{
		CvarDropdownOpen = Editing && HasTyping;
	}
	else if (!HasTyping || (!Editing && !MouseOverDropdown))
	{
		CvarDropdownOpen = false;
	}
	if (Matches.empty())
	{
		CvarDropdownOpen = false;
	}
	// Once the box holds a complete cvar name, drop the dropdown (don't re-show the single
	// exact match the user just picked).
	if (Matches.size() == 1 && Matches.front() == CvarBuffer)
	{
		Matches.clear();
		CvarDropdownOpen = false;
	}

	ImGui::End();
	ImGui::PopStyleColor(2);

	// Autocomplete dropdown floating just ABOVE the input box. A vertical-scroll listbox
	// caps the visible rows; clicking a row fills the box with that cvar name. Rendered
	// after the console window closes so it sits on top of the panel (and its log area).
	if (CvarDropdownOpen && !Matches.empty())
	{
		ImGui::SetNextWindowPos(ImVec2(DropLeft, DropTop), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(PopupW, PopupH), ImGuiCond_Always);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.10f, 0.11f, 0.98f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		if (ImGui::Begin("##CvarSuggest", nullptr,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
			| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings
			| ImGuiWindowFlags_NoFocusOnAppearing))
		{
			if (ImGui::BeginListBox("##CvarSuggestList", ImVec2(PopupW, PopupH)))
			{
				for (const std::string& Name : Matches)
				{
					const bool IsSelected = (Name == CvarBuffer);
					if (ImGui::Selectable(Name.c_str(), IsSelected))
					{
						// The click itself deactivates the input (ImGui releases focus on an
						// outside click), so just fill the buffer and refocus it next frame to
						// keep typing -- no need to touch the private active-id APIs.
						std::strncpy(CvarBuffer, Name.c_str(), sizeof(CvarBuffer) - 1);
						CvarBuffer[sizeof(CvarBuffer) - 1] = '\0';
						CvarPendingFocus = true;
						CvarDropdownOpen = false;
					}
				}
				ImGui::EndListBox();
			}
		}
		ImGui::End();
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();
	}
}

void FEditorConsole::Shutdown(FExampleEditor&)
{
	if (ListenerId != 0)
	{
		if (FLog* Log = GetLog())
		{
			Log->OnLog.Unbind(ListenerId);
		}
		ListenerId = 0;
	}
}

} // namespace Maho

extern "C" MAHO_EDITORCONSOLE_API Maho::FLayerBase* CreateLayer()
{
	return Maho::FEditorConsole::CreateLayer();
}
