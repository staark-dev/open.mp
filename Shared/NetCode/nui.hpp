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
	// RPC ID 220 — reserved for omp-nui server→client message
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
}
}
