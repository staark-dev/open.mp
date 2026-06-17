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
#include <unordered_map>
#include <fstream>
#include <string>

using namespace Impl;

static const UID NUIComponent_UID = UID(0x4e554900deadbeef);

struct INUIComponent : public IComponent
{
	PROVIDE_UID(NUIComponent_UID)
	virtual void createResource(StringView name, StringView path) = 0;
	virtual bool sendMessage(IPlayer& player, StringView resource, StringView json) = 0;
};

class NUIComponent final : public INUIComponent, public PawnEventHandler
{
public:
	static NUIComponent* s_instance;

	ICore* core_ = nullptr;
	IPawnComponent* pawn_ = nullptr;

	std::unordered_map<std::string, std::string> resources_;
	std::mutex resourcesMutex_;

	httplib::Server httpServer_;
	std::thread httpThread_;
	std::atomic<bool> running_ { false };

	// Raw AMX natives — registered directly with each script
	static cell AMX_NATIVE_CALL n_NUI_CreateResource(AMX* amx, const cell* params);
	static cell AMX_NATIVE_CALL n_NUI_SendMessage(AMX* amx, const cell* params);

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

	// PawnEventHandler — called for each AMX script that loads
	void onAmxLoad(IPawnScript& script) override;
	void onAmxUnload(IPawnScript& script) override { }
};
