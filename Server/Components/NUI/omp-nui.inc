#if defined _omp_nui_included
	#endinput
#endif
#define _omp_nui_included

/**
 * Register a NUI resource so the HTTP server can serve its files.
 *
 * @param name     Short identifier for this resource (e.g. "hud")
 * @param path     Filesystem path to the folder containing the resource files
 *                 (relative to the server working directory, e.g. "nui/hud")
 * @return         true on success
 *
 * After registration, files are accessible at:
 *   http://<server>:7778/nui/<name>/<file>
 */
native bool:NUI_CreateResource(const name[], const path[]);

/**
 * Send a JSON message to a player's NUI.
 *
 * @param playerid   Target player
 * @param resource   Resource name registered with NUI_CreateResource
 * @param json       JSON string dispatched as a window 'message' event in the browser
 * @return           true if the RPC was sent successfully
 *
 * In the browser (JavaScript):
 *   window.addEventListener('message', function(e) {
 *       var d = e.data;
 *       // d.type, d.value, ...
 *   });
 */
native bool:NUI_SendMessage(playerid, const resource[], const json[]);
