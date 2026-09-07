#include "EditorOutliner.h"

#include <GameWorld.h>

#include <cstdio>      // std::snprintf

#include "imgui.h"

namespace Maho
{

void FEditorOutliner::Draw(FExampleEditor& Editor)
{
	if (ImGui::Begin("Outliner", nullptr, ImGuiWindowFlags_NoCollapse))
	{
		GameWorld::FGameWorld* W = GameWorld::GetGameWorld();
		if (W == nullptr)
		{
			ImGui::Text("No game world");
			ImGui::End();
			return;
		}

		GameWorld::FEntityRegistry& Reg = W->GetRegistry();
		const std::uint32_t Capacity = Reg.GetCapacity();
		std::uint32_t& SelectedId = Editor.GetEditorContext().SelectedEntityId;

		ImGui::Text("Entities : %u", Capacity);
		ImGui::Separator();

		for (std::uint32_t I = 0; I < Capacity; ++I)
		{
			const GameWorld::FEntity E{ I, 0u };
			if (!W->IsAlive(E))
			{
				continue;
			}

			char Label[32];
			std::snprintf(Label, sizeof(Label), "Entity %u", I);
			if (ImGui::Selectable(Label, SelectedId == I))
			{
				SelectedId = I;
			}
		}
	}
	ImGui::End();
}

} // namespace Maho

extern "C" MAHO_EDITOROUTLINER_API Maho::FLayerBase* CreateLayer()
{
	return Maho::FEditorOutliner::CreateLayer();
}
