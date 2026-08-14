#pragma once

#include <ECS/ComponentType.h>
#include <ECS/EntityHandle.h>
#include <ECS/Chunk.h>
#include <ECS/Archetype.h>
#include <ECS/EntityManager.h>
#include <ECS/Query.h>
#include <ECS/System.h>
#include <ECS/SystemGroup.h>
#include <ECS/EntityCommandBuffer.h>
#include <ECS/World.h>

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
