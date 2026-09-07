#include "EditorControls.h"

#include "imgui.h"

namespace Maho
{

void FEditorControls::Draw(FExampleEditor& Editor)
{
	static bool bWireframe = false;
	static float Playback = 0.f;

	if (ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_NoCollapse))
	{
		ImGui::Text("Editor controls");
		ImGui::Separator();

		ImGui::Checkbox("Wireframe", &bWireframe);
		ImGui::SliderFloat("Playback speed", &Playback, 0.f, 4.f);

		if (ImGui::Button("Reset selection"))
		{
			Editor.GetEditorContext().SelectedEntityId = 0u;
		}

		ImGui::Separator();
		ImGui::Text("Selected entity: %u", Editor.GetEditorContext().SelectedEntityId);
	}
	ImGui::End();
}

} // namespace Maho

extern "C" MAHO_EDITORCONTROLS_API Maho::FLayerBase* CreateLayer()
{
	return Maho::FEditorControls::CreateLayer();
}
