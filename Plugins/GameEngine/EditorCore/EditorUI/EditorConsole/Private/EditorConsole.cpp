#include "EditorConsole.h"

#include <cctype>
#include <cstdint>
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

	if (!ImGui::Begin("Console", nullptr, ImGuiWindowFlags_NoCollapse))
	{
		ImGui::End();
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
		SelectedLogLine = -1;
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

	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.055f, 0.055f, 0.06f, 1.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 3.0f);
	ImGui::BeginChild("##ConsoleLines", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
	{

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

		// Auto-scroll to the newest line ONLY if the user is already pinned to the bottom,
		// so a manual scroll-up to read a long back-log is not fought every frame.
		const bool NearBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;

		// UE/Unity per-row model: every log line is its OWN selectable, so per-level
		// color (PushStyleColor on ImGuiCol_Text) coexists with click-to-select + Ctrl+C.
		// A single InputTextMultiline (the "rich text box") cannot do per-line color.
			// The per-line ID must be unique even when two lines render identical text
			// (e.g. repeated "Info: ExampleEditor: EditorCompose (pass3)" rows). That is
			// exactly why the write-up above warns about `PushID()/PopID()` in loops:
			// same text -> same ImGui ID -> "conflicting ID" programmer error. Scope each
			// Selectable under PushID(i)/PopID() so the index disambiguates duplicates.
			for (std::size_t i = 0; i < Snapshot.size(); ++i)
			{
				const FLogEntry& E = Snapshot[i];
				const std::string Text = BuildLine(E);
				ImGui::PushID(static_cast<int>(i));
				ImGui::PushStyleColor(ImGuiCol_Text, LevelColor(E.Level));
				if (ImGui::Selectable(Text.c_str(), static_cast<int>(i) == SelectedLogLine))
				{
					SelectedLogLine = static_cast<int>(i);
				}
				ImGui::PopStyleColor();
				ImGui::PopID();
			}
		ImGui::PopStyleVar();

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

	// UE/Unity-style copy: click a line to select it, Ctrl+C copies that line.
	if (SelectedLogLine >= 0 && static_cast<std::size_t>(SelectedLogLine) < Snapshot.size())
	{
		// The editor context may surface Ctrl either as io.KeyCtrl, the ImGuiMod_Ctrl
		// key, or the LeftCtrl/RightCtrl named keys -- accept any of them.
		const bool CtrlDown = ImGui::GetIO().KeyCtrl
			|| ImGui::IsKeyDown(ImGuiMod_Ctrl)
			|| ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
		if (ImGui::IsKeyPressed(ImGuiKey_C, false) && CtrlDown)
		{
			const std::string Sel = BuildLine(Snapshot[static_cast<std::size_t>(SelectedLogLine)]);
			ImGui::SetClipboardText(Sel.c_str());
		}
	}

	ImGui::End();
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
