#include <Core/Extension/World/ECS/EntityCommandBuffer.h>
#include <Core/Extension/World/ECS/EntityManager.h>

namespace Maho
{

FEntityCommandBuffer::~FEntityCommandBuffer()
{
	for (auto& Cmd : Commands)
	{
		if (Cmd.Data)
		{
			::operator delete(Cmd.Data);
		}
	}
}

FEntityHandle FEntityCommandBuffer::CreateEntity()
{
	FCommand Cmd;
	Cmd.Handle = FEntityHandle{};
	Cmd.Type = ECommandType::CreateEntity;
	Commands.push_back(Cmd);

	// Placeholder — real handle resolved during Playback.
	return FEntityHandle{};
}

void FEntityCommandBuffer::DestroyEntity(FEntityHandle Handle)
{
	FCommand Cmd;
	Cmd.Handle = Handle;
	Cmd.Type = ECommandType::DestroyEntity;
	Commands.push_back(Cmd);
}

void FEntityCommandBuffer::Playback(FEntityManager& Manager)
{
	for (auto& Cmd : Commands)
	{
		switch (Cmd.Type)
		{
		case ECommandType::CreateEntity:
		{
			// Store real handle into the command so caller can retrieve it after Playback.
			Cmd.Handle = Manager.CreateEntity();
		}
		break;

		case ECommandType::DestroyEntity:
		{
			Manager.DestroyEntity(Cmd.Handle);
		}
		break;

		case ECommandType::SetComponent:
		{
			if (Manager.IsValid(Cmd.Handle) && Cmd.Data != nullptr)
			{
				Manager.SetComponentTypeErased(Cmd.Handle, Cmd.TypeId, Cmd.Data, Cmd.DataSize);
			}
		}
		break;

		case ECommandType::AddComponent:
		{
			if (Manager.IsValid(Cmd.Handle) && Cmd.Data != nullptr)
			{
				Manager.AddComponentTypeErased(Cmd.Handle, Cmd.TypeId, Cmd.Data, Cmd.DataSize);
			}
		}
		break;

		case ECommandType::RemoveComponent:
		{
			if (Manager.IsValid(Cmd.Handle))
			{
				Manager.RemoveComponentTypeErased(Cmd.Handle, Cmd.TypeId);
			}
		}
		break;

		case ECommandType::AddTag:
		{
			if (Manager.IsValid(Cmd.Handle))
			{
				Manager.AddTagTypeErased(Cmd.Handle, Cmd.TypeId);
			}
		}
		break;

		case ECommandType::RemoveTag:
		{
			if (Manager.IsValid(Cmd.Handle))
			{
				Manager.RemoveTagTypeErased(Cmd.Handle, Cmd.TypeId);
			}
		}
		break;
		}
	}

	Clear();
}

void FEntityCommandBuffer::Clear()
{
	for (auto& Cmd : Commands)
	{
		if (Cmd.Data)
		{
			::operator delete(Cmd.Data);
		}
	}
	Commands.clear();
}

} // namespace Maho
