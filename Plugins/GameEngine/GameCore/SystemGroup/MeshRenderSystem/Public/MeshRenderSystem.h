#pragma once

#include "MeshRenderSystemApi.h"
#include <GameWorld.h>
#include <Name.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// sol2 is a Big Header and this is a public interface: the binder is declared, not included.
// `MAHO_LUA_BIND_REGISTER` expands to `Type::LuaBind(*static_cast<sol::state*>(...))`, so only the
// translation unit that OWNS the binder needs <sol/sol.hpp> (see MeshRenderSystem.cpp).
namespace sol
{
class state;
}

namespace Maho
{
class FPrimitiveRegistry;

namespace GameWorld
{

/** The material an entity with no `FMaterialComponent` draws with (a `Content/Shaders/<名>.shader`
 *  name). Naming it HERE, on the game side, is deliberate: "what the default material is" is a
 *  gameplay/project decision, and the render side must not have to guess it -- it resolves the name
 *  through the ShaderLibrary and falls back only when that name itself is unusable. */
inline constexpr const char* kDefaultMaterialShader = "DefaultLit";

/**
 * Mesh render component - "this entity is something to draw".
 *
 * Names, not pointers: the render side owns GPU buffers behind a name (`Mesh.Cube` today, an asset
 * path later), so the game side never holds a renderer handle. The material is a SEPARATE component
 * (`FMaterialComponent`) because "what it looks like" and "what shape it is" are edited by different
 * people and shared independently -- an entity with no material component still draws, with the
 * default material (see `kDefaultMaterialShader`).
 */
struct FMeshRenderComponent
{
	Name::FName Mesh;
	bool bVisible = true;
};

/**
 * One material parameter OVERRIDE: the value a designer/script sets on top of the shader's own
 * default.
 *
 * `Name` is matched CASE-INSENSITIVELY (`_basecolor` == `_BaseColor`): the shader property names are
 * a UI convention, and a gameplay API that rejects `_basecolor` only because of spelling is a
 * papercut, not a feature. The resolved, authoritative spelling is what reaches the render side.
 *
 * Two value kinds, exactly the two the .shader syntax can express today: a float4 (`Color` uses all
 * four lanes, `Float`/`Range`/`Int` use lane 0) and a texture asset name (`Texture2D`). A parameter
 * that is not listed here is NOT "zero" -- it is "use the shader's default", which is what keeps a
 * material component down to the one or two lines a designer actually cares about.
 */
struct FMaterialParam
{
	Name::FName Name;
	float Value[4] = {};
	Name::FName Texture;
};

/**
 * Material component - "this is what the entity looks like".
 *
 * `Shader` is a `.shader` name (`DefaultLit`, `UnlitColor`, `Transparent`); it is the KEY into the
 * render side's ShaderLibrary, so the game side never sees bytecode, a layout or a pipeline. Several
 * entities naming the SAME shader with the SAME parameters share ONE material entry in the published
 * snapshot (the system deduplicates per frame, see PublishScene) -- that is "change one material,
 * everything using it changes".
 */
struct FMaterialComponent
{
	Name::FName Shader;                   // None => the default material (kDefaultMaterialShader)
	std::vector<FMaterialParam> Params;   // overrides only; anything absent falls back to the shader default
};

/**
 * Camera component - "this entity is the eye".
 *
 * The values here are the application-level parameters a script or a designer would touch (fov /
 * near / far). The system turns the entity's transform into a VIEW matrix and publishes that plus
 * these numbers -- the PROJECTION is composed on the render side, because its aspect depends on the
 * draw target (see `FRenderCamera`). The values are already in the units the projection math wants:
 * radians and world units, not degrees and centimetres.
 */
struct FCameraComponent
{
	float FovY = 1.0471975512f;   // 60 degrees, in radians (the unit the projection math wants)
	float Near = 0.1f;
	float Far = 100.0f;
	bool bActive = true;
};

/**
 * Input component - the frame's input snapshot for ONE entity.
 *
 * The world fills it from the platform at `Update` and a script reads it; the script is deliberately
 * not allowed to call the platform itself (a script must not know the OS, and a per-entity view of
 * input is what makes two entities controllable in different ways).
 *
 *   MoveX/MoveZ - strafe / forward axis, [-1, 1]  (A/D, S/W)
 *   LookX/LookY - look delta for THIS frame    (mouse motion while the left button is held)
 */
struct FInputComponent
{
	float MoveX = 0.f;
	float MoveZ = 0.f;
	float LookX = 0.f;
	float LookY = 0.f;
};

/** One script instance. DEFINED IN THE PRIVATE HEADER: it holds a `sol::table`, and this interface
 *  must stay free of sol2 / of the Lua backend. The component keeps a `shared_ptr` to it, so the
 *  instance outlives the component copy the pool makes (see `TComponentPool`) and is destroyed
 *  through the deleter its own module baked in -- no cross-module `delete`. */
struct FScriptInstance;

/**
 * Script component - "this entity's behaviour lives in a Lua table".
 *
 * `EntityId` is the id the script sees: the system injects it into the loaded table as
 * `self.entity`, and every `maho.*` binding takes it as its first argument. `Call` carries 0 or 1
 * float and nothing else, so the entity has to travel as data the script already owns -- that is
 * exactly what this id is for (the id <-> FEntity mapping lives in the system, see
 * `FMeshRenderSystem::LuaIdToEntity`).
 */
struct FScriptComponent
{
	std::shared_ptr<FScriptInstance> Script;
	std::uint32_t EntityId = 0;
};

/**
 * MeshRenderSystem - the GAME side of the mirror (Unity-shaped: components here, rendering there).
 *
 * One Update per frame does four things, in order:
 *   1. fill every `FInputComponent` from the platform's published input frame;
 *   2. call every `FScriptComponent`'s `update(dt)` -- the gameplay lives in the script;
 *   3. walk (FTransform + FMeshRenderComponent) entities, compose each TRS into an object matrix and
 *      `Publish` the whole scene: primitives + a DEDUPLICATED material table + its parameter values
 *      (see `PublishScene` and PrimitiveRegistry.h's `FRenderScene`);
 *   4. compose the active camera entity's VIEW matrix and `PublishCamera` it together with its
 *      frustum (fov/near/far) -- the projection itself is the render side's, see `FRenderCamera`.
 *
 * The render side (MeshFeature) reads both channels and knows nothing about entities, components or
 * transforms. Nothing here touches the RHI, the renderer, or a GPU buffer -- it produces numbers.
 */
class MAHO_MESHRENDERSYSTEM_API FMeshRenderSystem
	: public FFrameExtension
	, public IPipeline<IOnInstalled, IProcessInput, IUpdate, IPreUnInstall>
{
	MAHO_DECLARE_FRAME_UNDER(FMeshRenderSystem, FGameWorld);

public:
	void OnInstalled(FGameWorld& World, FGameWorldContext& Frame) override;
	/** 输入快照：世界的 IProcessInput 阶段（固定/常规 update 之前跑一次，并已 Wait 过，
	 *  所以 Update 里的脚本读到的是一份settled 的输入）。 */
	void ProcessInput(FGameWorld& World, FGameWorldContext& Frame) override;
	void Update(FGameWorld& World, FGameWorldContext& Frame) override;
	void PreUnInstall(FGameWorld& World, FGameWorldContext& Frame) override;

	/** Lua bindings for the game side (`maho.set_position/get_position/...`). Defined in the cpp; the
	 *  declaration here only forward-declares `sol::state`. */
	static void LuaBind(sol::state& Lua);

private:
	/** 种演示内容：一个自转立方体 entity + 一个相机 entity（两者都挂脚本）。脚本缺失就响亮上报。 */
	void SeedDemoEntities(FGameWorld& World);

	/** 给 entity 分配一个脚本可见的整数 id（从 1 开始，0 表示"没有 entity"）。 */
	std::uint32_t AssignLuaId(FEntity E);

	/** 整数 id → entity；未登记或已死返回无效 handle。 */
	[[nodiscard]] FEntity FindEntity(std::uint32_t LuaId) const;

	/** entity → 整数 id（脚本调用时反查）。找不到返回 0。 */
	[[nodiscard]] std::uint32_t FindLuaId(FEntity E) const;

	/** 把一个已加载的脚本表挂到 entity 上（注入 `self.entity`，登记组件）。 */
	void AttachScript(FGameWorld& World, FEntity E, const char* ScriptFile);

	/** 确保 Lua 绑定真的装上了。`MAHO_LUA_BIND_REGISTER` 在"Lua 后端还没注册"时会被
	 *  `RegisterLanguage` **静默丢掉**（`GetLanguage("Lua")==nullptr` 那一路没有上报），
	 *  所以这里每帧检查一次：状态一可用就立刻直接 bind，长时间装不上就一次性响亮上报。 */
	void EnsureLuaBindings();

	/** 状态变化时的诊断行（脚本数/文件、mesh entity 数、相机数、镜像表可用性、发布计数）。 */
	void ReportState(FGameWorld& World, Maho::FPrimitiveRegistry* Registry);

	/** 每帧从平台填 `FInputComponent`（键位 + 鼠标视角增量）；在 `ProcessInput` 里调用。 */
	void FillInput(FGameWorld& World);

	/** 材质参数查找/写入（名字**大小写不敏感**）：脚本按 `_basecolor` 写、shader 里写 `_BaseColor`
	 *  都能命中同一条覆盖。返回 nullptr = 没有该名字的覆盖（不是错误，回退 shader 默认值）。 */
	static FMaterialParam* FindParam(FMaterialComponent& Material, std::string_view ParamName);
	/** 同上的写入侧：命中就改，没命中就追加一条。`Name` 用调用方给的拼写（首次出现者为准）。 */
	static FMaterialParam& EnsureParam(FMaterialComponent& Material, Name::FName ParamName);

	/** 把 (FTransform + FMeshRenderComponent) 的 entity 合成对象矩阵，并把材质表 + 参数表
	 *  去重后一起发布（三段同一次 swap，见 PrimitiveRegistry.h 的 FRenderScene）。 */
	void PublishScene(FGameWorld& World);

	/** 把激活的相机 entity 合成 **view 矩阵 + 视锥参数（fov/near/far）** 并发布；没有相机就发布
	 *  "没有相机"。**不发布投影**：投影的宽高比属于绘制目标（渲染侧），见 FRenderCamera。 */
	void PublishCamera(FGameWorld& World);

	FEntity CubeEntity;      // demo:脚本驱动自转
	FEntity Cube2Entity;     // demo:第二个 actor,不同 transform + 自己的脚本实例
	FEntity CameraEntity;    // demo:脚本读输入驱动

	/** 脚本可见 id ↔ entity 的双向映射。id 由本系统分配（1 起），不直接暴露 `FEntity::Index`：
	 *  句柄是 (Index, Generation) 对，把 Index 当 id 交给脚本会在槽位复用后指向别的 entity。 */
	std::unordered_map<std::uint32_t, FEntity> LuaIdToEntity;
	std::unordered_map<std::uint32_t, std::uint32_t> EntityToLuaId;
	std::uint32_t NextLuaId = 1;

	/** 上一帧的鼠标位置（鼠标视角按"位移"算，快照里只有绝对位置）。 */
	float PrevMouseX = 0.f;
	float PrevMouseY = 0.f;
	bool bHasPrevMouse = false;

	/** 诊断计数：发布出去多少帧（"游戏侧活着"的可观测证据）。 */
	std::uint64_t PublishCount = 0;

	/** 上一帧发布出去的材质表 / 参数表大小（去重后）：诊断行里的读数，也是"共享材质生效"的证据。 */
	std::size_t PublishedMaterials = 0;
	std::size_t PublishedParams = 0;

	/** 相机发布了多少帧（0 = 一帧都没发过，"从来没有相机"与"相机中途消失"是两回事）。 */
	std::uint64_t CameraPublishCount = 0;

	/** 一次性上报开关：同一个毛病每帧刷屏就没人在意了，但一声不响地黑屏更糟。 */
	bool bWarnedNoMeshName = false;
	bool bWarnedNoCamera = false;

	/** Lua 绑定是否已装（装不上就每帧重试，超过宽限期一次性响亮上报 —— 见 EnsureLuaBindings）。 */
	bool bLuaBindingsInstalled = false;
	bool bWarnedNoLuaBindings = false;
	std::uint64_t FramesWithoutLuaBindings = 0;

	/** 镜像表还没起来的帧数（同 frame 0 竞态 vs 真的没装：宽限期之后才响亮上报）。 */
	std::uint64_t FramesWithoutRegistry = 0;
	bool bReportedNoRegistry = false;

	/** 上一次诊断行的状态签名：只有状态真的变了才再打一行（发布计数每帧都在涨，不进签名）。 */
	std::size_t ReportedScripts = static_cast<std::size_t>(-1);
	std::size_t ReportedMeshEntities = static_cast<std::size_t>(-1);
	std::size_t ReportedCameras = static_cast<std::size_t>(-1);
	std::string ReportedScriptFiles;
	bool bReportedRegistry = false;
	bool bReportedCamera = false;
	bool bReportedBindings = false;
};

} // namespace GameWorld
} // namespace Maho
