#pragma once

#include <Core/ECS/ComponentType.h>
#include <Core/ECS/EntityHandle.h>
#include <Core/ECS/Chunk.h>
#include <Core/ECS/Archetype.h>
#include <Core/ECS/EntityManager.h>
#include <Core/ECS/Query.h>
#include <Core/ECS/System.h>
#include <Core/ECS/SystemGroup.h>
#include <Core/ECS/EntityCommandBuffer.h>
#include <Core/ECS/World.h>

/**
 * Convenience macro to register a data component type.
 * Must be called at global/namespace scope.
 */
#define REGISTER_COMPONENT(T) \
	namespace { \
		[[maybe_unused]] static Maho::FComponentTypeId __reg_##T = Maho::GetComponentTypeId<T>(); \
	}

/**
 * Convenience macro to register a tag component type.
 * Identical to REGISTER_COMPONENT for tags (size 0),
 * but semantically distinct.
 */
#define REGISTER_TAG(T) \
	namespace { \
		static_assert(sizeof(T) == 0, #T " must be a tag component (sizeof == 0)"); \
		[[maybe_unused]] static Maho::FComponentTypeId __reg_tag_##T = Maho::GetComponentTypeId<T>(); \
	}
