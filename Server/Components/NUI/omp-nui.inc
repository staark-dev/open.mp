#if defined _omp_nui_included
    #endinput
#endif
#define _omp_nui_included

// Register a folder to be served at http://<server>:7778/nui/{name}/
// path is relative to server working dir (e.g. "nui/hud")
native bool:NUI_CreateResource(const name[], const path[]);

// Send a JSON string to a player's NUI — triggers window.addEventListener('message')
native bool:NUI_SendMessage(playerid, const resource[], const json[]);

// Open the CEF browser overlay for a player (NUIShow RPC → handled by client DLL)
// Also generates a per-player auth token sent with the RPC
native bool:NUI_Show(playerid, const resource[]);

// Close/hide the CEF browser overlay for a player
native bool:NUI_Hide(playerid, const resource[]);

// Returns true if the CEF browser for resource is currently open for player
native bool:NUI_IsVisible(playerid, const resource[]);

// Override the HTTP base URL sent to clients in the NUI_Show RPC.
// Default: auto-detected from network.public_addr in config.json.
// Call this in OnGameModeInit() when hosting on a VPS:
//   NUI_SetBaseURL("http://185.23.45.67:7778");
// Or with a domain + reverse proxy:
//   NUI_SetBaseURL("https://nui.myserver.com");
native NUI_SetBaseURL(const url[]);

// Fired on the server when a player's HTML does:
//   fetch(window.__NUI_BASE_URL__ + '/nui/callback/{resource}', {
//       method: 'POST',
//       headers: { 'X-NUI-Token': window.__NUI_TOKEN__ },
//       body: JSON.stringify({ action: 'login', user: '...', pass: '...' })
//   })
// data[] contains the raw JSON body
forward OnNUIMessage(playerid, const resource[], const data[]);
