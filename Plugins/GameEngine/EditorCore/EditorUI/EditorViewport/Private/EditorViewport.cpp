#include "EditorViewport.h"

#include <Name.h>
#include <variant>

#include "imgui.h"

namespace Maho
{

void FEditorViewport::Draw(FExampleEditor& Editor)
{
	// Dock into the host's main docking space on first use (ImGui remembers the layout
	// afterwards). NoMove locks it so it fills the node and can never be torn off, but
	// other panels may still dock alongside and split the shared space.
	const std::uint32_t DockId = Editor.GetEditorDockSpaceId();
	if (DockId != 0)
	{
		ImGui::SetNextWindowDockID(static_cast<ImGuiID>(DockId), ImGuiCond_FirstUseEver);
	}
	if (ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove))
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
