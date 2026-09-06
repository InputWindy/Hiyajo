#pragma once

#include "UISystemApi.h"
#include <GameWorld.h>
#include "FUIBuilder.h"
#include <Core/Delegate.h>

#include <functional>
#include <mutex>
#include <vector>

namespace Maho
{
namespace GameWorld
{

/**
 * ECS-side UI widget model. Pure game data -- the render layer (FUIFeature/ImGui)
 * consumes this upstream; this system only owns and animates it.
 */
struct MAHO_UISYSTEM_API FUIWidget
{
	float X = 0.f;          // screen-space position (left)
	float Y = 0.f;          // screen-space position (top)
	float Width = 100.f;
	float Height = 50.f;
	bool  bVisible = true;
};

/**
 * GameWorld UI system -- a WORLD SYSTEM, installed into FGameWorld as a peer
 * layer (via the sub-landlord collector). It spawns a few widget entities on
 * install, animates them every update (demonstrating component read/write
 * through the world accessor), and tears them down on uninstall.
 *
 * It also owns the FUIBuilder -- the game->render UI command broker. Game-side UI
 * components (this system included) Submit draw CLOSURES to it; FUIFeature pulls them
 * once per render frame and executes the closures between ImGui::NewFrame and
 * ImGui::Render. So the game defines what to draw (the closures call ImGui::*), while
 * the render worker executes every ImGui call on its own frame. The game never touches
 * FRender; the render feature only owns the ImGui context + the FName->RDG mirror.
 */
class MAHO_UISYSTEM_API FUISystem : public FLayer<IOnInstalled, IProcessInput, IUpdate, IPreUnInstall>
{
	MAHO_DECLARE_LAYER(FUISystem, "UISystem.dll");

public:
	void OnInstalled(FGameWorld& World) override;
	void ProcessInput(FGameWorld& World) override;
	void Update(FGameWorld& World) override;
	void PreUnInstall(FGameWorld& World) override;

	/** Subscribe a handler to the "UI built this frame" broadcast. Thread-safe: the
	 *  subscriber (the render feature) may join from a render worker while the game
	 *  thread broadcasts. The handler runs ON THE BROADCAST THREAD (the game thread,
	 *  after Submit finished building the frame), RECEIVING the UIBuilder by reference --
	 *  so it can CopyFrame() directly, no GetUISystem() re-lookup (and no null-window
	 *  during teardown). Returns a subscription id; UnsubscribeUIHandler(id) removes ONLY
	 *  this handler. Callers MUST retain it until they unload. */
	uint64_t SubscribeUIHandler(std::function<void(const FUIBuilder&)> Handler);

	/** Remove ONLY the handler registered under this subscription id (mirror of
	 *  SubscribeUIHandler). No-op if the id is stale. */
	void UnsubscribeUIHandler(uint64_t Subscription);

private:

	FUIBuilder UIBuilder;   // game->render UI command broker (owned by this world system)
	std::vector<FEntity> Widgets;   // spawned widget entities
	float Time = 0.f;

	mutable std::mutex EventMutex;   // guards OnUIBuilt (bind vs broadcast race)
	TMulticastEvent<void(const FUIBuilder&)> OnUIBuilt;
};

/** Global accessor to the UI system (cross-DLL, mirrors Resource::GetResourceSystem()).
 *  nullptr until the system is installed by FGameWorld. */
MAHO_UISYSTEM_API FUISystem* GetUISystem();

} // namespace GameWorld
} // namespace Maho
