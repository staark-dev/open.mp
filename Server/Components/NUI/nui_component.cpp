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

// ── MIME type table ───────────────────────────────────────────────────────────

static std::string mimeType(const std::string& path)
{
	auto dot = path.rfind('.');
	if (dot == std::string::npos)
		return "application/octet-stream";
	std::string ext = path.substr(dot + 1);

	// Text / scripts
	if (ext == "html" || ext == "htm")  return "text/html; charset=utf-8";
	if (ext == "css")                   return "text/css; charset=utf-8";
	if (ext == "js"  || ext == "mjs")  return "application/javascript; charset=utf-8";
	if (ext == "json" || ext == "map") return "application/json; charset=utf-8";
	if (ext == "txt")                   return "text/plain; charset=utf-8";
	if (ext == "xml")                   return "application/xml";

	// Images
	if (ext == "png")                   return "image/png";
	if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
	if (ext == "gif")                   return "image/gif";
	if (ext == "webp")                  return "image/webp";
	if (ext == "ico")                   return "image/x-icon";
	if (ext == "svg")                   return "image/svg+xml";
	if (ext == "avif")                  return "image/avif";

	// Fonts — critical for icon libs (Font Awesome, Material Icons, etc.)
	if (ext == "woff")                  return "font/woff";
	if (ext == "woff2")                 return "font/woff2";
	if (ext == "ttf")                   return "font/ttf";
	if (ext == "otf")                   return "font/otf";
	if (ext == "eot")                   return "application/vnd.ms-fontobject";

	// Audio / video
	if (ext == "mp4")                   return "video/mp4";
	if (ext == "webm")                  return "video/webm";
	if (ext == "mp3")                   return "audio/mpeg";
	if (ext == "ogg")                   return "audio/ogg";
	if (ext == "wav")                   return "audio/wav";
	if (ext == "flac")                  return "audio/flac";

	// WebAssembly
	if (ext == "wasm")                  return "application/wasm";

	return "application/octet-stream";
}

// ── Token helpers ─────────────────────────────────────────────────────────────

std::string NUIComponent::generateToken()
{
	static thread_local std::mt19937 rng(std::random_device {}());
	static thread_local std::uniform_int_distribution<> dist(0, 15);
	static const char hex[] = "0123456789abcdef";
	std::string token(32, 0);
	for (char& c : token)
		c = hex[dist(rng)];
	return token;
}

std::string NUIComponent::getOrCreateToken(int playerid)
{
	std::lock_guard<std::mutex> lock(tokensMutex_);
	auto it = playerTokens_.find(playerid);
	if (it != playerTokens_.end())
		return it->second;
	std::string token = generateToken();
	playerTokens_[playerid] = token;
	tokenPlayers_[token] = playerid;
	return token;
}

// ── HTTP server setup ─────────────────────────────────────────────────────────

void NUIComponent::onLoad(ICore* core)
{
	core_ = core;
	s_instance = this;

	core_->getEventDispatcher().addEventHandler(this);
	core_->getPlayers().getPlayerConnectDispatcher().addEventHandler(this);

	// /ping — health check
	httpServer_.Get("/ping", [](const httplib::Request&, httplib::Response& res) {
		res.set_header("Access-Control-Allow-Origin", "*");
		res.set_content("omp-nui ok", "text/plain");
	});

	// CORS preflight for POST callbacks
	httpServer_.Options(".*", [](const httplib::Request&, httplib::Response& res) {
		res.set_header("Access-Control-Allow-Origin", "*");
		res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
		res.set_header("Access-Control-Allow-Headers", "Content-Type, X-NUI-Token");
		res.status = 204;
	});

	// POST /nui/callback/{resource} — client→server, queued for OnNUIMessage
	httpServer_.Post(R"(/nui/callback/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
		res.set_header("Access-Control-Allow-Origin", "*");

		std::string token = req.get_header_value("X-NUI-Token");
		int playerid = -1;
		{
			std::lock_guard<std::mutex> lock(tokensMutex_);
			auto it = tokenPlayers_.find(token);
			if (it != tokenPlayers_.end())
				playerid = it->second;
		}

		if (playerid < 0)
		{
			res.status = 401;
			res.set_content("Invalid token", "text/plain");
			return;
		}

		{
			std::lock_guard<std::mutex> lock(callbackMutex_);
			pendingCallbacks_.push({ playerid, req.matches[1].str(), req.body });
		}

		res.set_content("ok", "text/plain");
	});

	// GET /nui/{resource}/{path} — static files + SPA fallback + token injection
	httpServer_.Get(R"(/nui/([^/]+)(?:/(.*))?)", [this](const httplib::Request& req, httplib::Response& res) {
		res.set_header("Access-Control-Allow-Origin", "*");

		std::string resourceName = req.matches[1].str();
		std::string filePath = req.matches[2].str();
		if (filePath.empty())
			filePath = "index.html";

		if (filePath.find("..") != std::string::npos)
		{
			res.status = 400;
			res.set_content("Invalid path", "text/plain");
			return;
		}

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

		// Try exact file; fall back to index.html for SPA routes
		std::ifstream f(basePath + "/" + filePath, std::ios::binary);
		if (!f)
		{
			filePath = "index.html";
			f.open(basePath + "/index.html", std::ios::binary);
			if (!f)
			{
				res.status = 404;
				res.set_content("File not found", "text/plain");
				return;
			}
		}

		std::string content((std::istreambuf_iterator<char>(f)), {});
		std::string mime = mimeType(filePath);

		// Inject token into HTML head so window.__NUI_TOKEN__ is available to JS
		if (mime.rfind("text/html", 0) == 0)
		{
			std::string token = req.get_header_value("X-NUI-Token");
			if (!token.empty())
			{
				std::string inj = "<script>window.__NUI_TOKEN__='" + token + "';</script>";
				auto pos = content.find("</head>");
				if (pos != std::string::npos)
					content.insert(pos, inj);
				else
					content = inj + content;
			}
		}

		res.set_content(content, mime.c_str());
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
		core_->printLn("[NUI] Pawn integration active — NUI_CreateResource / NUI_SendMessage / NUI_Show / NUI_Hide / OnNUIMessage");
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
	if (core_)
	{
		core_->getEventDispatcher().removeEventHandler(this);
		core_->getPlayers().getPlayerConnectDispatcher().removeEventHandler(this);
	}
	s_instance = nullptr;
	delete this;
}

// ── PlayerConnectEventHandler ─────────────────────────────────────────────────

void NUIComponent::onPlayerDisconnect(IPlayer& player, PeerDisconnectReason reason)
{
	int playerid = player.getID();
	std::lock_guard<std::mutex> lock(tokensMutex_);
	auto it = playerTokens_.find(playerid);
	if (it != playerTokens_.end())
	{
		tokenPlayers_.erase(it->second);
		playerTokens_.erase(it);
	}
}

// ── CoreEventHandler — drain POST callback queue on game tick ─────────────────

void NUIComponent::onTick(Microseconds elapsed, TimePoint now)
{
	std::queue<PendingCallback> toProcess;
	{
		std::lock_guard<std::mutex> lock(callbackMutex_);
		toProcess.swap(pendingCallbacks_);
	}
	while (!toProcess.empty())
	{
		auto& cb = toProcess.front();
		fireOnNUIMessage(cb.playerid, cb.resource, cb.data);
		toProcess.pop();
	}
}

void NUIComponent::fireOnNUIMessage(int playerid, const std::string& resource, const std::string& data)
{
	std::lock_guard<std::mutex> lock(scriptsMutex_);
	for (auto* script : scripts_)
	{
		script->Call("OnNUIMessage", DefaultReturnValue_False,
			(cell)playerid,
			StringView(resource.c_str(), resource.size()),
			StringView(data.c_str(), data.size()));
	}
}

// ── INUIComponent implementation ──────────────────────────────────────────────

void NUIComponent::createResource(StringView name, StringView path)
{
	std::string pathStr(path);
	while (!pathStr.empty() && pathStr.back() == '/')
		pathStr.pop_back();

	std::lock_guard<std::mutex> lock(resourcesMutex_);
	resources_[std::string(name)] = pathStr;
	core_->printLn("[NUI] Resource registered: %.*s -> %s",
		(int)name.size(), name.data(), pathStr.c_str());
}

bool NUIComponent::sendMessage(IPlayer& player, StringView resource, StringView json)
{
	NetCode::RPC::NUIMessage msg;
	msg.Resource = resource;
	msg.JsonData = json;
	return PacketHelper::send(msg, player);
}

bool NUIComponent::showNUI(IPlayer& player, StringView resource)
{
	std::string token = getOrCreateToken(player.getID());
	NetCode::RPC::NUIShow msg;
	msg.Resource = resource;
	msg.Token = StringView(token.c_str(), token.size());
	return PacketHelper::send(msg, player);
}

bool NUIComponent::hideNUI(IPlayer& player, StringView resource)
{
	NetCode::RPC::NUIHide msg;
	msg.Resource = resource;
	return PacketHelper::send(msg, player);
}

// ── PawnEventHandler ──────────────────────────────────────────────────────────

void NUIComponent::onAmxLoad(IPawnScript& script)
{
	static const AMX_NATIVE_INFO natives[] = {
		{ "NUI_CreateResource", NUIComponent::n_NUI_CreateResource },
		{ "NUI_SendMessage",    NUIComponent::n_NUI_SendMessage },
		{ "NUI_Show",           NUIComponent::n_NUI_Show },
		{ "NUI_Hide",           NUIComponent::n_NUI_Hide },
		{ nullptr, nullptr }
	};
	script.Register(natives, -1);

	std::lock_guard<std::mutex> lock(scriptsMutex_);
	scripts_.push_back(&script);
}

void NUIComponent::onAmxUnload(IPawnScript& script)
{
	std::lock_guard<std::mutex> lock(scriptsMutex_);
	scripts_.erase(std::remove(scripts_.begin(), scripts_.end(), &script), scripts_.end());
}

// ── AMX Natives ───────────────────────────────────────────────────────────────

cell AMX_NATIVE_CALL NUIComponent::n_NUI_CreateResource(AMX* amx, const cell* params)
{
	if (!s_instance || !s_instance->pawn_) return 0;
	if (params[0] < 2 * (cell)sizeof(cell)) return 0;
	IPawnScript* script = s_instance->pawn_->getScript(amx);
	if (!script) return 0;

	cell* namePtr = nullptr; cell* pathPtr = nullptr;
	script->GetAddr(params[1], &namePtr);
	script->GetAddr(params[2], &pathPtr);
	char name[64] = {}, path[256] = {};
	script->GetString(name, namePtr, false, sizeof(name) - 1);
	script->GetString(path, pathPtr, false, sizeof(path) - 1);

	s_instance->createResource(StringView(name), StringView(path));
	return 1;
}

cell AMX_NATIVE_CALL NUIComponent::n_NUI_SendMessage(AMX* amx, const cell* params)
{
	if (!s_instance || !s_instance->pawn_) return 0;
	if (params[0] < 3 * (cell)sizeof(cell)) return 0;
	IPawnScript* script = s_instance->pawn_->getScript(amx);
	if (!script) return 0;

	int playerid = (int)params[1];
	cell* resourcePtr = nullptr; cell* jsonPtr = nullptr;
	script->GetAddr(params[2], &resourcePtr);
	script->GetAddr(params[3], &jsonPtr);
	char resource[64] = {}, json[4096] = {};
	script->GetString(resource, resourcePtr, false, sizeof(resource) - 1);
	script->GetString(json,     jsonPtr,     false, sizeof(json) - 1);

	IPlayer* player = s_instance->core_->getPlayers().get(playerid);
	if (!player) return 0;
	return s_instance->sendMessage(*player, StringView(resource), StringView(json)) ? 1 : 0;
}

cell AMX_NATIVE_CALL NUIComponent::n_NUI_Show(AMX* amx, const cell* params)
{
	if (!s_instance || !s_instance->pawn_) return 0;
	if (params[0] < 2 * (cell)sizeof(cell)) return 0;
	IPawnScript* script = s_instance->pawn_->getScript(amx);
	if (!script) return 0;

	int playerid = (int)params[1];
	cell* resourcePtr = nullptr;
	script->GetAddr(params[2], &resourcePtr);
	char resource[64] = {};
	script->GetString(resource, resourcePtr, false, sizeof(resource) - 1);

	IPlayer* player = s_instance->core_->getPlayers().get(playerid);
	if (!player) return 0;
	return s_instance->showNUI(*player, StringView(resource)) ? 1 : 0;
}

cell AMX_NATIVE_CALL NUIComponent::n_NUI_Hide(AMX* amx, const cell* params)
{
	if (!s_instance || !s_instance->pawn_) return 0;
	if (params[0] < 2 * (cell)sizeof(cell)) return 0;
	IPawnScript* script = s_instance->pawn_->getScript(amx);
	if (!script) return 0;

	int playerid = (int)params[1];
	cell* resourcePtr = nullptr;
	script->GetAddr(params[2], &resourcePtr);
	char resource[64] = {};
	script->GetString(resource, resourcePtr, false, sizeof(resource) - 1);

	IPlayer* player = s_instance->core_->getPlayers().get(playerid);
	if (!player) return 0;
	return s_instance->hideNUI(*player, StringView(resource)) ? 1 : 0;
}

// ── Entry point ───────────────────────────────────────────────────────────────

COMPONENT_ENTRY_POINT()
{
	return &component;
}
