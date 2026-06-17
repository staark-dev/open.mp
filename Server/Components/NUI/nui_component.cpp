/*
 *  This Source Code Form is subject to the terms of the Mozilla Public License,
 *  v. 2.0. If a copy of the MPL was not distributed with this file, You can
 *  obtain one at http://mozilla.org/MPL/2.0/.
 *
 *  The original code is copyright (c) 2022, open.mp team and contributors.
 */

#include "nui_component.hpp"
#include <nui.hpp>
#include <packet.hpp>

NUIComponent* NUIComponent::s_instance = nullptr;
static NUIComponent component;

static std::string mimeType(const std::string& path)
{
	auto dot = path.rfind('.');
	if (dot == std::string::npos)
		return "application/octet-stream";
	std::string ext = path.substr(dot + 1);
	if (ext == "html" || ext == "htm")
		return "text/html";
	if (ext == "css")
		return "text/css";
	if (ext == "js")
		return "application/javascript";
	if (ext == "json")
		return "application/json";
	if (ext == "png")
		return "image/png";
	if (ext == "jpg" || ext == "jpeg")
		return "image/jpeg";
	if (ext == "svg")
		return "image/svg+xml";
	return "application/octet-stream";
}

// ── HTTP server setup ─────────────────────────────────────────────────────────

void NUIComponent::onLoad(ICore* core)
{
	core_ = core;
	s_instance = this;

	httpServer_.Get("/ping", [](const httplib::Request&, httplib::Response& res) {
		res.set_header("Access-Control-Allow-Origin", "*");
		res.set_content("omp-nui ok", "text/plain");
	});

	httpServer_.Get(R"(/nui/([^/]+)/(.+))", [this](const httplib::Request& req, httplib::Response& res) {
		res.set_header("Access-Control-Allow-Origin", "*");

		std::string resourceName = req.matches[1].str();
		std::string filePath = req.matches[2].str();

		std::string basePath;
		{
			std::lock_guard<std::mutex> lock(resourcesMutex_);
			auto it = resources_.find(resourceName);
			if (it == resources_.end())
			{
				res.status = 404;
				res.set_content("Resource not registered", "text/plain");
				return;
			}
			basePath = it->second;
		}

		std::string fullPath = basePath + "/" + filePath;
		std::ifstream f(fullPath, std::ios::binary);
		if (!f)
		{
			res.status = 404;
			res.set_content("File not found", "text/plain");
			return;
		}

		std::string content((std::istreambuf_iterator<char>(f)), {});
		res.set_content(content, mimeType(filePath).c_str());
	});

	running_ = true;
	httpThread_ = std::thread([this]() {
		httpServer_.listen("0.0.0.0", 7778);
	});

	core_->printLn("[NUI] HTTP server started on port 7778");
}

// ── Component lifecycle ───────────────────────────────────────────────────────

void NUIComponent::onInit(IComponentList* components)
{
	pawn_ = components->queryComponent<IPawnComponent>();
	if (pawn_)
	{
		pawn_->getEventDispatcher().addEventHandler(this);
		core_->printLn("[NUI] Pawn integration active — NUI_CreateResource / NUI_SendMessage available");
	}
	else
	{
		core_->printLn("[NUI] WARNING: Pawn component not found — natives unavailable");
	}
}

void NUIComponent::onFree(IComponent* component)
{
	if (component == pawn_)
		pawn_ = nullptr;
}

void NUIComponent::free()
{
	if (running_)
	{
		running_ = false;
		httpServer_.stop();
		if (httpThread_.joinable())
			httpThread_.join();
	}
	if (pawn_)
		pawn_->getEventDispatcher().removeEventHandler(this);

	s_instance = nullptr;
	delete this;
}

// ── INUIComponent implementation ──────────────────────────────────────────────

void NUIComponent::createResource(StringView name, StringView path)
{
	std::lock_guard<std::mutex> lock(resourcesMutex_);
	resources_[std::string(name)] = std::string(path);
	core_->printLn("[NUI] Resource registered: %.*s -> %.*s",
		(int)name.size(), name.data(),
		(int)path.size(), path.data());
}

bool NUIComponent::sendMessage(IPlayer& player, StringView resource, StringView json)
{
	NetCode::RPC::NUIMessage msg;
	msg.Resource = resource;
	msg.JsonData = json;
	return PacketHelper::send(msg, player);
}

// ── PawnEventHandler ──────────────────────────────────────────────────────────

void NUIComponent::onAmxLoad(IPawnScript& script)
{
	static const AMX_NATIVE_INFO natives[] = {
		{ "NUI_CreateResource", NUIComponent::n_NUI_CreateResource },
		{ "NUI_SendMessage", NUIComponent::n_NUI_SendMessage },
		{ nullptr, nullptr }
	};
	script.Register(natives, -1);
}

// ── AMX Natives ───────────────────────────────────────────────────────────────

// native bool:NUI_CreateResource(const name[], const path[]);
cell AMX_NATIVE_CALL NUIComponent::n_NUI_CreateResource(AMX* amx, const cell* params)
{
	if (!s_instance || !s_instance->pawn_)
		return 0;
	if (params[0] < 2 * (cell)sizeof(cell))
		return 0;

	IPawnScript* script = s_instance->pawn_->getScript(amx);
	if (!script)
		return 0;

	cell* namePtr = nullptr;
	cell* pathPtr = nullptr;
	script->GetAddr(params[1], &namePtr);
	script->GetAddr(params[2], &pathPtr);

	char name[64] = {}, path[256] = {};
	script->GetString(name, namePtr, false, sizeof(name) - 1);
	script->GetString(path, pathPtr, false, sizeof(path) - 1);

	s_instance->createResource(StringView(name), StringView(path));
	return 1;
}

// native bool:NUI_SendMessage(playerid, const resource[], const json[]);
cell AMX_NATIVE_CALL NUIComponent::n_NUI_SendMessage(AMX* amx, const cell* params)
{
	if (!s_instance || !s_instance->pawn_)
		return 0;
	if (params[0] < 3 * (cell)sizeof(cell))
		return 0;

	IPawnScript* script = s_instance->pawn_->getScript(amx);
	if (!script)
		return 0;

	int playerid = (int)params[1];

	cell* resourcePtr = nullptr;
	cell* jsonPtr = nullptr;
	script->GetAddr(params[2], &resourcePtr);
	script->GetAddr(params[3], &jsonPtr);

	char resource[64] = {}, json[2048] = {};
	script->GetString(resource, resourcePtr, false, sizeof(resource) - 1);
	script->GetString(json, jsonPtr, false, sizeof(json) - 1);

	IPlayer* player = s_instance->core_->getPlayers().get(playerid);
	if (!player)
		return 0;

	return s_instance->sendMessage(*player, StringView(resource), StringView(json)) ? 1 : 0;
}

// ── Entry point ───────────────────────────────────────────────────────────────

COMPONENT_ENTRY_POINT()
{
	return &component;
}
