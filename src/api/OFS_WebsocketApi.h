#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <string>
#include <vector>
#include <atomic>

#include "SDL_thread.h"
#include "SDL_atomic.h"
#include "SDL_timer.h"

#include "OFS_Event.h"

struct EventSerializationContext
{
    SDL_cond* processCond = nullptr;
    
    std::atomic<bool> shouldExit = false;
    std::atomic<bool> hasExited = false;

    SDL_SpinLock eventLock = {0};
    std::vector<EventPointer> events;

    EventSerializationContext() noexcept
    {
        processCond = SDL_CreateCond();
    }

    template<typename T, typename... Args>
    inline void Push(Args&&... args) noexcept
    {
        SDL_AtomicLock(&eventLock);
        events.emplace_back(std::move(std::make_shared<T>(std::forward<Args>(args)...)));
        SDL_AtomicUnlock(&eventLock);
    }

    inline bool EventsEmpty() noexcept
    {
        SDL_AtomicLock(&eventLock);
        bool empty = events.empty();
        SDL_AtomicUnlock(&eventLock);
        return empty;
    }

    inline int StartProcessing() noexcept
    {
        return SDL_CondSignal(processCond);
    }

    inline void Shutdown() noexcept
    {
        shouldExit = true;
        StartProcessing();
        while(!hasExited) { 
            SDL_Delay(1); 
        }
        SDL_DestroyCond(processCond);
    }
};

class OFS_WebsocketApi
{
    private:
    void* ctx = nullptr;
    uint32_t stateHandle = 0xFFFF'FFFF;
    std::vector<uint32_t> scriptUpdateCooldown;
    std::unique_ptr<EventSerializationContext> eventSerializationCtx;

    public:
    OFS_WebsocketApi() noexcept;
    OFS_WebsocketApi(const OFS_WebsocketApi&) = delete;
    OFS_WebsocketApi(OFS_WebsocketApi&&) = delete;

    bool Init() noexcept;

    bool StartServer() noexcept;
    void StopServer() noexcept;

    void Update() noexcept;
    void ShowWindow(bool* open) noexcept;
    void Shutdown() noexcept;

    // Acknowledge a command that carried an "id". Goes through the same
    // serialization queue as every other event, so only one thread ever writes
    // to a connection.
    void PushCommandResult(const std::string& id, const std::string& name,
        bool ok, const std::string& error) noexcept;

    int ClientsConnected() const noexcept;
};