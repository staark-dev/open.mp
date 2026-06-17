/*
 * CEF subprocess helper — spawned automatically by CEF for renderer/gpu/utility processes.
 * Must ship as omp-nui-helper.exe alongside omp-nui.dll.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "include/cef_app.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    CefMainArgs args(GetModuleHandle(nullptr));
    return CefExecuteProcess(args, nullptr, nullptr);
}
