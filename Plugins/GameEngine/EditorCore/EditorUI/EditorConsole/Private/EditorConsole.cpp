#include "EditorConsole.h"

#include <algorithm>
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

	// -- Snapshot the buffer under the lock, render outside it ----------------
	std::vector<FLogEntry> Snapshot;
	{
		std::lock_guard<std::mutex> Lock(LinesMutex);
		Snapshot.assign(Lines.begin(), Lines.end());
	}

	// -- Build the selectable body (level-colored text labels are joined into one
	//    plain-text buffer; InputTextMultiline gives drag-select + Ctrl+C, which a
	//    per-line TextColored renderer cannot). Append one trailing NUL so a fully
	//    empty body still points at a valid, writable char buffer.
	LogBody.clear();
	for (const FLogEntry& E : Snapshot)
	{
		if (!E.Category.empty())
		{
			LogBody += E.Category;
			LogBody += ": ";
		}
		LogBody += LevelName(E.Level);
		LogBody += ": ";
		LogBody += E.Message;
		LogBody += '\n';
	}
	LogBody.push_back('\0');   // InputTextMultiline reads up to this NUL

	// -- Message list: the live log as a READ-ONLY, SELECTABLE multiline ---------
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.055f, 0.055f, 0.06f, 1.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 3.0f);
	ImGui::BeginChild("##ConsoleLines", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
	{
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
		ImGui::InputTextMultiline(
			"##LogBody",
			LogBody.data(),
			std::max<std::size_t>(1, LogBody.size()),
			ImVec2(-1.0f, -1.0f),
			ImGuiInputTextFlags_ReadOnly);
		ImGui::PopStyleVar();

		// Right-click the log area => context menu with Clear.
		if (ImGui::BeginPopupContextWindow("##LogContext"))
		{
			if (ImGui::MenuItem("Clear"))
			{
				std::lock_guard<std::mutex> Lock(LinesMutex);
				Lines.clear();
			}
			ImGui::EndPopup();
		}
	}
	ImGui::EndChild();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();

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
