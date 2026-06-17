/*
 *  This Source Code Form is subject to the terms of the Mozilla Public License,
 *  v. 2.0. If a copy of the MPL was not distributed with this file, You can
 *  obtain one at http://mozilla.org/MPL/2.0/.
 *
 *  The original code is copyright (c) 2022, open.mp team and contributors.
 */

#pragma once

#include <sdk.hpp>
#include <Server/Components/Pawn/pawn.hpp>
#include <httplib.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <algorithm>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <string>
#include <random>

using namespace Impl;

static const UID NUIComponent_UID = UID(0x4e554900deadbeef);

struct INUIComponent : public IComponent
{
	PROVIDE_UID(NUIComponent_UID)
	virtual void createResource(StringView name, StringView path) = 0;
	virtual bool sendMessage(IPlayer& player, StringView resource, StringView json) = 0;
	virtual bool showNUI(IPlayer& player, StringView resource) = 0;
	virtual bool hideNUI(IPlayer& player, StringView resource) = 0;
};

class NUIComponent final
	: public INUIComponent
	, public PawnEventHandler
	, public CoreEventHandler
	, public PlayerConnectEventHandler
{
public:
	static NUIComponent* s_instance;

	ICore* core_ = nullptr;
	IPawnComponent* pawn_ = nullptr;

	// Registered NUI resources: name → filesystem path
	std::unordered_map<std::string, std::string> resources_;
	std::mutex resourcesMutex_;

	// HTTP server
	httplib::Server httpServer_;
	std::thread httpThread_;
	std::atomic<bool> running_ { false };

	// Loaded Pawn scripts (for firing OnNUIMessage)
	std::vector<IPawnScript*> scripts_;
	std::mutex scriptsMutex_;

	// Player auth tokens for POST callbacks
	std::unordered_map<int, std::string> playerTokens_;  // playerid → token
	std::unordered_map<std::string, int> tokenPlayers_;  // token → playerid
	std::mutex tokensMutex_;

	// Cross-thread callback queue: HTTP thread pushes, game thread drains on tick
	struct PendingCallback
	{
		int playerid;
		std::string resource;
		std::string data;
	};
	std::queue<PendingCallback> pendingCallbacks_;
	std::mutex callbackMutex_;

	// Token helpers
	std::string generateToken();
	std::string getOrCreateToken(int playerid);

	// Fire OnNUIMessage into all loaded scripts (game thread only)
	void fireOnNUIMessage(int playerid, const std::string& resource, const std::string& data);

	// AMX natives
	static cell AMX_NATIVE_CALL n_NUI_CreateResource(AMX* amx, const cell* params);
	static cell AMX_NATIVE_CALL n_NUI_SendMessage(AMX* amx, const cell* params);
	static cell AMX_NATIVE_CALL n_NUI_Show(AMX* amx, const cell* params);
	static cell AMX_NATIVE_CALL n_NUI_Hide(AMX* amx, const cell* params);

	// IComponent
	StringView componentName() const override { return "NUI"; }
	SemanticVersion componentVersion() const override { return SemanticVersion(1, 0, 0, 0); }
	void onLoad(ICore* core) override;
	void onInit(IComponentList* components) override;
	void onFree(IComponent* component) override;
	void reset() override { }
	void free() override;

	// INUIComponent
	void createResource(StringView name, StringView path) override;
	bool sendMessage(IPlayer& player, StringView resource, StringView json) override;
	bool showNUI(IPlayer& player, StringView resource) override;
	bool hideNUI(IPlayer& player, StringView resource) override;

	// PawnEventHandler
	void onAmxLoad(IPawnScript& script) override;
	void onAmxUnload(IPawnScript& script) override;

	// CoreEventHandler — drains callback queue on every server tick
	void onTick(Microseconds elapsed, TimePoint now) override;

	// PlayerConnectEventHandler — token cleanup on disconnect
	void onPlayerConnect(IPlayer& player) override { }
	void onPlayerDisconnect(IPlayer& player, PeerDisconnectReason reason) override;
};
