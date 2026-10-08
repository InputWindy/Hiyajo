#include "MeshRenderSystem.h"

// sol2 只在本文件的翻译单元里出现：公共头只前向声明 `sol::state`（见 MeshRenderSystem.h）。
// 宏与 Lua 后端保持一致（Script.cpp 同样在包含 sol.hpp 之前打开它），否则头文件里的
// inline 实体在两处会不一致。
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <Log.h>
#include <Platform.h>
#include <PrimitiveRegistry.h>
#include <Script.h>
#include <Trace.h>

// 矩阵数学交给 glm（引擎核心以 PUBLIC 链了 glm::glm，见 Tools/maho_tools.py 的 Maho target）。
// glm 与 GLSL 同为列主序，value_ptr 出来的 16 个 float 就是镜像边界要的那 16 个数。
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace Maho
{
namespace GameWorld
{

/** 脚本实例：一个 Lua 表 + 它来自哪个文件。定义在私有实现里（公共头只放前向声明），
 *  这样 `FScriptComponent` 的接口不依赖 sol2 / 不依赖 Lua 后端。 */
struct FScriptInstance
{
	sol::table Table;
	std::string File;
	bool bHasUpdate = false;
};

namespace
{
	/** 全局实例指针（同 `GUISystem` 的形状）。Lua 的自由函数拿不到 `this`，只能靠它：
	 *  OnInstalled 置上、PreUnInstall 清掉，期间才可能有脚本调用。 */
	FMeshRenderSystem* GMeshRenderSystem = nullptr;

	// GLFW 的键码是稳定的平台无关整数，这里只需要几个（引擎不把 glfw.h 带进游戏侧）。
	constexpr int kKeyA = 65;
	constexpr int kKeyD = 68;
	constexpr int kKeyW = 87;
	constexpr int kKeyS = 83;

	// -- 矩阵工具：列主序（GLSL mat4 的内存序，M[列 * 4 + 行]）。数学全部走 glm -----------------
	// 这些函数刻意留在 GAME 侧：TRS 的合成顺序 / 单位 / 层级是游戏语义，渲染侧只该收到 16 个 float。
	// glm 与 GLSL 同为列主序，value_ptr 出来的 16 个 float 可以原样 memcpy 过镜像边界。

	/** glm::mat4 -> 调用方要的列主序 float[16]（镜像边界的格式，不要改）。 */
	void StoreMatrix(const glm::mat4& M, float* Out)
	{
		std::memcpy(Out, glm::value_ptr(M), sizeof(float) * 16);
	}

	/** 欧拉角 -> 旋转矩阵：R = Rz * Ry * Rx（先绕 X，再 Y，最后 Z —— 也就是 Unity 的 ZXY 直觉）。
	 *  单轴用例（只有 RotationY 或只有 RotationX）与旧版 MeshFeature 的 `RotY * RotX` 完全一致。 */
	glm::mat4 RotationXYZ(float RX, float RY, float RZ)
	{
		return glm::rotate(glm::mat4(1.0f), RZ, glm::vec3(0.0f, 0.0f, 1.0f))     // Rz
		     * glm::rotate(glm::mat4(1.0f), RY, glm::vec3(0.0f, 1.0f, 0.0f))     // * Ry
		     * glm::rotate(glm::mat4(1.0f), RX, glm::vec3(1.0f, 0.0f, 0.0f));    // * Rx
	}

	void MakeTranslation(float X, float Y, float Z, float* M)
	{
		StoreMatrix(glm::translate(glm::mat4(1.0f), glm::vec3(X, Y, Z)), M);
	}

	void MakeScale(float X, float Y, float Z, float* M)
	{
		StoreMatrix(glm::scale(glm::mat4(1.0f), glm::vec3(X, Y, Z)), M);
	}

	void MakeRotationX(float Angle, float* M)
	{
		StoreMatrix(glm::rotate(glm::mat4(1.0f), Angle, glm::vec3(1.0f, 0.0f, 0.0f)), M);
	}

	void MakeRotationY(float Angle, float* M)
	{
		StoreMatrix(glm::rotate(glm::mat4(1.0f), Angle, glm::vec3(0.0f, 1.0f, 0.0f)), M);
	}

	void MakeRotationZ(float Angle, float* M)
	{
		StoreMatrix(glm::rotate(glm::mat4(1.0f), Angle, glm::vec3(0.0f, 0.0f, 1.0f)), M);
	}

	/** TRS -> 对象矩阵：M = T * R * S。 */
	void ComposeTransform(const FTransform& T, float* Out)
	{
		const glm::mat4 Translation = glm::translate(glm::mat4(1.0f), glm::vec3(T.X, T.Y, T.Z));
		const glm::mat4 Rotation    = RotationXYZ(T.RotationX, T.RotationY, T.RotationZ);
		const glm::mat4 Scale       = glm::scale(glm::mat4(1.0f), glm::vec3(T.ScaleX, T.ScaleY, T.ScaleZ));
		StoreMatrix(Translation * Rotation * Scale, Out);
	}

	/** 相机的 view 矩阵：刚体变换的逆（旋转部分转置，平移部分 -R^T * P），也就是 (T * R) 的逆。
	 *  相机不带缩放 —— 缩放是"物体怎么被拉长"，不该改变视锥的空间。
	 *
	 *  **投影不在这里**：fov/near/far 和 view 一起发到渲染侧，由**拥有绘制目标的一方**按目标的真实
	 *  宽高比组投影（见 FRenderCamera 与 FMeshFeature::Render）—— 游戏侧的窗口尺寸和这一帧真正画进
	 *  去的目标不是一回事，用错那个就会在某个轴上把画面压扁。 */
	void MakeViewFromCamera(const FTransform& T, float* M)
	{
		const glm::mat4 Rigid = glm::translate(glm::mat4(1.0f), glm::vec3(T.X, T.Y, T.Z))
		                      * RotationXYZ(T.RotationX, T.RotationY, T.RotationZ);
		StoreMatrix(glm::inverse(Rigid), M);
	}

	/** 大小写不敏感比较：材质参数名是 UI 约定（`_BaseColor`），脚本写 `_basecolor` 必须命中同一条。 */
	bool EqualsIgnoreCase(std::string_view A, std::string_view B)
	{
		if (A.size() != B.size())
		{
			return false;
		}
		for (std::size_t I = 0; I < A.size(); ++I)
		{
			const unsigned char CA = static_cast<unsigned char>(A[I]);
			const unsigned char CB = static_cast<unsigned char>(B[I]);
			if (std::tolower(CA) != std::tolower(CB))
			{
				return false;
			}
		}
		return true;
	}

	/** 一个材质的去重键：shader 名 + 参数覆盖（按参数名的规范化拼写排序，再逐条序列化数值 /
	 *  纹理名）。两个 entity 的组件内容完全一致 → 同一个键 → 同一条材质表项（参数共享）。
	 *
	 *  排序让"声明顺序不同、内容相同"的两份组件也归并到一条；代价是每帧一次字符串拼装，
	 *  与整帧的矩阵合成相比可以忽略。 */
	std::string MakeMaterialKey(Name::FName Shader, const FMaterialComponent& Material)
	{
		std::vector<std::string> Entries;
		Entries.reserve(Material.Params.size());
		for (const FMaterialParam& P : Material.Params)
		{
			std::string Entry(P.Name.ToString());
			for (char& C : Entry)
			{
				C = static_cast<char>(std::tolower(static_cast<unsigned char>(C)));
			}
			Entry += '=';
			for (float V : P.Value)
			{
				Entry += std::to_string(V);
				Entry += ',';
			}
			Entry += P.Texture.IsNone() ? "-" : std::string(P.Texture.ToString());
			Entries.push_back(std::move(Entry));
		}
		std::sort(Entries.begin(), Entries.end());

		std::string Key(Shader.ToString());
		Key += '|';
		for (const std::string& E : Entries)
		{
			Key += E;
			Key += ';';
		}
		return Key;
	}
} // namespace

void FMeshRenderSystem::OnInstalled(FGameWorld& World, FGameWorldContext&)
{
	MAHO_TRACE_STAGE(IOnInstalled, "Mesh render system install", "register the Lua bindings and seed the demo entities");
	GMeshRenderSystem = this;

	// 绑定注册：两条路，缺一不可 ——
	//   1. 后端已经 initialize（`TryGetState()` 非空）：**直接** bound。这条是必需的那条：
	//      `MAHO_LUA_BIND_REGISTER` 走 `FScriptSystem::RegisterTypeBinder("Lua", ...)`，而
	//      `RegisterLanguage`/`GetLanguage` 在"Lua 后端还没注册"时返回 nullptr，那一分支**没有 else**，
	//      绑定就被**静默丢掉**了（本系统由世界安装，早于脚本系统的 IInit）。丢掉的症状是脚本里
	//      `maho` 为 nil，每帧一条 "attempt to index a nil value (global 'maho')"。
	//   2. 后端还没 initialize：退回队列形式（后端会在 Initialize 之后跑一遍队列）。这条只是兜底，
	//      因为此时 "Lua" 这个名字可能都还没注册，队列会落空 —— 所以 Update 里还有每帧重试。
	if (Script::GetScriptSystem() != nullptr)
	{
		if (void* State = Script::GetScriptSystem()->TryGetState())
		{
			FMeshRenderSystem::LuaBind(*static_cast<sol::state*>(State));
			bLuaBindingsInstalled = true;
		}
		else
		{
			MAHO_LUA_BIND_REGISTER(FMeshRenderSystem);
		}
	}
	else
	{
		MAHO_LOG_CORE_ERROR("MeshRenderSystem: no script system installed -- gameplay bindings are NOT registered");
	}

	SeedDemoEntities(World);
}

void FMeshRenderSystem::EnsureLuaBindings()
{
	if (bLuaBindingsInstalled)
	{
		return;
	}

	Script::FScriptSystem* System = Script::GetScriptSystem();
	void* State = System != nullptr ? System->TryGetState() : nullptr;
	if (State == nullptr)
	{
		// 后端还在 initialize（或根本没装 script 系统）：这一帧还没有可以 bind 的状态。
		// 宽限期（约 2 秒@60Hz）之后一次性响亮上报 —— 绑定装不上必须留证据，不能静默。
		++FramesWithoutLuaBindings;
		if (!bWarnedNoLuaBindings && FramesWithoutLuaBindings > 120)
		{
			bWarnedNoLuaBindings = true;
			MAHO_LOG_CORE_ERROR(
				"MeshRenderSystem: Lua bindings are NOT installed after {} frames (script system {}, state {}) -- "
				"every script calling maho.* fails; the gameplay bindings never reached the Lua state",
				FramesWithoutLuaBindings, System != nullptr ? "present" : "missing",
				System != nullptr ? "missing" : "n/a");
		}
		return;
	}

	// 状态可用了：立刻直接 bind（这就是"装不上"的正常修补路径 —— 后端的队列在
	// "Lua 名字还没注册"时会把 binder 丢掉，所以不能只靠队列）。
	FMeshRenderSystem::LuaBind(*static_cast<sol::state*>(State));
	bLuaBindingsInstalled = true;
	FramesWithoutLuaBindings = 0;
	bWarnedNoLuaBindings = false;
}

void FMeshRenderSystem::SeedDemoEntities(FGameWorld& World)
{
	// 演示内容：一个自转立方体 + 一个相机，两者都是普通 entity（变换 + 组件 + 脚本）。
	// C++ 只搭骨架，行为全在 Scripts/*.lua 里 —— 改转速/操控不需要重新编译引擎。
	FEntity Cube = World.CreateEntity();
	FTransform CubeTransform;   // 位置 (0,0,0)、旋转 0、缩放 1 —— 姿态由脚本负责
	World.AddComponent<FTransform>(Cube, CubeTransform);
	FMeshRenderComponent CubeMesh;
	CubeMesh.Mesh = Name::FName("Mesh.Cube");
	World.AddComponent<FMeshRenderComponent>(Cube, CubeMesh);
	// 材质：DefaultLit + 一条**明确的** `_BaseColor` 覆盖。覆盖会盖掉 shader 文件里的默认值
	// （DefaultLit.shader 里那行是偏冷的浅蓝），所以画面上看到的颜色同时证明"参数真的到了 GPU"
	// 和"组件优先级高于 shader 默认值"。`_Metallic` 故意不写：不在组件里的参数回退 shader 默认值。
	FMaterialComponent CubeMaterial;
	CubeMaterial.Shader = Name::FName("DefaultLit");
	{
		FMaterialParam BaseColor;
		BaseColor.Name = Name::FName("_BaseColor");
		BaseColor.Value[0] = 0.20f;
		BaseColor.Value[1] = 0.85f;
		BaseColor.Value[2] = 0.45f;
		BaseColor.Value[3] = 1.0f;
		CubeMaterial.Params.push_back(BaseColor);
	}
	World.AddComponent<FMaterialComponent>(Cube, std::move(CubeMaterial));
	AttachScript(World, Cube, "cube.lua");
	CubeEntity = Cube;

	// 第二个立方体：位置/缩放都不同，且挂**另一个**脚本 —— 于是"两个 actor 同屏"和
	// "每个 entity 有自己的脚本实例"是同一条证据（脚本表是每 entity 一份，状态互不干扰）。
	// 它用**另一个 shader**（UnlitColor）且**不写任何参数**：画面上它的颜色就是
	// Content/Shaders/UnlitColor.shader 里那行默认值 —— "shader 文本的默认值真的被解析"的活证据。
	FEntity Cube2 = World.CreateEntity();
	FTransform Cube2Transform;
	Cube2Transform.X = -1.8f;
	Cube2Transform.Z = 0.8f;
	Cube2Transform.ScaleX = Cube2Transform.ScaleY = Cube2Transform.ScaleZ = 0.55f;
	World.AddComponent<FTransform>(Cube2, Cube2Transform);
	FMeshRenderComponent Cube2Mesh;
	Cube2Mesh.Mesh = Name::FName("Mesh.Cube");
	World.AddComponent<FMeshRenderComponent>(Cube2, Cube2Mesh);
	FMaterialComponent Cube2Material;
	Cube2Material.Shader = Name::FName("UnlitColor");
	World.AddComponent<FMaterialComponent>(Cube2, std::move(Cube2Material));
	AttachScript(World, Cube2, "cube2.lua");
	Cube2Entity = Cube2;

	FEntity Camera = World.CreateEntity();
	FTransform CameraTransform;
	CameraTransform.Z = 3.0f;   // 退到 +Z，朝 -Z 看（与旧版手工相机同一视角）
	World.AddComponent<FTransform>(Camera, CameraTransform);
	World.AddComponent<FCameraComponent>(Camera, FCameraComponent{});
	World.AddComponent<FInputComponent>(Camera, FInputComponent{});
	AttachScript(World, Camera, "camera.lua");
	CameraEntity = Camera;

	MAHO_LOG_CORE_INFO("MeshRenderSystem: seeded demo entities (cube id {}, cube2 id {}, camera id {})",
		FindLuaId(Cube), FindLuaId(Cube2), FindLuaId(Camera));
}

std::uint32_t FMeshRenderSystem::AssignLuaId(FEntity E)
{
	// 幂等：同一个 entity 永远拿到同一个 id。
	if (const auto It = EntityToLuaId.find(E.Index); It != EntityToLuaId.end())
	{
		return It->second;
	}
	const std::uint32_t Id = NextLuaId++;
	LuaIdToEntity.emplace(Id, E);
	EntityToLuaId.emplace(E.Index, Id);
	return Id;
}

FEntity FMeshRenderSystem::FindEntity(std::uint32_t LuaId) const
{
	const auto It = LuaIdToEntity.find(LuaId);
	return It == LuaIdToEntity.end() ? FEntity{} : It->second;
}

std::uint32_t FMeshRenderSystem::FindLuaId(FEntity E) const
{
	const auto It = EntityToLuaId.find(E.Index);
	return It == EntityToLuaId.end() ? 0u : It->second;
}

FMaterialParam* FMeshRenderSystem::FindParam(FMaterialComponent& Material, std::string_view ParamName)
{
	for (FMaterialParam& P : Material.Params)
	{
		if (EqualsIgnoreCase(P.Name.ToString(), ParamName))
		{
			return &P;
		}
	}
	return nullptr;
}

FMaterialParam& FMeshRenderSystem::EnsureParam(FMaterialComponent& Material, Name::FName ParamName)
{
	if (FMaterialParam* Existing = FindParam(Material, ParamName.ToString()))
	{
		return *Existing;
	}
	FMaterialParam Fresh;
	Fresh.Name = ParamName;   // 首次出现者的拼写成为权威拼写（发到渲染侧的就是它）
	Material.Params.push_back(Fresh);
	return Material.Params.back();
}

void FMeshRenderSystem::AttachScript(FGameWorld& World, FEntity E, const char* ScriptFile)
{
	const std::uint32_t Id = AssignLuaId(E);

	Script::FScriptSystem* System = Script::GetScriptSystem();
	if (System == nullptr || System->GetActive() == nullptr)
	{
		MAHO_LOG_CORE_ERROR("MeshRenderSystem: no active script language -- '{}' NOT loaded for entity {}", ScriptFile, Id);
		return;
	}

	// 路径相对脚本目录解析（后端把 `Scripts` 作为基准，所以这里只给文件名）。
	// `LoadScript<sol::object>` 而不是 `<sol::table>`：后端交出的是 `new sol::object`，
	// 用 object 接住再判定/转换，类型是对的（拿 table 去 static_cast 一个 object 才是错的）。
	sol::object Loaded = System->LoadScript<sol::object>(ScriptFile);
	if (!Loaded.is<sol::table>())
	{
		MAHO_LOG_CORE_ERROR(
			"MeshRenderSystem: script '{}' did not load (missing file, syntax error, or no returned table) "
			"-- entity {} has no behaviour this run", ScriptFile, Id);
		return;
	}

	sol::table Table = Loaded.as<sol::table>();

	// 脚本与 entity 的连接方式（唯一、显式）：系统把整数 id 写进加载出来的表，
	// 脚本里 `self.entity` 因此就是自己的 id；`update(dt)` 只用 dt（Call 只支持 0/1 个 float 参数），
	// 其余信息全部靠这个 id 回查（maho.get_position(self.entity) 等）。
	Table["entity"] = Id;

	auto Instance = std::make_shared<FScriptInstance>();
	Instance->Table = Table;
	Instance->File = ScriptFile;
	Instance->bHasUpdate = Table["update"].is<sol::function>();
	if (!Instance->bHasUpdate)
	{
		// 响亮上报：脚本在，但没有 update —— 该 entity 这一轮不会动（其余 entity 照跑）。
		MAHO_LOG_CORE_ERROR("MeshRenderSystem: script '{}' has no 'update(dt)' -- entity {} will not move",
			ScriptFile, Id);
	}

	// 读出来再 move：`std::move` 之后 Instance 已经空了（下面那行日志要用这个值）。
	const bool bHasUpdate = Instance->bHasUpdate;

	FScriptComponent Component;
	Component.Script = std::move(Instance);
	Component.EntityId = Id;
	World.AddComponent<FScriptComponent>(E, std::move(Component));

	MAHO_LOG_CORE_INFO("MeshRenderSystem: entity {} bound to script '{}'{}", Id, ScriptFile,
		bHasUpdate ? "" : " (no update)");
}

void FMeshRenderSystem::FillInput(FGameWorld& World)
{
	Platform::FPlatform* P = Platform::GetPlatform();
	if (P == nullptr)
	{
		return;
	}

	// 用"已发布的输入帧"而不是平台的实时工作副本：本阶段跑在世界的任务图里，和平台的 pump
	// 线程不同步，只有发布出来的那一帧是完整且不会再被改的（Platform.h 的 ring 注释）。
	Platform::MInputContext In;
	P->ReadFrameInput(In);

	// 鼠标视角按"本帧位移"算（快照里只有绝对位置），并且只在按住左键时生效：
	// 否则指针随便一飘（自动化脚本点个标签页）就会把相机甩走。
	float LookX = 0.f;
	float LookY = 0.f;
	if (In.MouseButtons[0] && bHasPrevMouse)
	{
		LookX = In.MouseX - PrevMouseX;
		LookY = In.MouseY - PrevMouseY;
	}
	PrevMouseX = In.MouseX;
	PrevMouseY = In.MouseY;
	bHasPrevMouse = true;

	const float MoveX = (In.KeyDown[kKeyD] ? 1.f : 0.f) - (In.KeyDown[kKeyA] ? 1.f : 0.f);
	const float MoveZ = (In.KeyDown[kKeyW] ? 1.f : 0.f) - (In.KeyDown[kKeyS] ? 1.f : 0.f);

	for (FEntity E : World.GetAllWithComponent<FInputComponent>())
	{
		if (FInputComponent* Input = World.GetComponent<FInputComponent>(E))
		{
			Input->MoveX = MoveX;
			Input->MoveZ = MoveZ;
			Input->LookX = LookX;
			Input->LookY = LookY;
		}
	}
}

void FMeshRenderSystem::ProcessInput(FGameWorld& World, FGameWorldContext&)
{
	// 世界在派发完本阶段后立刻 Wait —— 固定步进与常规 update 因此读到的是一份已定型的输入。
	MAHO_TRACE_STAGE(IProcessInput, "Mesh render system input", "snapshot the platform input into the components");
	FillInput(World);
}

void FMeshRenderSystem::Update(FGameWorld& World, FGameWorldContext& Frame)
{
	MAHO_TRACE_STAGE(IUpdate, "Mesh render system update", "scripts -> publish the mirror channels");

	// (a) 输入快照已在 ProcessInput 里填好（本函数不再重复填）

	// (a2) 绑定自检：安装时后端还没起来的话，这里每帧重试到装上为止（装不上会一次性上报）。
	EnsureLuaBindings();

	// (b) gameplay：每个挂脚本的 entity 调一次 update(dt)。dt 用世界自己的帧时（与固定步进同源）。
	const float Delta = World.GetDeltaSeconds();
	for (FEntity E : World.GetAllWithComponent<FScriptComponent>())
	{
		FScriptComponent* Component = World.GetComponent<FScriptComponent>(E);
		if (Component == nullptr || Component->Script == nullptr)
		{
			continue;
		}
		if (!Component->Script->bHasUpdate)
		{
			continue;   // 加载时已响亮上报过（不缺文件就缺 update），这里不重复刷屏
		}

		Script::FScriptSystem* System = Script::GetScriptSystem();
		if (System == nullptr)
		{
			continue;
		}
		// 脚本里的异常由后端包住（protected_function），失败会打错误日志并返回 false，
		// 不中断这一帧其余 entity。
		System->Call(Component->Script->Table, "update", Delta);
	}

	// (c)(d) 合成并发布两条镜像通道
	PublishScene(World);
	PublishCamera(World);

	// (e) 诊断：状态一变就打一行（脚本/文件、要画的 entity 数、发布计数、相机有没有发出去）。
	//     这是"游戏侧到底活着没有"的唯一可观测点：画不出东西时先看这一行。
	ReportState(World, GetPrimitiveRegistry());
}

void FMeshRenderSystem::ReportState(FGameWorld& World, Maho::FPrimitiveRegistry* Registry)
{
	// 镜像表本身没起来 = 这一路发不出去任何东西（PublishPrimitives/PublishCamera 都直接 return）。
	// 头几帧是收集器竞态（世界与渲染各跑一套阶段，没有顺序边），宽限期之后还空就是真没装 ——
	// 这条必须响亮：否则现象就是"画面全黑且日志里一句都没有"。
	if (Registry == nullptr)
	{
		++FramesWithoutRegistry;
		if (!bReportedNoRegistry && FramesWithoutRegistry > 120)
		{
			bReportedNoRegistry = true;
			MAHO_LOG_CORE_ERROR(
				"MeshRenderSystem: GetPrimitiveRegistry() is NULL after {} frames -- the mirror table frame "
				"(FPrimitiveRegistry) did not initialize: nothing is published and the render side has nothing "
				"to draw (it must be installed by the HOST, not by a render collector -- see PrimitiveRegistry.h)",
				FramesWithoutRegistry);
		}
	}
	else
	{
		FramesWithoutRegistry = 0;
		if (bReportedNoRegistry)
		{
			bReportedNoRegistry = false;
			MAHO_LOG_CORE_INFO("MeshRenderSystem: mirror registry became available (publishes={})",
				Registry->GetPublishCount());
		}
	}

	std::size_t Scripts = 0;
	std::string Files;
	for (FEntity E : World.GetAllWithComponent<FScriptComponent>())
	{
		if (FScriptComponent* Component = World.GetComponent<FScriptComponent>(E); Component != nullptr)
		{
			++Scripts;
			if (Component->Script != nullptr)
			{
				if (!Files.empty())
				{
					Files += ", ";
				}
				Files += Component->Script->File;
			}
		}
	}

	std::size_t MeshEntities = 0;
	for (FEntity E : World.GetAllWithComponent<FTransform>())
	{
		if (World.GetComponent<FMeshRenderComponent>(E) != nullptr)
		{
			++MeshEntities;
		}
	}

	std::size_t Cameras = 0;
	for (FEntity E : World.GetAllWithComponent<FCameraComponent>())
	{
		if (World.GetComponent<FTransform>(E) != nullptr && World.GetComponent<FCameraComponent>(E)->bActive)
		{
			++Cameras;
		}
	}

	const bool bRegistry = Registry != nullptr;
	const bool bCamera = Registry != nullptr && Registry->ReadLatestCamera() != nullptr;

	if (Scripts == ReportedScripts && MeshEntities == ReportedMeshEntities && Cameras == ReportedCameras
		&& Files == ReportedScriptFiles && bRegistry == bReportedRegistry && bCamera == bReportedCamera
		&& bLuaBindingsInstalled == bReportedBindings)
	{
		return;   // 状态没变：不打（发布计数每帧都在涨，它不进签名，只作为当行的读数）
	}

	ReportedScripts = Scripts;
	ReportedMeshEntities = MeshEntities;
	ReportedCameras = Cameras;
	ReportedScriptFiles = Files;
	bReportedRegistry = bRegistry;
	bReportedCamera = bCamera;
	bReportedBindings = bLuaBindingsInstalled;

	MAHO_LOG_CORE_INFO(
		"MeshRenderSystem: state change -- scripts={} ({}), lua-bindings={}, mesh-entities={}, materials={}/params={}, "
		"cameras={}, mirror-registry={}, publishes={}, camera-published={}",
		Scripts, Files.empty() ? "<none>" : Files, bLuaBindingsInstalled ? "installed" : "MISSING",
		MeshEntities, PublishedMaterials, PublishedParams, Cameras, bRegistry ? "present" : "MISSING",
		Registry != nullptr ? Registry->GetPublishCount() : 0, bCamera ? "yes" : "no");
}

void FMeshRenderSystem::PublishScene(FGameWorld& World)
{
	Maho::FPrimitiveRegistry* Registry = GetPrimitiveRegistry();
	if (Registry == nullptr)
	{
		return;   // 镜像表还没发布（渲染层未安装）—— 没有东西可发，也不是错误
	}

	// 三段一起建、一次 swap（FRenderScene）。材质表是**去重**的：`MaterialIndexByKey` 把
	// "shader + 参数内容" 映射到材质表下标，于是多个 actor 共享一条材质、一份参数 ——
	// 那正是"改一次材质全体生效"的语义来源（design D5）。
	FRenderScene Scene;
	std::map<std::string, std::uint32_t> MaterialIndexByKey;

	// 遍历 (变换 + 网格) 的 entity：谁有这两样，谁就是"要画的东西"。
	for (FEntity E : World.GetAllWithComponent<FTransform>())
	{
		FTransform* Transform = World.GetComponent<FTransform>(E);
		FMeshRenderComponent* MeshRender = World.GetComponent<FMeshRenderComponent>(E);
		if (Transform == nullptr || MeshRender == nullptr)
		{
			continue;
		}
		if (!MeshRender->bVisible)
		{
			continue;   // 不可见 = 不发布（渲染侧因此连 actor 都不会建）
		}
		if (MeshRender->Mesh.IsNone())
		{
			if (!bWarnedNoMeshName)
			{
				bWarnedNoMeshName = true;
				MAHO_LOG_CORE_ERROR("MeshRenderSystem: entity {} has a mesh component with no mesh name; skipped", FindLuaId(E));
			}
			continue;
		}

		// 材质：没有组件就是"默认材质"（名字是游戏侧的决定，见 kDefaultMaterialShader）。
		// 空组件是合法的 —— 拿到的就是 shader 文件里的默认参数值。
		static const FMaterialComponent EmptyMaterial;
		const FMaterialComponent* Material = World.GetComponent<FMaterialComponent>(E);
		if (Material == nullptr)
		{
			Material = &EmptyMaterial;
		}
		const Name::FName ShaderName = Material->Shader.IsNone()
			? Name::FName(kDefaultMaterialShader)
			: Material->Shader;

		FRenderPrimitive Primitive;
		Primitive.Mesh = MeshRender->Mesh;
		Primitive.Material = ShaderName;   // 诊断用（材质名目前就是 shader 名）
		// ActorId 用与脚本同一个整数 id：一个 entity 一个稳定 id，渲染侧按它增删 actor
		// （"这个 actor 还在不在快照里"就是它的身份）。
		Primitive.ActorId = AssignLuaId(E);
		ComposeTransform(*Transform, Primitive.ObjectMatrix);

		// 去重：同 shader + 同参数内容的 entity 落在同一条材质表项上。
		const std::string Key = MakeMaterialKey(ShaderName, *Material);
		std::uint32_t MaterialIndex = 0;
		if (const auto It = MaterialIndexByKey.find(Key); It != MaterialIndexByKey.end())
		{
			MaterialIndex = It->second;
		}
		else
		{
			MaterialIndex = static_cast<std::uint32_t>(Scene.Materials.size());
			MaterialIndexByKey.emplace(Key, MaterialIndex);

			FRenderMaterial Entry;
			Entry.Shader = ShaderName;
			Entry.Name = ShaderName;
			Entry.FirstParam = static_cast<std::uint32_t>(Scene.Params.size());
			Entry.ParamCount = static_cast<std::uint32_t>(Material->Params.size());
			Scene.Materials.push_back(Entry);

			for (const FMaterialParam& P : Material->Params)
			{
				FRenderMaterialParam Out;
				Out.Name = P.Name;
				for (int Lane = 0; Lane < 4; ++Lane)
				{
					Out.Value[Lane] = P.Value[Lane];
				}
				Out.Texture = P.Texture;
				Scene.Params.push_back(Out);
			}
		}
		Primitive.MaterialIndex = MaterialIndex;
		Scene.Primitives.push_back(Primitive);
	}

	const std::size_t PrimitiveCount = Scene.Primitives.size();
	const std::size_t MaterialCount = Scene.Materials.size();
	const std::size_t ParamCount = Scene.Params.size();
	PublishedMaterials = MaterialCount;
	PublishedParams = ParamCount;
	Registry->Publish(std::move(Scene));

	++PublishCount;
	// 证据行：首帧 + 之后每 600 帧（约 10 秒@60Hz）一次，卸载时由 PreUnInstall 报最终值。
	// 三个数一起报：actor 数、材质数（去重后）、参数条数 —— 只报 actor 数看不出"共享材质"这件事。
	if (PublishCount == 1 || PublishCount % 600 == 0)
	{
		MAHO_LOG_CORE_INFO("MeshRenderSystem: published {} primitive(s), {} material(s), {} parameter(s) to the mirror table (publish #{})",
			PrimitiveCount, MaterialCount, ParamCount, Registry->GetPublishCount());
	}
}

void FMeshRenderSystem::PublishCamera(FGameWorld& World)
{
	Maho::FPrimitiveRegistry* Registry = GetPrimitiveRegistry();
	if (Registry == nullptr)
	{
		return;
	}

	for (FEntity E : World.GetAllWithComponent<FCameraComponent>())
	{
		FCameraComponent* Camera = World.GetComponent<FCameraComponent>(E);
		FTransform* Transform = World.GetComponent<FTransform>(E);
		if (Camera == nullptr || Transform == nullptr || !Camera->bActive)
		{
			continue;
		}

		// 发出去的是 **view + 视锥参数**，不是 view-projection：投影的宽高比属于**绘制目标**，
		// 而目标是渲染侧的东西（游戏侧的窗口尺寸和这一帧真正画进去的目标不是一回事 —— 用错那个就在
		// 某个轴上把画面压扁）。渲染侧拿这三个数 + 目标的真实尺寸自己组 P（见 FRenderCamera）。
		FRenderCamera Out;
		MakeViewFromCamera(*Transform, Out.View);
		Out.FovY = Camera->FovY;
		Out.Near = Camera->Near;
		Out.Far = Camera->Far;
		Out.Eye[0] = Transform->X;
		Out.Eye[1] = Transform->Y;
		Out.Eye[2] = Transform->Z;
		Registry->PublishCamera(Out);

		// 证据行：首帧 + 之后每 600 帧（与 primitive 那行同一节奏）。相机位姿是**脚本每帧写回**的，
		// 所以这行既证明"相机在发布"，也证明"位姿真的在变"—— eye 一直在动就是 gameplay 在开相机，
		// 而不是渲染侧写死了某个视角。只打首帧的话，"相机没动"与"相机在动但没记录"无法区分。
		if (CameraPublishCount == 0 || CameraPublishCount % 600 == 0)
		{
			MAHO_LOG_CORE_INFO("MeshRenderSystem: camera published (entity {}, eye {:.2f}/{:.2f}/{:.2f}, fov {:.1f} deg)",
				FindLuaId(E), Transform->X, Transform->Y, Transform->Z, Camera->FovY * 57.29578f);
		}
		++CameraPublishCount;
		return;   // 一帧一个相机：第一个激活的相机 witness 这一帧的视角
	}

	// 没有激活的相机：不发布（旧的快照因此过期），渲染侧会据此上报"没有相机"。
	if (CameraPublishCount != 0 && !bWarnedNoCamera)
	{
		bWarnedNoCamera = true;
		MAHO_LOG_CORE_WARN("MeshRenderSystem: no active camera entity this frame -- nothing published");
	}
}

void FMeshRenderSystem::PreUnInstall(FGameWorld&, FGameWorldContext&)
{
	MAHO_TRACE_STAGE(IPreUnInstall, "Mesh render system teardown", "clear the mirror channels so nothing lingers");

	// 清空两条通道：卸载后不让旧快照继续被画（残影）。相机发布成**全零的 FRenderCamera** = 这一帧没有
	// 可看的东西（全零的 fov/near/far 组不出投影，渲染侧明确当"没有相机"处理，不会去算 tan(0)）；
	// 注册表自己的 Shutdown 随后会把槽位置回 null。primitive 通道发布**空的三段**（材质 / 参数
	// 也一起清），否则渲染侧会留着上一帧的 actor + 材质表。
	if (Maho::FPrimitiveRegistry* Registry = GetPrimitiveRegistry())
	{
		Registry->Publish(FRenderScene{});
		Registry->PublishCamera(FRenderCamera{});
		MAHO_LOG_CORE_INFO("MeshRenderSystem: mirror cleared at uninstall ({} primitive publishes this run)",
			Registry->GetPublishCount());
	}

	LuaIdToEntity.clear();
	EntityToLuaId.clear();
	NextLuaId = 1;
	CubeEntity = FEntity{};
	CameraEntity = FEntity{};
	GMeshRenderSystem = nullptr;
}

void FMeshRenderSystem::LuaBind(sol::state& Lua)
{
	// 自由的 maho.* 函数：全部以整数 entity id 为第一参数（Call 只能带 0/1 个 float，
	// 所以 entity 由脚本自己持有 —— 见 AttachScript 注入的 `self.entity`）。
	sol::table Maho = Lua.create_named_table("maho");

	Maho.set_function("set_position", [](std::uint32_t Id, float X, float Y, float Z)
	{
		if (GMeshRenderSystem == nullptr)
		{
			return;
		}
		FGameWorld* World = GetGameWorld();
		FTransform* Transform = World != nullptr ? World->GetComponent<FTransform>(GMeshRenderSystem->FindEntity(Id)) : nullptr;
		if (Transform == nullptr)
		{
			return;   // 未登记的 id / 已销毁的 entity：脚本的错，但不动摇整帧
		}
		Transform->X = X;
		Transform->Y = Y;
		Transform->Z = Z;
	});

	Maho.set_function("get_position", [](std::uint32_t Id)
	{
		if (GMeshRenderSystem == nullptr)
		{
			return std::make_tuple(0.0f, 0.0f, 0.0f);
		}
		FGameWorld* World = GetGameWorld();
		FTransform* Transform = World != nullptr ? World->GetComponent<FTransform>(GMeshRenderSystem->FindEntity(Id)) : nullptr;
		return Transform != nullptr ? std::make_tuple(Transform->X, Transform->Y, Transform->Z)
		                            : std::make_tuple(0.0f, 0.0f, 0.0f);
	});

	Maho.set_function("set_rotation", [](std::uint32_t Id, float RX, float RY, float RZ)
	{
		if (GMeshRenderSystem == nullptr)
		{
			return;
		}
		FGameWorld* World = GetGameWorld();
		FTransform* Transform = World != nullptr ? World->GetComponent<FTransform>(GMeshRenderSystem->FindEntity(Id)) : nullptr;
		if (Transform == nullptr)
		{
			return;
		}
		Transform->RotationX = RX;
		Transform->RotationY = RY;
		Transform->RotationZ = RZ;
	});

	Maho.set_function("get_rotation", [](std::uint32_t Id)
	{
		if (GMeshRenderSystem == nullptr)
		{
			return std::make_tuple(0.0f, 0.0f, 0.0f);
		}
		FGameWorld* World = GetGameWorld();
		FTransform* Transform = World != nullptr ? World->GetComponent<FTransform>(GMeshRenderSystem->FindEntity(Id)) : nullptr;
		return Transform != nullptr ? std::make_tuple(Transform->RotationX, Transform->RotationY, Transform->RotationZ)
		                            : std::make_tuple(0.0f, 0.0f, 0.0f);
	});

	Maho.set_function("get_input", [](std::uint32_t Id)
	{
		if (GMeshRenderSystem == nullptr)
		{
			return std::make_tuple(0.0f, 0.0f, 0.0f, 0.0f);
		}
		FGameWorld* World = GetGameWorld();
		FInputComponent* Input = World != nullptr ? World->GetComponent<FInputComponent>(GMeshRenderSystem->FindEntity(Id)) : nullptr;
		return Input != nullptr
			? std::make_tuple(Input->MoveX, Input->MoveZ, Input->LookX, Input->LookY)
			: std::make_tuple(0.0f, 0.0f, 0.0f, 0.0f);
	});

	// -- 材质参数（gameplay 数据）--------------------------------------------------------------
	// 脚本改的是(entity 的 FMaterialComponent)这条**覆盖**，下一帧随快照发布 —— 不重编、不重启，
	// 画面就变（这就是"材质参数走镜像"的那条证据）。参数名大小写不敏感（`_basecolor` == `_BaseColor`）。
	Maho.set_function("set_material_color", [](std::uint32_t Id, std::string Name, float R, float G, float B, float A)
	{
		if (GMeshRenderSystem == nullptr)
		{
			return;
		}
		FGameWorld* World = GetGameWorld();
		const FEntity Entity = GMeshRenderSystem->FindEntity(Id);
		if (World == nullptr || World->GetComponent<FTransform>(Entity) == nullptr)
		{
			return;   // 未登记的 id / 已销毁的 entity：脚本的错，但不动摇整帧
		}
		if (World->GetComponent<FMaterialComponent>(Entity) == nullptr)
		{
			World->AddComponent<FMaterialComponent>(Entity, FMaterialComponent{});
		}
		FMaterialComponent* Material = World->GetComponent<FMaterialComponent>(Entity);
		if (Material == nullptr)
		{
			return;
		}
		FMaterialParam& Param = EnsureParam(*Material, Name::FName(Name));
		Param.Value[0] = R;
		Param.Value[1] = G;
		Param.Value[2] = B;
		Param.Value[3] = A;
	});

	Maho.set_function("set_material_texture", [](std::uint32_t Id, std::string Name, std::string TextureName)
	{
		if (GMeshRenderSystem == nullptr)
		{
			return;
		}
		FGameWorld* World = GetGameWorld();
		const FEntity Entity = GMeshRenderSystem->FindEntity(Id);
		if (World == nullptr || World->GetComponent<FTransform>(Entity) == nullptr)
		{
			return;
		}
		if (World->GetComponent<FMaterialComponent>(Entity) == nullptr)
		{
			World->AddComponent<FMaterialComponent>(Entity, FMaterialComponent{});
		}
		FMaterialComponent* Material = World->GetComponent<FMaterialComponent>(Entity);
		if (Material == nullptr)
		{
			return;
		}
		EnsureParam(*Material, Name::FName(Name)).Texture = Name::FName(TextureName);
	});

	Maho.set_function("get_material_color", [](std::uint32_t Id, std::string Name)
	{
		if (GMeshRenderSystem == nullptr)
		{
			return std::make_tuple(0.0f, 0.0f, 0.0f, 0.0f);
		}
		FGameWorld* World = GetGameWorld();
		FMaterialComponent* Material = World != nullptr
			? World->GetComponent<FMaterialComponent>(GMeshRenderSystem->FindEntity(Id))
			: nullptr;
		if (Material == nullptr)
		{
			return std::make_tuple(0.0f, 0.0f, 0.0f, 0.0f);
		}
		const FMaterialParam* Param = FindParam(*Material, Name);
		return Param != nullptr
			? std::make_tuple(Param->Value[0], Param->Value[1], Param->Value[2], Param->Value[3])
			: std::make_tuple(0.0f, 0.0f, 0.0f, 0.0f);
	});

	MAHO_LOG_CORE_INFO("MeshRenderSystem: Lua bindings registered (maho.set_position/get_position/set_rotation/get_rotation/get_input/"
		"set_material_color/set_material_texture/get_material_color)");
}

} // namespace GameWorld
} // namespace Maho

// The C export the host looks up BY SYMBOL NAME for dynamic install.
extern "C" MAHO_MESHRENDERSYSTEM_API Maho::FFrameExtension* CreateFrame()
{
	return Maho::GameWorld::FMeshRenderSystem::CreateFrame();
}
