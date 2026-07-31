#pragma once
#include "OFS_StateHandle.h"
#include <string>

struct WebsocketApiState
{
    static constexpr auto StateName = "WebsocketApi";
    std::string port = "8080";
    bool serverActive = false;
    // Off by default: the API can open and save files, so it listens on
    // loopback only unless this is deliberately turned on.
    bool exposeOnNetwork = false;

    static inline WebsocketApiState& State(uint32_t stateHandle) noexcept
    {
        return OFS_AppState<WebsocketApiState>(stateHandle).Get();
    }
};

REFL_TYPE(WebsocketApiState)
    REFL_FIELD(port)
    REFL_FIELD(serverActive)
    REFL_FIELD(exposeOnNetwork)
REFL_END