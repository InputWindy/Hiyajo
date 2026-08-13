#pragma once

#include <Core/Extension/World/ECS/ComponentType.h>
#include <Core/Extension/World/ECS/EntityHandle.h>
#include <Core/Extension/World/ECS/Chunk.h>
#include <Core/Extension/World/ECS/Archetype.h>
#include <Core/Extension/World/ECS/EntityManager.h>
#include <Core/Extension/World/ECS/Query.h>
#include <Core/Extension/World/ECS/System.h>
#include <Core/Extension/World/ECS/SystemGroup.h>
#include <Core/Extension/World/ECS/EntityCommandBuffer.h>
#include <Core/Extension/World/ECS/World.h>

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
