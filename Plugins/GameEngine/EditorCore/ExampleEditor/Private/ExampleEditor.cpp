#include "ExampleEditor.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <functional>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include <Log.h>
#include <FrameRenderFeature.h>
#include <Name.h>
#include <Platform.h>
#include <Scene.h>
#include <UIFeature.h>
#include <RHI/RHICommandList.h>
#include <RHI/RHIEnums.h>
#include <RHI/RHIResources.h>
#include <ShaderParameterStruct.h>

#include "imgui.h"

#include "ImGuiTheme.h"

// ImTextureID for the live present target (game composite UIRenderTarget). The name is
// never a registered mirror (the target is CreateTexture'd directly, no mirror entry), so
// the host's translate step special-cases this id -> R.GetPresentTarget().
constexpr const char* kPresentTargetTexName = "__EditorPresentTarget__";

#if defined(_WIN32)
#	include <windows.h>
#endif

namespace
{
	// Editor ImGui vertex shader (identical to the game UI shader -- same GLSL).
	constexpr const char* kEditorVertShader = R"(
#version 460
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
layout(push_constant) uniform Push { mat4 Proj; } PC;
layout(location = 0) out vec2 FragUV;
layout(location = 1) out vec4 FragColor;
void main()
{
	FragUV = aUV;
	FragColor = aColor;
	gl_Position = PC.Proj * vec4(aPos, 0.0, 1.0);
}
 )";

	constexpr const char* kEditorFragShader = R"(
#version 460
layout(location = 0) in vec2 FragUV;
layout(location = 1) in vec4 FragColor;
layout(set = 0, binding = 0) uniform sampler2D FontTex;
	layout(location = 0) out vec4 OutColor;
	void main()
	{
		OutColor = FragColor * texture(FontTex, FragUV);
	}
	)";
}

namespace Maho
{
	FRHISamplerDesc EditorClampSamplerDesc()
	{
		FRHISamplerDesc S;
		S.AddressU = ERHIAddressMode::ClampToEdge;
		S.AddressV = ERHIAddressMode::ClampToEdge;
		S.AddressW = ERHIAddressMode::ClampToEdge;
		return S;
	}

	std::uint32_t FExampleEditor::PresentTargetTextureId()
	{
		return Name::FName(kPresentTargetTexName).GetId();
	}
}

namespace Maho
{
	// Compile-time FParameters for the editor draw pass (identical shape to the game UI):
	// vertex-stage mat4 push constant + set0/binding0 font sampler.
	BEGIN_SHADER_PARAMETER_STRUCT(FEditorParameters)
		SHADER_PARAMETER_TEXTURE_SAMPLER(Texture2D, FontTexture, FontSampler, 0, 0, ERHIShaderStage::Fragment)
		SHADER_PARAMETER_ARRAY(float, OrthoMatrix, 16)
	END_SHADER_PARAMETER_STRUCT()

	// Statics of FEditorShader: expose the feature-private GLSL to the shader type.
	const char* FEditorShader::GetVertexSource()       { return kEditorVertShader; }
	const char* FEditorShader::GetFragmentSource()     { return kEditorFragShader; }
	const char* FEditorShader::GetVertexEntryPoint()   { return "main"; }
	const char* FEditorShader::GetFragmentEntryPoint() { return "main"; }
}

namespace Maho
{

FExampleEditor::FExampleEditor()
{
	// The editor is the LAST UI writer: it must run AFTER the game UI feature's
	// IRenderUI so its SetPresentTarget(EditorRT) wins the present slot over the game
	// composite. Without this edge the two UI features could run in any order and the
	// editor surface could be overridden back to the game RT.
	MyStage<IEditorCompose>().IsWaiting<FUIFeature>().ForStage<IRenderUI>();
	// Editor runs its compose + submits LAST, after the scene's IEndRender (the scene
	// color mirror the viewport samples is written by then). The reverse edge declares
	// that the frame feature's IPresent (the very last frame stage) runs AFTER this
	// feature's IEditorCompose and consumes the EditorRT it set as the present target.
	// FFrameRenderFeature is always installed, so the edge is satisfied; an absent editor
	// (runtime build) never gets here. No forward WaitFor: FFrameRenderFeature is present
	// in both build types, but this feature only exists in an editor build.
	MyStage<IEditorCompose>().IsWaiting<Scene::FScene>().ForStage<IEndRender>();
	MyStage<IEditorCompose>().IsBlocking<FFrameRenderFeature>().OnStage<IPresent>();
}

bool FExampleEditor::EnsureUIBackend(FRender& R)
{
	if (bUIInit)
	{
		return true;
	}
	ImFontAtlas* Fonts = ImGui::GetIO().Fonts;
	unsigned char* Pixels = nullptr;
	int FontW = 0, FontH = 0, FontBpp = 0;
	Fonts->GetTexDataAsRGBA32(&Pixels, &FontW, &FontH, &FontBpp);
	if (Pixels == nullptr || FontW <= 0 || FontH <= 0)
	{
		MAHO_LOG_CORE_ERROR("ExampleEditor: no font atlas");
		return false;
	}
	FRHITextureDesc TexDesc;
	TexDesc.Format = ERHIFormat::R8G8B8A8_UNORM;
	TexDesc.Dimension = ERHITextureDimension::Tex2D;
	TexDesc.Extent = { static_cast<std::uint32_t>(FontW), static_cast<std::uint32_t>(FontH), 1 };
	TexDesc.MipLevels = 1;
	TexDesc.ArrayLayers = 1;
	TexDesc.Usage = ERHITextureUsage::Sampled | ERHITextureUsage::TransferDst;
	TexDesc.MemoryUsage = ERHIMemoryUsage::GPUOnly;
	FontTexture = R.CreateTexture(TexDesc, ERDGResourceLifetime::Persistent);
	if (!FontTexture.IsValid())
	{
		MAHO_LOG_CORE_ERROR("ExampleEditor: font texture failed");
		return false;
	}
	Fonts->TexID = 0;
	bUIInit = true;
	MAHO_LOG_CORE_INFO("ExampleEditor: font backend ready");
	return true;
}

void FExampleEditor::UploadFont(FRender& R)
{
	if (bFontUploaded)
	{
		return;
	}
	R.AddPass(ERHICommandListType::Graphics, [this, &R](FRHICommandList& Cmd)
	{
		ImFontAtlas* Fonts = ImGui::GetIO().Fonts;
		unsigned char* Pixels = nullptr;
		int FontW = 0, FontH = 0, FontBpp = 0;
		Fonts->GetTexDataAsRGBA32(&Pixels, &FontW, &FontH, &FontBpp);
		if (Pixels != nullptr && FontW > 0 && FontH > 0 && FontTexture.IsValid())
		{
			FRHIBufferDesc StagingDesc;
			StagingDesc.Size = static_cast<std::uint64_t>(FontW) * FontH * 4;
			StagingDesc.Usage = ERHIBufferUsage::TransferSrc;
			StagingDesc.MemoryUsage = ERHIMemoryUsage::CPUToGPU;
			FRDGBufferRef Staging = R.CreateBuffer(StagingDesc, ERDGResourceLifetime::Transient);
			if (!Staging.IsValid() || Staging.GetRHI() == nullptr)
			{
				MAHO_LOG_CORE_ERROR("ExampleEditor: font staging failed");
				return;
			}
			Cmd.UpdateBuffer(Staging.GetRHI(), 0, static_cast<std::uint64_t>(FontW) * FontH * 4, Pixels);
			Cmd.TransitionTexture(FontTexture.GetRHI(), ERHIResourceState::Common, ERHIResourceState::CopyDst);
			Cmd.CopyBufferToTexture(Staging.GetRHI(), FontTexture.GetRHI(), 0);
			Cmd.TransitionTexture(FontTexture.GetRHI(), ERHIResourceState::CopyDst, ERHIResourceState::ShaderResource);
		}
		bFontUploaded = true;
	});
}

void FExampleEditor::OnInstalled(FRender& R)
{
	// THIS feature owns the editor's OWN ImGui context -- fully isolated from the game's
	// UIFeature context. Created at install, before anything touches GetIO(); it becomes
	// the current context for the rest of frame setup (the game feature switches back to
	// its own context at its InitViews). Docking is enabled so the editor shell can dock.
	// NO GLFW backend: input + display size are fed by hand in InitEditorViews (same model
	// as FUIFeature), so there is no process-wide ImGui_ImplGlfw single-instance to clash
	// with anything.
	if (m_Context == nullptr)
	{
		Platform::FPlatform* P = Platform::GetPlatform();
		if (P == nullptr || P->GetWindowWidth() == 0 || P->GetToolkitWindowHandle() == nullptr)
		{
			MAHO_LOG_CORE_ERROR("ExampleEditor::OnInstalled: no window; editor disabled");
			return;
		}
		IMGUI_CHECKVERSION();
		m_Context = ImGui::CreateContext();
		ImGui::SetCurrentContext(m_Context);
		ImGuiIO& IO = ImGui::GetIO();
		IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		IO.IniFilename = "EditorLayout.ini";
		ApplyMahoNightTheme();
		MAHO_LOG_CORE_INFO("ExampleEditor: ImGui context created (editor-own, no GLFW backend)");
	}

	ImGui::SetCurrentContext(m_Context);
	if (!EnsureUIBackend(R))
	{
		return;
	}
	if (!bFontUploaded)
	{
		UploadFont(R);
		if (!bFontUploaded)
		{
			return;
		}
	}
	// Install the editor component plugins (viewport) + run their Init graph at the next
	// safe point, now that this feature's own ImGui context + font backend are ready.
	InstallEditorComponents();
}

void FExampleEditor::InstallEditorComponents()
{
	// Editor components are loaded as DLLs and installed into this host's collector
	// (safe point: their IEditorInit graph runs on FlushPendingUpdatePipelines).
	Install("EditorViewport.dll");
	Install("EditorConsole.dll");
	FlushPendingUpdatePipelines<TTypeList<IEditorInit>, TTypeList<IEditorShutdown>>();
}

void FExampleEditor::InitEditorViews(FRender& R)
{
	if (m_Context == nullptr || !bUIInit)
	{
		return;
	}

	// Switch to the editor's OWN context for the whole frame. Two UI features share the
	// process-wide ImGui DLL; each must select its own context so a frame is never built
	// against the wrong one. UIFeature does the same for its own context.
	ImGui::SetCurrentContext(m_Context);

	std::lock_guard<std::mutex> FrameLock(ImGuiFrameMutex);

	// Record the frame render target the components reach through this host.
	RenderRef = &R;
	EditorContext.bSceneReady = (Scene::GetScene() != nullptr);

	// Rebuild the editor composite target (EditorRT) to the current canvas.
	{
		const std::uint32_t CanvasW = R.GetCanvasWidth();
		const std::uint32_t CanvasH = R.GetCanvasHeight();
		if (CanvasW == 0 || CanvasH == 0)
		{
			return;
		}
		if (!EditorRT.IsValid() || EditorRT.GetWidth() != CanvasW || EditorRT.GetHeight() != CanvasH)
		{
			if (EditorRT.IsValid())
			{
				R.ReleaseTexture(EditorRT);
				EditorRT.Reset();
			}
			FRHITextureDesc Desc;
			Desc.Format = R.GetSwapchainFormat();
			Desc.Dimension = ERHITextureDimension::Tex2D;
			Desc.Extent = { CanvasW, CanvasH, 1 };
			Desc.MipLevels = 1;
			Desc.ArrayLayers = 1;
			Desc.Usage = ERHITextureUsage::ColorAttachment
				| ERHITextureUsage::Sampled
				| ERHITextureUsage::TransferSrc;
			Desc.MemoryUsage = ERHIMemoryUsage::GPUOnly;
			EditorRT = R.CreateTexture(Desc, ERDGResourceLifetime::Persistent);
			if (!EditorRT.IsValid())
			{
				MAHO_LOG_CORE_ERROR("ExampleEditor: EditorRT creation failed");
				return;
			}
			// EditorRT leaves create as UNDEFINED but is used as a dynamic-rendering color
			// attachment (BeginRendering declares COLOR_ATTACHMENT and never auto-transitions).
			// Bring it to COLOR_ATTACHMENT_OPTIMAL once, now, so RenderUI's BeginRendering and
			// PresentTexture's "is a color attachment" barrier find it legal.
			{
				FRHITexture* RT = EditorRT.GetRHI();
				R.AddPass(ERHICommandListType::Graphics, [RT](FRHICommandList& Cmd)
				{
					Cmd.TransitionTexture(RT, ERHIResourceState::Common, ERHIResourceState::RenderTarget);
				});
			}
		}
	}

	// -- Frame feed (manual Win32 input, same model as FUIFeature) -- then NewFrame.
	// No GLFW backend: we set the display size from the platform window and poll cursor
	// + mouse-button state directly via Win32. The editor is the only input-consuming
	// ImGui context in an editor build; the game UI feature stays cold.
	ImGuiIO& IO = ImGui::GetIO();
	Platform::FPlatform* P = Platform::GetPlatform();
	if (P == nullptr)
	{
		return;
	}
	IO.DisplaySize = ImVec2(
		static_cast<float>(P->GetWindowWidth()),
		static_cast<float>(P->GetWindowHeight()));
#if defined(_WIN32)
	if (HWND Hwnd = static_cast<HWND>(P->GetNativeWindow()))
	{
		POINT Pt{};
		if (::GetCursorPos(&Pt) && ::ScreenToClient(Hwnd, &Pt))
		{
			RECT Client{};
			::GetClientRect(Hwnd, &Client);
			const float ScaleX = Client.right > 0 ? IO.DisplaySize.x / static_cast<float>(Client.right) : 1.f;
			const float ScaleY = Client.bottom > 0 ? IO.DisplaySize.y / static_cast<float>(Client.bottom) : 1.f;
			IO.AddMousePosEvent(static_cast<float>(Pt.x) * ScaleX, static_cast<float>(Pt.y) * ScaleY);
		}
		IO.AddMouseButtonEvent(0, (::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
		IO.AddMouseButtonEvent(1, (::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
		IO.AddMouseButtonEvent(2, (::GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0);
	}
#endif

	unsigned char* FontPixels = nullptr;
	int FontW = 0, FontH = 0, FontBpp = 0;
	IO.Fonts->GetTexDataAsRGBA32(&FontPixels, &FontW, &FontH, &FontBpp);
	if (FontPixels == nullptr || FontW <= 0 || FontH <= 0)
	{
		MAHO_LOG_CORE_ERROR("ExampleEditor: font atlas not built");
		return;
	}
	ImGui::NewFrame();

	// Dock frame shell (host-owned) + each editor component draws its own window.
	DrawEditorPanels();

	ImGui::Render();
	ImDrawData* DrawData = ImGui::GetDrawData();
	if (DrawData == nullptr || !DrawData->Valid || DrawData->CmdListsCount <= 0)
	{
		return;
	}

	// Translate ImDrawData -> FDrawList (merged vertex/index buffers uploaded here).
	FDrawList& Out = this->DrawList;
	Out.Reset();
	std::size_t TotalVerts = 0, TotalIndices = 0;
	for (int I = 0; I < DrawData->CmdListsCount; ++I)
	{
		TotalVerts += DrawData->CmdLists[I]->VtxBuffer.Size;
		TotalIndices += DrawData->CmdLists[I]->IdxBuffer.Size;
	}
	if (TotalVerts == 0 || TotalIndices == 0)
	{
		return;
	}
	std::vector<ImDrawVert> Verts(TotalVerts);
	std::vector<ImDrawIdx> Idx(TotalIndices);
	{
		std::size_t OffV = 0, OffI = 0;
		for (int I = 0; I < DrawData->CmdListsCount; ++I)
		{
			const ImDrawList* List = DrawData->CmdLists[I];
			std::memcpy(Verts.data() + OffV, List->VtxBuffer.Data, List->VtxBuffer.Size * sizeof(ImDrawVert));
			std::memcpy(Idx.data() + OffI, List->IdxBuffer.Data, List->IdxBuffer.Size * sizeof(ImDrawIdx));
			OffV += List->VtxBuffer.Size;
			OffI += List->IdxBuffer.Size;
		}
	}
	const std::uint64_t VtxBytes = static_cast<std::uint64_t>(TotalVerts) * sizeof(ImDrawVert);
	const std::uint64_t IdxBytes = static_cast<std::uint64_t>(TotalIndices) * sizeof(ImDrawIdx);
	FRHIBufferDesc VDesc;
	VDesc.Size = VtxBytes;
	VDesc.Usage = ERHIBufferUsage::Vertex;
	VDesc.MemoryUsage = ERHIMemoryUsage::GPUOnly;
	FRDGBufferRef VB = R.CreateBuffer(VDesc, ERDGResourceLifetime::Transient);
	FRHIBufferDesc IDesc;
	IDesc.Size = IdxBytes;
	IDesc.Usage = ERHIBufferUsage::Index;
	IDesc.MemoryUsage = ERHIMemoryUsage::GPUOnly;
	FRDGBufferRef IB = R.CreateBuffer(IDesc, ERDGResourceLifetime::Transient);
	if (!VB.IsValid() || !IB.IsValid())
	{
		MAHO_LOG_CORE_ERROR("ExampleEditor: vertex/index buffer create failed");
		return;
	}
	R.AddPass(ERHICommandListType::Graphics,
		[&VB, &IB, &Verts, &Idx, VtxBytes, IdxBytes](FRHICommandList& Cmd)
		{
			Cmd.UpdateBuffer(VB.GetRHI(), 0, VtxBytes, Verts.data());
			Cmd.UpdateBuffer(IB.GetRHI(), 0, IdxBytes, Idx.data());
		});
	Out.SetVertexBuffer(VB);
	Out.SetIndexBuffer(IB);

	const float OrthoL = DrawData->DisplayPos.x;
	const float OrthoT = DrawData->DisplayPos.y;
	const float OrthoRt = DrawData->DisplayPos.x + DrawData->DisplaySize.x;
	const float OrthoB = DrawData->DisplayPos.y + DrawData->DisplaySize.y;
	const float Ortho[16] = {
		2.0f / (OrthoRt - OrthoL), 0.0f, 0.0f, 0.0f,
		0.0f, 2.0f / (OrthoB - OrthoT), 0.0f, 0.0f,
		0.0f, 0.0f, -1.0f, 0.0f,
		-(OrthoRt + OrthoL) / (OrthoRt - OrthoL), -(OrthoT + OrthoB) / (OrthoB - OrthoT), 0.0f, 1.0f,
	};
	Out.SetPushConstants(ERHIShaderStage::Vertex | ERHIShaderStage::Fragment, static_cast<std::uint32_t>(sizeof(Ortho)), Ortho);

	const float DisplayW = DrawData->DisplaySize.x;
	const float DisplayH = DrawData->DisplaySize.y;
	std::size_t VtxBase = 0, IdxBase = 0;
	for (int I = 0; I < DrawData->CmdListsCount; ++I)
	{
		const ImDrawList* List = DrawData->CmdLists[I];
		for (const ImDrawCmd& DrawCmd : List->CmdBuffer)
		{
			FDrawBatch Batch;
			Batch.VertexCount = DrawCmd.ElemCount;
			Batch.IndexCount = DrawCmd.ElemCount;
			Batch.bIndex32 = sizeof(ImDrawIdx) == 4;
			Batch.VertexOffset = static_cast<std::uint32_t>((VtxBase + DrawCmd.VtxOffset) * sizeof(ImDrawVert));
			Batch.IndexOffset = static_cast<std::uint32_t>((IdxBase + DrawCmd.IdxOffset) * sizeof(ImDrawIdx));
			const ImVec4 Clip = DrawCmd.ClipRect;
			Batch.ScissorX = static_cast<std::int32_t>(Clip.x < 0.0f ? 0.0f : Clip.x);
			Batch.ScissorY = static_cast<std::int32_t>(Clip.y < 0.0f ? 0.0f : Clip.y);
			Batch.ScissorW = static_cast<std::uint32_t>(static_cast<std::int32_t>(Clip.z > DisplayW ? DisplayW : Clip.z) - Batch.ScissorX);
			Batch.ScissorH = static_cast<std::uint32_t>(static_cast<std::int32_t>(Clip.w > DisplayH ? DisplayH : Clip.w) - Batch.ScissorY);
			Batch.bHasScissor = true;
			if (DrawCmd.TextureId != 0)
			{
				// Two resolution paths: a NON-zero ImTextureID is normally a mirror FName id
				// (a registered texture, resolved via GetMirror). But the live present target
				// (game composite UIRenderTarget) has NO mirror entry -- it is CreateTexture'd
				// directly -- so the special present-target id is resolved straight from
				// R.GetPresentTarget() instead. The target is y-down: flip V when displayed.
				const Name::FName TexName = Name::FName::FromId(static_cast<std::uint32_t>(DrawCmd.TextureId));
				if (TexName == Name::FName(kPresentTargetTexName))
				{
					const FRDGTextureRef Present = R.GetPresentTarget();
					if (Present.IsValid())
					{
						FRDGDescriptorSet DescriptorSet;
						DescriptorSet.SetIndex = 0;
						DescriptorSet.Frequency = EDescriptorSetFrequency::Static;
						FRDGBinding Binding;
						Binding.Type = ERHIDescriptorType::CombinedImageSampler;
						Binding.Stages = ERHIShaderStage::Fragment;
						Binding.Resource = Present;
						Binding.SamplerIndex = DescriptorSet.AddSampler(R.CreateSampler(EditorClampSamplerDesc()));
						DescriptorSet.Bindings.push_back({ 0, Binding });
						Batch.Sets.push_back(std::move(DescriptorSet));
					}
				}
				else
				{
					const FRDGResourceRef* Mirror = R.GetMirror(TexName);
					const FRDGTextureRef* Tex = Mirror != nullptr ? std::get_if<FRDGTextureRef>(Mirror) : nullptr;
					if (Tex != nullptr)
					{
						FRDGDescriptorSet DescriptorSet;
						DescriptorSet.SetIndex = 0;
						DescriptorSet.Frequency = EDescriptorSetFrequency::Static;
						FRDGBinding Binding;
						Binding.Type = ERHIDescriptorType::CombinedImageSampler;
						Binding.Stages = ERHIShaderStage::Fragment;
						Binding.Resource = *Tex;
						Binding.SamplerIndex = DescriptorSet.AddSampler(R.CreateSampler(EditorClampSamplerDesc()));
						DescriptorSet.Bindings.push_back({ 0, Binding });
						Batch.Sets.push_back(std::move(DescriptorSet));
					}
				}
			}
			Out.Add(std::move(Batch));
		}
		VtxBase += List->VtxBuffer.Size;
		IdxBase += List->IdxBuffer.Size;
	}
}

void FExampleEditor::DrawEditorPanels()
{
	// Docking host: a fullscreen dockspace behind the editor windows (host-owned frame
	// shell). Each component plugin draws its own window inside it.
	ImGuiViewport* VP = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(VP->WorkPos);
	ImGui::SetNextWindowSize(VP->WorkSize);
	ImGui::SetNextWindowViewport(VP->ID);
	ImGuiWindowFlags HostFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("EditorDock", nullptr, HostFlags);
	ImGui::PopStyleVar(3);
	EditorDockSpaceId = static_cast<std::uint32_t>(ImGui::GetID("EditorDockSpace"));
	ImGui::DockSpace(EditorDockSpaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
	ImGui::End();

	for (IEditorPanel* P : Cast<IEditorPanel>())
	{
		P->Draw(*this);
	}
}

void FExampleEditor::RenderEditorUI(FRender& R)
{
	if (!EditorRT.IsValid())
	{
		return;
	}
	const ERHIFormat ColorFormat = EditorRT.GetFormat();
	FDrawList& DrawList = this->DrawList;
	if (!DrawList.HasPrimitiveData())
	{
		return;
	}

	FRenderTarget Target;
	FRenderTarget::FAttachment Color;
	Color.View = EditorRT;
	Color.LoadOp = ERHILoadOp::Clear;
	Color.StoreOp = ERHIStoreOp::Store;
	Target.AddColor(Color);

	if (!FontTexture.IsValid())
	{
		MAHO_LOG_CORE_ERROR("ExampleEditor: font texture missing");
		return;
	}
	FEditorParameters* Params = R.AllocParameters<FEditorParameters>();
	Params->FontTexture = FontTexture;
	Params->FontSampler = R.CreateSampler(EditorClampSamplerDesc());

	TShaderHandle<FEditorShader> Shader = R.TryGetShader<FEditorShader>();
	FRHIShaderModule* VS = nullptr;
	FRHIShaderModule* FS = nullptr;
	if (Shader.Wait())
	{
		VS = Shader.GetVertex();
		FS = Shader.GetFragment();
	}
	if (VS == nullptr || FS == nullptr)
	{
		MAHO_LOG_CORE_ERROR("ExampleEditor: shader not ready");
		return;
	}

	FRHIGraphicsPipelineDesc PipelineDesc;
	PipelineDesc.VertexShader = VS;
	PipelineDesc.FragmentShader = FS;
	PipelineDesc.VertexShaderHash = Shader.GetVertexHash();
	PipelineDesc.FragmentShaderHash = Shader.GetFragmentHash();
	PipelineDesc.VertexEntryPoint = FEditorShader::GetVertexEntryPoint();
	PipelineDesc.FragmentEntryPoint = FEditorShader::GetFragmentEntryPoint();
	PipelineDesc.RenderPass = nullptr;   // dynamic rendering
	PipelineDesc.Topology = ERHIPrimitiveTopology::TriangleList;
	PipelineDesc.VertexStride = sizeof(ImDrawVert);
	PipelineDesc.Attributes = {
		{ 0, ERHIFormat::R32G32_SFLOAT,  offsetof(ImDrawVert, pos) },
		{ 1, ERHIFormat::R32G32_SFLOAT,  offsetof(ImDrawVert, uv) },
		{ 2, ERHIFormat::R8G8B8A8_UNORM, offsetof(ImDrawVert, col) },
	};
	PipelineDesc.CullMode = ERHICullMode::None;
	PipelineDesc.FillMode = ERHIFillMode::Solid;
	PipelineDesc.ColorFormat = ColorFormat;
	PipelineDesc.DepthFormat = ERHIFormat::Unknown;
	FRHIAttachmentBlend Blend;
	Blend.bBlend = true;
	Blend.SrcColorFactor = ERHIBlendFactor::SrcAlpha;
	Blend.DstColorFactor = ERHIBlendFactor::OneMinusSrcAlpha;
	Blend.SrcAlphaFactor = ERHIBlendFactor::One;
	Blend.DstAlphaFactor = ERHIBlendFactor::OneMinusSrcAlpha;
	PipelineDesc.AttachmentBlends = { Blend };

	// The editor draws only its own ImGui content into EditorRT, and its viewport window
	// samples the live present target (the game composite UIRenderTarget, flipped to
	// SHADER_READ_ONLY by pass2 for exactly this sampled read). Sample it here. No layout
	// re-flip is needed: this frame presents EditorRT (below), so UIRenderTarget stays in
	// SHADER_READ_ONLY until the game UI's next RenderUI head flips it back to
	// COLOR_ATTACHMENT before it writes again -- the RHI never auto-transitions, so the
	// editor must NOT try to reuse it as a render target itself.
	R.AddPass(ERHICommandListType::Graphics, PipelineDesc, Target, Params, DrawList);

	// The editor owns the final on-screen surface in an editor build: set its EditorRT
	// as the present target (last writer wins over the game UI, which may have set the
	// game composite). The frame feature's IPresent blits it to the swapchain.
	R.SetPresentTarget(EditorRT);
}

void FExampleEditor::EditorCompose(FRender& R)
{
	// Pass3 -- the editor's whole frame in a single graph stage (after the game-UI
	// composite IRenderUI, before the frame's IPresent). Split into two private steps:
	// InitViews builds the ImGui frame (feed + NewFrame + panels + Render + translate),
	// RenderUI composes it into EditorRT and takes over the present target. Panels /
	// component draws (DrawEditorPanels) are the content you fill.
	MAHO_LOG_CORE_INFO("ExampleEditor::EditorCompose (pass3)");
	InitEditorViews(R);
	RenderEditorUI(R);
}

void FExampleEditor::PreUnInstall(FRender& R)
{
	// Uninstall the editor components first -- their Shutdown graph runs (and the layer
	// instances + DLLs are freed) before the host tears down the surface/font/context it
	// owns. The editor as a whole is a self-consistent module: it drains its components.
	ShutdownEditorComponents();

	if (EditorRT.IsValid())
	{
		R.ReleaseTexture(EditorRT);
		EditorRT.Reset();
	}
	if (FontTexture.IsValid())
	{
		R.ReleaseTexture(FontTexture);
		FontTexture.Reset();
	}
	// Destroy the ImGui context (no GLFW backend to shut down -- input is hand-fed).
	if (m_Context != nullptr)
	{
		ImGui::SetCurrentContext(nullptr);
		ImGui::DestroyContext(m_Context);
		m_Context = nullptr;
	}
	bUIInit = false;
	bFontUploaded = false;
}

void FExampleEditor::ShutdownEditorComponents()
{
	// Uninstall by DLL path -- symmetric with Install("EditorConsole.dll"). The layer's
	// GetName() is "FEditorConsole", but TryUninstall resolves either form, so this
	// guarantees the component's IEditorShutdown (EditorConsole unbinding its OnLog
	// subscription) runs BEFORE FLog::Shutdown -- no name/module asymmetry to trip on.
	TryUninstall("EditorConsole.dll");
	TryUninstall("EditorViewport.dll");
	FlushPendingUpdatePipelines<TTypeList<IEditorInit>, TTypeList<IEditorShutdown>>();
}

} // namespace Maho

// C export -- the host (FRender collector) loads this DLL and calls CreateLayer() by symbol name.
extern "C" MAHO_EXAMPLEEDITOR_API Maho::FLayerBase* CreateLayer()
{
	return Maho::FExampleEditor::CreateLayer();
}
