/*
 *  This Source Code Form is subject to the terms of the Mozilla Public License,
 *  v. 2.0. If a copy of the MPL was not distributed with this file, You can
 *  obtain one at http://mozilla.org/MPL/2.0/.
 *
 *  The original code is copyright (c) 2022, open.mp team and contributors.
 */

#pragma once

#include <network.hpp>
#include <player.hpp>
#include <types.hpp>
#include <packet.hpp>

namespace NetCode
{
namespace RPC
{
	// RPC 220 — server→client: send JSON message to a NUI resource
	struct NUIMessage : NetworkPacketBase<220, NetworkPacketType::RPC, OrderingChannel_SyncRPC>
	{
		HybridString<64> Resource;
		HybridString<2048> JsonData;

		bool read(NetworkBitStream& bs)
		{
			bs.readDynStr8(Resource);
			return bs.readDynStr32(JsonData);
		}

		void write(NetworkBitStream& bs) const
		{
			bs.writeDynStr8(Resource);
			bs.writeDynStr32(JsonData);
		}
	};

	// RPC 221 — server→client: open CEF browser for a NUI resource
	// BaseURL example: "http://185.23.45.67:7778"
	// Client opens: {BaseURL}/nui/{Resource}/index.html
	// All relative fetch() calls in the HTML resolve against BaseURL automatically.
	struct NUIShow : NetworkPacketBase<221, NetworkPacketType::RPC, OrderingChannel_SyncRPC>
	{
		HybridString<64>  Resource;
		HybridString<64>  Token;
		HybridString<256> BaseURL;

		bool read(NetworkBitStream& bs)
		{
			bs.readDynStr8(Resource);
			bs.readDynStr8(Token);
			return bs.readDynStr8(BaseURL);
		}

		void write(NetworkBitStream& bs) const
		{
			bs.writeDynStr8(Resource);
			bs.writeDynStr8(Token);
			bs.writeDynStr8(BaseURL);
		}
	};

	// RPC 222 — server→client: hide/close CEF browser for a NUI resource
	struct NUIHide : NetworkPacketBase<222, NetworkPacketType::RPC, OrderingChannel_SyncRPC>
	{
		HybridString<64> Resource;

		bool read(NetworkBitStream& bs)
		{
			return bs.readDynStr8(Resource);
		}

		void write(NetworkBitStream& bs) const
		{
			bs.writeDynStr8(Resource);
		}
	};
}
}
