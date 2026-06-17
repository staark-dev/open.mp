#pragma once

namespace RpcReceiver
{
    // Hooks WinSock recvfrom to intercept UDP packets from the game server.
    // Parses SA-MP/OMP RPC packets and routes NUI RPCs (220/221/222) to CefManager.
    void Install();
    void Uninstall();
}
