#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Maho
{

enum class EAgentChatRole : std::uint8_t
{
	System = 0,
	User,
	Assistant
};

struct FAgentChatBubble
{
	EAgentChatRole Role = EAgentChatRole::System;
	std::string Text;
};

struct FAgentChatStartOptions
{
	/** Working directory exposed to the Cursor local agent (usually the game project root). */
	std::string ProjectCwd;

	/** Directory containing server.mjs + node_modules (engine Tools/AgentBridge). */
	std::string BridgeDirectory;

	/** Cursor User API key (from DefaultEditor.ini / env). Passed to the bridge process. */
	std::string ApiKey;

	/** Local HTTP port for the bridge. */
	int Port = 8765;

	/** Spawn `node server.mjs` if /health is not already up. */
	bool bSpawnBridge = true;
};

/**
 * Editor-side client for the Maho Agent bridge (Cursor SDK sidecar).
 * UI thread calls Tick / Drain / Send; HTTP + process live on a worker thread.
 */
class FAgentChatClient
{
public:
	FAgentChatClient();
	~FAgentChatClient();

	FAgentChatClient(const FAgentChatClient&) = delete;
	FAgentChatClient& operator=(const FAgentChatClient&) = delete;

	void Start(const FAgentChatStartOptions& Options);
	void Stop();

	/** Pump worker results into Drain queue; call once per frame from the editor. */
	void Tick();

	void SendUserMessage(std::string Text);

	/** Move newly arrived remote bubbles (assistant/system) into Out. */
	void DrainRemoteBubbles(std::vector<FAgentChatBubble>& Out);

	[[nodiscard]] bool IsStarted() const;
	[[nodiscard]] bool IsConnected() const;
	[[nodiscard]] bool IsBusy() const;
	[[nodiscard]] bool IsMockMode() const;
	[[nodiscard]] std::string GetStatusText() const;

private:
	struct FImpl;
	std::unique_ptr<FImpl> Impl;
};

} // namespace Maho
