#pragma once

#include <Core/Misc/Export.h>
#include <Core/Extension/World/ECS/ComponentType.h>
#include <Core/Extension/World/ECS/EntityHandle.h>
#include <Core/Extension/World/ECS/System.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <type_traits>
#include <variant>
#include <vector>

namespace Maho
{

class FEntityManager;

/**
 * Thread-safe deferred command recorder.
 *
 * Each system/job gets its own ECB instance. Commands are recorded
 * during ExecuteStage(EEngineStage::Update) and played back serially on the
 * main thread via Playback().
 *
 * Automatic ECB Systems:
 *   FBeginSimECBSystem  — plays back the "begin" ECB before the simulation group
 *   FEndSimECBSystem    — plays back the "end" ECB after the simulation group
 */
class MAHO_API FEntityCommandBuffer
{
public:
	FEntityCommandBuffer() = default;
	~FEntityCommandBuffer();

	FEntityCommandBuffer(const FEntityCommandBuffer&) = delete;
	FEntityCommandBuffer& operator=(const FEntityCommandBuffer&) = delete;
	FEntityCommandBuffer(FEntityCommandBuffer&&) = default;
	FEntityCommandBuffer& operator=(FEntityCommandBuffer&&) = default;

	// ─── Recording API ───────────────────────────────────────────

	/** Create an entity. Entity is created during Playback. */
	FEntityHandle CreateEntity();

	/** Destroy an entity during Playback. */
	void DestroyEntity(FEntityHandle Handle);

	/** Set component value on entity during Playback (add + set). */
	template <typename T>
	void SetComponent(FEntityHandle Handle, const T& Value)
	{
		static_assert(std::is_trivially_copyable_v<T>, "ECS component must be trivially copyable");
		static_assert(!IsTagComponent<T>, "SetComponent: use AddTag/RemoveTag for tag components");

		void* Data = ::operator new(sizeof(T));
		std::memcpy(Data, &Value, sizeof(T));

		FCommand Cmd;
		Cmd.Handle = Handle;
		Cmd.Type = ECommandType::SetComponent;
		Cmd.TypeId = GetComponentTypeId<T>();
		Cmd.DataSize = sizeof(T);
		Cmd.Data = Data;
		Commands.push_back(Cmd);
	}

	/** Add a component with initial value during Playback. */
	template <typename T>
	void AddComponent(FEntityHandle Handle, const T& Value)
	{
		static_assert(std::is_trivially_copyable_v<T>, "ECS component must be trivially copyable");
		static_assert(!IsTagComponent<T>, "AddComponent: use AddTag for tag components");

		void* Data = ::operator new(sizeof(T));
		std::memcpy(Data, &Value, sizeof(T));

		FCommand Cmd;
		Cmd.Handle = Handle;
		Cmd.Type = ECommandType::AddComponent;
		Cmd.TypeId = GetComponentTypeId<T>();
		Cmd.DataSize = sizeof(T);
		Cmd.Data = Data;
		Commands.push_back(Cmd);
	}

	/** Remove a component during Playback. */
	template <typename T>
	void RemoveComponent(FEntityHandle Handle)
	{
		static_assert(!IsTagComponent<T>, "RemoveComponent: use RemoveTag for tag components");

		FCommand Cmd;
		Cmd.Handle = Handle;
		Cmd.Type = ECommandType::RemoveComponent;
		Cmd.TypeId = GetComponentTypeId<T>();
		Commands.push_back(Cmd);
	}

	/** Add a tag to entity during Playback. */
	template <typename T>
	void AddTag(FEntityHandle Handle)
	{
		static_assert(IsTagComponent<T>, "AddTag: T must be a tag component");

		FCommand Cmd;
		Cmd.Handle = Handle;
		Cmd.Type = ECommandType::AddTag;
		Cmd.TypeId = GetComponentTypeId<T>();
		Commands.push_back(Cmd);
	}

	/** Remove a tag from entity during Playback. */
	template <typename T>
	void RemoveTag(FEntityHandle Handle)
	{
		static_assert(IsTagComponent<T>, "RemoveTag: T must be a tag component");

		FCommand Cmd;
		Cmd.Handle = Handle;
		Cmd.Type = ECommandType::RemoveTag;
		Cmd.TypeId = GetComponentTypeId<T>();
		Commands.push_back(Cmd);
	}

	// ─── Playback ────────────────────────────────────────────────

	/** Execute all recorded commands against the given EntityManager. */
	void Playback(FEntityManager& Manager);

	/** Clear all recorded commands without executing them. */
	void Clear();

	[[nodiscard]] bool IsEmpty() const { return Commands.empty(); }
	[[nodiscard]] std::size_t GetCommandCount() const { return Commands.size(); }

private:
	enum class ECommandType : std::uint8_t
	{
		CreateEntity,
		DestroyEntity,
		SetComponent,
		AddComponent,
		RemoveComponent,
		AddTag,
		RemoveTag,
	};

	struct FCommand
	{
		FEntityHandle Handle;
		ECommandType Type = ECommandType::CreateEntity;
		FComponentTypeId TypeId = 0;
		std::size_t DataSize = 0;
		void* Data = nullptr;
	};

	std::vector<FCommand> Commands;
};

/**
 * Standard ECB system that plays back an ECB before/after a system group.
 *
 * ExecuteStage performs actual work only for EEngineStage::Update
 * (ECB playback); all other stages are no-ops.
 */
class MAHO_API FECBSystem : public ISystem
{
public:
	explicit FECBSystem(FEntityCommandBuffer& InECB, const char* InName);

	const char* GetName() const override { return Name.c_str(); }

	bool ExecuteStage(EEngineStage Stage, float DeltaTime, FWorld& World) override;

private:
	FEntityCommandBuffer& ECB;
	std::string Name;
};

} // namespace Maho
