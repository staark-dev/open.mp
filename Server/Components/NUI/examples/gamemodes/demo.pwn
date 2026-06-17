#include <open.mp>

#tryinclude <omp-nui>

main() {}

public OnGameModeInit()
{
    SetGameModeText("omp-nui Test");
    AddPlayerClass(0, 1958.3, 1343.1, 15.4, 270.0, 0, 0, 0, 0, 0, 0);

    #if defined _omp_nui_included
        NUI_CreateResource("hud", "nui/hud/");
        print("[NUI] Resursa HUD inregistrata");
    #else
        print("[NUI] Plugin NUI negasit - ruleaza fara NUI");
    #endif
    return 1;
}

public OnPlayerSpawn(playerid)
{
    #if defined _omp_nui_included
        NUI_SendMessage(playerid, "hud", "{\"type\":\"setHealth\",\"value\":100}");
        NUI_SendMessage(playerid, "hud", "{\"type\":\"setMoney\",\"value\":500}");
    #endif
    return 1;
}

public OnPlayerTakeDamage(playerid, issuerid, Float:amount, weaponid, bodypart)
{
    #if defined _omp_nui_included
        new Float:hp;
        GetPlayerHealth(playerid, hp);
        new msg[64];
        format(msg, sizeof(msg), "{\"type\":\"setHealth\",\"value\":%d}", floatround(hp));
        NUI_SendMessage(playerid, "hud", msg);
    #endif
    return 1;
}
