#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <string>
#include <cstring>
#include "rpc_receiver.hpp"
#include "cef_manager.hpp"

// ── RPC IDs (must match Shared/NetCode/nui.hpp on the server) ────────────────
static constexpr uint8_t RPC_NUI_MESSAGE = 220;
static constexpr uint8_t RPC_NUI_SHOW    = 221;
static constexpr uint8_t RPC_NUI_HIDE    = 222;

// ── SA-MP / OMP RakNet packet layout (simplified) ────────────────────────────
//
// UDP payload from server:
//   Byte 0:      RakNet packet ID
//     0x14 = DISCONNECT
//     0x85 = SA-MP RPC packet
//     0xA0 = SA-MP game packet
//   For ID 0x85 (RPC):
//     Byte 1:      RPC index (our RPC_NUI_* values)
//     Byte 2-3:    bit-length of payload (little-endian uint16)
//     Byte 4+:     payload bits
//
// Strings in our RPCs are length-prefixed:
//   NUIMessage:   [u8 resLen][res bytes][u32 jsonLen][json bytes]
//   NUIShow:      [u8 resLen][res bytes][u8 tokLen][tok bytes][u8 urlLen][url bytes]
//   NUIHide:      [u8 resLen][res bytes]

static constexpr uint8_t PACKET_RPC = 0x85;

// ── Bit-level reader for RakNet payloads ─────────────────────────────────────

struct BitReader
{
    const uint8_t* data;
    size_t         totalBits;
    size_t         bitPos = 0;

    BitReader(const uint8_t* d, size_t bits) : data(d), totalBits(bits) {}

    bool ReadByte(uint8_t& out)
    {
        if (bitPos + 8 > totalBits) return false;
        size_t byteIdx = bitPos / 8;
        size_t shift   = bitPos % 8;
        out = (shift == 0)
            ? data[byteIdx]
            : (data[byteIdx] << shift) | (data[byteIdx + 1] >> (8 - shift));
        bitPos += 8;
        return true;
    }

    bool ReadU32(uint32_t& out)
    {
        uint8_t b0, b1, b2, b3;
        if (!ReadByte(b0) || !ReadByte(b1) || !ReadByte(b2) || !ReadByte(b3)) return false;
        out = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
        return true;
    }

    bool ReadStr8(std::string& out)
    {
        uint8_t len;
        if (!ReadByte(len)) return false;
        out.resize(len);
        for (int i = 0; i < len; ++i)
            if (!ReadByte((uint8_t&)out[i])) return false;
        return true;
    }

    bool ReadStr32(std::string& out)
    {
        uint32_t len;
        if (!ReadU32(len) || len > 1024 * 1024) return false;
        out.resize(len);
        for (uint32_t i = 0; i < len; ++i)
            if (!ReadByte((uint8_t&)out[i])) return false;
        return true;
    }
};

// ── Parse and dispatch NUI RPCs ───────────────────────────────────────────────

static void HandleNUIPacket(uint8_t rpcId, const uint8_t* payload, size_t bitLen)
{
    BitReader br(payload, bitLen);

    if (rpcId == RPC_NUI_MESSAGE)
    {
        std::string resource, json;
        if (br.ReadStr8(resource) && br.ReadStr32(json))
            CefManager::SendMessage(resource, json);
    }
    else if (rpcId == RPC_NUI_SHOW)
    {
        std::string resource, token, baseUrl;
        if (br.ReadStr8(resource) && br.ReadStr8(token) && br.ReadStr8(baseUrl))
            CefManager::ShowNUI(resource, baseUrl, token);
    }
    else if (rpcId == RPC_NUI_HIDE)
    {
        std::string resource;
        if (br.ReadStr8(resource))
            CefManager::HideNUI(resource);
    }
}

// ── recvfrom hook ─────────────────────────────────────────────────────────────

using recvfrom_t = int(WSAAPI*)(SOCKET, char*, int, int, sockaddr*, int*);
static recvfrom_t oRecvfrom = nullptr;

static int WSAAPI hkRecvfrom(SOCKET s, char* buf, int len, int flags,
                               sockaddr* from, int* fromlen)
{
    int ret = oRecvfrom(s, buf, len, flags, from, fromlen);
    if (ret < 4) return ret;

    auto* data = reinterpret_cast<const uint8_t*>(buf);

    // RakNet wraps our RPCs in 0x85 packets with a simple reliability header.
    // Minimum size: ID(1) + RPC_ID(1) + bitLen(2) + at least 1 byte payload
    if (data[0] == PACKET_RPC && ret >= 5)
    {
        uint8_t  rpcId  = data[1];
        uint16_t bitLen = data[2] | (data[3] << 8);

        if (rpcId == RPC_NUI_MESSAGE || rpcId == RPC_NUI_SHOW || rpcId == RPC_NUI_HIDE)
            HandleNUIPacket(rpcId, data + 4, bitLen);
    }

    return ret;
}

// ── Import table hook for WS2_32!recvfrom ─────────────────────────────────────

static bool HookImport(HMODULE hMod, const char* dllName, const char* funcName, void* hook, void** original)
{
    auto* base = reinterpret_cast<uint8_t*>(hMod);
    auto* dos  = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt   = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    auto& imp  = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];

    auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + imp.VirtualAddress);
    for (; desc->Name; ++desc)
    {
        const char* name = reinterpret_cast<const char*>(base + desc->Name);
        if (_stricmp(name, dllName) != 0) continue;

        auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);
        auto* orig  = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->OriginalFirstThunk);

        for (; thunk->u1.Function; ++thunk, ++orig)
        {
            if (orig->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
            auto* ibn = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + orig->u1.AddressOfData);
            if (strcmp(reinterpret_cast<const char*>(ibn->Name), funcName) != 0) continue;

            DWORD old;
            VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &old);
            *original = reinterpret_cast<void*>(thunk->u1.Function);
            thunk->u1.Function = reinterpret_cast<ULONG_PTR>(hook);
            VirtualProtect(&thunk->u1.Function, sizeof(void*), old, &old);
            return true;
        }
    }
    return false;
}

void RpcReceiver::Install()
{
    // Hook recvfrom in SAMP.dll (SA-MP) — it's the DLL that calls recvfrom for game packets
    HMODULE samp = GetModuleHandleA("samp.dll");
    if (!samp) samp = GetModuleHandleA("omp-client.dll");  // open.mp client
    if (!samp) samp = GetModuleHandleA(nullptr);            // fallback: main exe

    HookImport(samp, "WS2_32.dll", "recvfrom", (void*)hkRecvfrom, (void**)&oRecvfrom);
}

void RpcReceiver::Uninstall()
{
    // Restore — skipped (process exiting anyway)
}
