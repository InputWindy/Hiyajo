#include "EditorViewport.h"

#include <Name.h>
#include <variant>

#include "imgui.h"

namespace Maho
{

void FEditorViewport::Draw(FExampleEditor& Editor)
{
	if (ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoCollapse))
	{
		const ImVec2 Sz = ImGui::GetContentRegionAvail();
		if (!Editor.GetEditorContext().bSceneReady)
		{
			ImGui::Text("No scene in this build");
		}
		else
		{
			// The scene's shared color target is published on FRender under the fixed
			// name "SceneColor"; the editor HOST re-resolves it when translating this
			// draw command (DrawCmd.TextureId != 0 -> FName::FromId -> GetMirror), so the
			// ImTextureID we hand to ImGui::Image must be that very FName's id.
			FRender& R = Editor.GetRender();
			const Name::FName SceneColorName("SceneColor");
			const FRDGResourceRef* Mirror = R.GetMirror(SceneColorName);
			const FRDGTextureRef* Tex = Mirror ? std::get_if<FRDGTextureRef>(Mirror) : nullptr;
			if (Tex != nullptr)
			{
			ImGui::Image(
				static_cast<ImTextureID>(SceneColorName.GetId()),
				Sz,
				ImVec2(0.f, 1.f), ImVec2(1.f, 0.f));   // flip V: scene target is y-down
			}
			else
			{
				ImGui::Text("SceneColor not mirrored yet");
			}
		}
	}
	ImGui::End();
}

} // namespace Maho

extern "C" MAHO_EDITORVIEWPORT_API Maho::FLayerBase* CreateLayer()
{
	return Maho::FEditorViewport::CreateLayer();
}
