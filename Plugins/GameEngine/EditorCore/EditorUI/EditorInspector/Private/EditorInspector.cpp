#include "EditorInspector.h"

#include <GameWorld.h>

#include "imgui.h"

namespace Maho
{

void FEditorInspector::Draw(FExampleEditor& Editor)
{
	if (ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoCollapse))
	{
		const std::uint32_t Id = Editor.GetEditorContext().SelectedEntityId;
		GameWorld::FGameWorld* W = GameWorld::GetGameWorld();
		if (W == nullptr)
		{
			ImGui::Text("No game world");
			ImGui::End();
			return;
		}

		const GameWorld::FEntity E{ Id, 0u };
		if (!W->IsAlive(E))
		{
			ImGui::Text("No entity selected");
			ImGui::End();
			return;
		}

		if (GameWorld::FTransform* T = W->GetComponent<GameWorld::FTransform>(E))
		{
			ImGui::Text("Entity %u", Id);
			ImGui::Separator();
			ImGui::SliderFloat("X", &T->X, -10.f, 10.f);
			ImGui::SliderFloat("Y", &T->Y, -10.f, 10.f);
			ImGui::SliderFloat("Z", &T->Z, -10.f, 10.f);
		}
		else
		{
			ImGui::Text("Entity %u has no FTransform", Id);
		}
	}
	ImGui::End();
}

} // namespace Maho

extern "C" MAHO_EDITORINSPECTOR_API Maho::FLayerBase* CreateLayer()
{
	return Maho::FEditorInspector::CreateLayer();
}
