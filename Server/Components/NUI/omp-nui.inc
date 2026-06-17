#if defined _omp_nui_included
    #endinput
#endif
#define _omp_nui_included

// Register a folder to be served at http://localhost:7778/nui/{name}/
// path is relative to server working dir (e.g. "nui/hud")
native bool:NUI_CreateResource(const name[], const path[]);

// Send a JSON string to a player's NUI — triggers window.addEventListener('message')
native bool:NUI_SendMessage(playerid, const resource[], const json[]);

// Open the CEF browser overlay for a player (NUIShow RPC → handled by Bloc 2 client DLL)
// Also generates a per-player auth token sent with the RPC
native bool:NUI_Show(playerid, const resource[]);

// Close/hide the CEF browser overlay for a player
native bool:NUI_Hide(playerid, const resource[]);

// Fired on the server when a player's HTML does:
//   fetch('http://localhost:7778/nui/callback/{resource}', {
//       method: 'POST',
//       headers: { 'X-NUI-Token': window.__NUI_TOKEN__ },
//       body: JSON.stringify({ action: 'login', user: '...', pass: '...' })
//   })
// data[] contains the raw JSON body
forward OnNUIMessage(playerid, const resource[], const data[]);
