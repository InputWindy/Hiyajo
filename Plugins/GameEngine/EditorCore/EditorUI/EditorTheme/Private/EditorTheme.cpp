#include "EditorTheme.h"

#include <ExampleEditorTheme.h>

#include <imgui.h>

#include <cstdio>
#include <cstring>

namespace Maho
{

void FEditorTheme::ReloadDefaults()
{
	const int CCount = GetEditorThemeColorCount();
	const int SCount = GetEditorThemeStyleCount();
	Colors.assign(static_cast<std::size_t>(CCount) * 4, 0.0f);
	Styles.assign(static_cast<std::size_t>(SCount) * 2, 0.0f);
	for (int i = 0; i < CCount; ++i)
	{
		GetEditorThemeColorDefault(i, &Colors[static_cast<std::size_t>(i) * 4]);
	}
	for (int i = 0; i < SCount; ++i)
	{
		GetEditorThemeStyleDefault(i, &Styles[static_cast<std::size_t>(i) * 2]);
	}
}

void FEditorTheme::Init(FExampleEditor& Editor)
{
	(void)Editor;

	std::strncpy(PathBuffer, GetEditorThemeDefaultPath(), sizeof(PathBuffer) - 1);
	PathBuffer[sizeof(PathBuffer) - 1] = '\0';

	ReloadDefaults();
	// Start from a persisted theme when one exists, otherwise stay on defaults.
	if (LoadEditorTheme(PathBuffer, Colors.data(), Styles.data()))
	{
		ApplyEditorTheme(Colors.data());
		ApplyEditorThemeStyles(Styles.data());
	}
}

bool FEditorTheme::DrawStyleSection()
{
	bool Dirty = false;
	const int SCount = GetEditorThemeStyleCount();
	for (int i = 0; i < SCount;)
	{
		const char* Group = GetEditorThemeStyleGroup(i);
		if (ImGui::CollapsingHeader(Group, ImGuiTreeNodeFlags_DefaultOpen))
		{
			while (i < SCount && std::strcmp(GetEditorThemeStyleGroup(i), Group) == 0)
			{
				float* V = &Styles[static_cast<std::size_t>(i) * 2];
				const char* Name = GetEditorThemeStyleName(i);
				const int Arity = GetEditorThemeStyleArity(i);
				ImGui::PushID(Name);
				if (Arity == 2)
				{
					if (ImGui::DragFloat2(Name, V, 0.25f))
					{
						Dirty = true;
					}
				}
				else
				{
					if (ImGui::DragFloat(Name, V, 0.25f))
					{
						Dirty = true;
					}
				}
				ImGui::PopID();
				++i;
			}
		}
		else
		{
			while (i < SCount && std::strcmp(GetEditorThemeStyleGroup(i), Group) == 0)
			{
				++i;
			}
		}
	}
	return Dirty;
}

void FEditorTheme::Draw(FExampleEditor& Editor)
{
	ImGui::SetNextWindowDockID(static_cast<ImGuiID>(Editor.GetEditorDockSpaceId()), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("EditorTheme", nullptr, ImGuiWindowFlags_NoCollapse))
	{
		ImGui::End();
		return;
	}

	bool Dirty = false;
	const int CCount = GetEditorThemeColorCount();
	const int SCount = GetEditorThemeStyleCount();

	ImGui::Checkbox("Live apply", &LiveApply);
	ImGui::SameLine();
	ImGui::TextColored(ImVec4(0.62f, 0.64f, 0.70f, 1.0f), "%d colors / %d styles", CCount, SCount);
	ImGui::Separator();

	ImGui::BeginChild("theme", ImVec2(0.0f, -96.0f), true);

	// Colors.
	for (int i = 0; i < CCount;)
	{
		const char* Group = GetEditorThemeColorGroup(i);
		if (ImGui::CollapsingHeader(Group, ImGuiTreeNodeFlags_DefaultOpen))
		{
			while (i < CCount && std::strcmp(GetEditorThemeColorGroup(i), Group) == 0)
			{
				float* C = &Colors[static_cast<std::size_t>(i) * 4];
				const char* Name = GetEditorThemeColorName(i);
				if (ImGui::ColorEdit4(Name, C, ImGuiColorEditFlags_AlphaBar))
				{
					Dirty = true;
				}
				++i;
			}
		}
		else
		{
			while (i < CCount && std::strcmp(GetEditorThemeColorGroup(i), Group) == 0)
			{
				++i;
			}
		}
	}

	ImGui::Separator();
	ImGui::TextUnformatted("Style (rounding / padding / spacing)");

	// Styles.
	if (DrawStyleSection())
	{
		Dirty = true;
	}

	ImGui::EndChild();

	ImGui::Separator();
	ImGui::InputText("Path", PathBuffer, IM_ARRAYSIZE(PathBuffer));

	ImGui::PushID("EditorThemeOps");
	if (ImGui::Button("Apply"))
	{
		ApplyEditorTheme(Colors.data());
		ApplyEditorThemeStyles(Styles.data());
		std::snprintf(StatusBuffer, sizeof(StatusBuffer), "Applied.");
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset"))
	{
		ReloadDefaults();
		ApplyEditorTheme(Colors.data());
		ApplyEditorThemeStyles(Styles.data());
		std::snprintf(StatusBuffer, sizeof(StatusBuffer), "Reset to defaults.");
	}
	ImGui::SameLine();
	if (ImGui::Button("Save"))
	{
		if (SaveEditorTheme(PathBuffer, Colors.data(), Styles.data()))
		{
			std::snprintf(StatusBuffer, sizeof(StatusBuffer), "Saved: %s", PathBuffer);
		}
		else
		{
			std::snprintf(StatusBuffer, sizeof(StatusBuffer), "Save failed.");
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Load"))
	{
		if (LoadEditorTheme(PathBuffer, Colors.data(), Styles.data()))
		{
			ApplyEditorTheme(Colors.data());
			ApplyEditorThemeStyles(Styles.data());
			std::snprintf(StatusBuffer, sizeof(StatusBuffer), "Loaded: %s", PathBuffer);
		}
		else
		{
			std::snprintf(StatusBuffer, sizeof(StatusBuffer), "Load failed.");
		}
	}
	ImGui::PopID();

	if (StatusBuffer[0] != '\0')
	{
		ImGui::TextUnformatted(StatusBuffer);
	}

	// Live preview: push any in-progress edit straight to the live style.
	if (Dirty && LiveApply)
	{
		ApplyEditorTheme(Colors.data());
		ApplyEditorThemeStyles(Styles.data());
	}

	ImGui::End();
}

} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_EDITORTHEME_API Maho::FLayerBase* CreateLayer()
{
	return Maho::FEditorTheme::CreateLayer();
}
