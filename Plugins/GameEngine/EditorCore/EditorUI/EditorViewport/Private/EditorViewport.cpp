#include "EditorViewport.h"

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
		// The viewport shows the CURRENT on-screen surface: the game composite
		// (UIRenderTarget) in an editor build. The host resolves the stable
		// PresentTargetTextureId() to R.GetPresentTarget() at translate time; the
		// target is y-down so flip V (0,1)->(1,0). No scene mirror / SceneColor is
		// involved -- the viewport samples whatever would be presented this frame.
		if (Sz.x > 0.0f && Sz.y > 0.0f)
		{
			ImGui::Image(
				static_cast<ImTextureID>(FExampleEditor::PresentTargetTextureId()),
				Sz,
				ImVec2(0.f, 0.f), ImVec2(1.f, 1.f));
		}
	}
	ImGui::End();
}

} // namespace Maho

extern "C" MAHO_EDITORVIEWPORT_API Maho::FLayerBase* CreateLayer()
{
	return Maho::FEditorViewport::CreateLayer();
}
