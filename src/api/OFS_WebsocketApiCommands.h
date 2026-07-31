#pragma once

#include <vector>
#include <variant>
#include <memory>
#include <string>

#include "SDL_atomic.h"
#include "OFS_Util.h"

#include "state/states/ChapterState.h"

class WsCmd
{
    public:
    // Optional client-assigned id. When present the command's outcome is
    // reported back as a "command_result" event -- a state-change event is not a
    // reliable ack, since OFS only emits one when the state actually changes.
    std::string id;
    std::string name;
    std::string error;

    // Returns false on failure, with `error` explaining why.
    virtual bool Run() noexcept = 0;
    virtual ~WsCmd() noexcept = default;
};

class WsPlayChangeCmd : public WsCmd
{
    public:
    bool playing = false;
    WsPlayChangeCmd(bool playing) noexcept
        : playing(playing) {}

    bool Run() noexcept override;
};

class WsPlaybackSpeedChangeCmd : public WsCmd
{
    public:
    float speed = 1.f;
    WsPlaybackSpeedChangeCmd(float speed) noexcept
        : speed(speed) {}

    bool Run() noexcept override;
};

class WsTimeChangeCmd : public WsCmd
{
    public:
    float time = 0.f;
    WsTimeChangeCmd(float time) noexcept
        : time(time) {}

    bool Run() noexcept override;
};

class WsSeekRelativeCmd : public WsCmd
{
    public:
    float seconds = 0.f;
    WsSeekRelativeCmd(float seconds) noexcept
        : seconds(seconds) {}

    bool Run() noexcept override;
};

// Re-broadcast the full state (media, duration, position, every script). Lets a
// client resynchronise without reconnecting.
class WsGetStateCmd : public WsCmd
{
    public:
    bool Run() noexcept override;
};

// Open a project, a funscript or a media file -- the same entry point the
// "File -> Open" menu and the command line use.
class WsOpenFileCmd : public WsCmd
{
    public:
    std::string path;
    WsOpenFileCmd(const std::string& path) noexcept
        : path(path) {}

    bool Run() noexcept override;
};

class WsSaveProjectCmd : public WsCmd
{
    public:
    bool Run() noexcept override;
};

// Replace the chapter timeline wholesale.
class WsSetChaptersCmd : public WsCmd
{
    public:
    std::vector<Chapter> newChapters;
    WsSetChaptersCmd(std::vector<Chapter>&& chapters) noexcept
        : newChapters(std::move(chapters)) {}

    bool Run() noexcept override;
};

// Reports a command that could not be constructed. Queued so the failure is
// reported from the main thread, like every other result.
class WsFailedCmd : public WsCmd
{
    public:
    WsFailedCmd(const std::string& why) noexcept { error = why; }
    bool Run() noexcept override { return false; }
};

class WsCommandBuffer
{
    private:
    std::vector<std::unique_ptr<WsCmd>> commands;
    SDL_SpinLock commandLock = {0};
    public:

    WsCommandBuffer() noexcept;
    bool AddCmd(const nlohmann::json& jsonCmd) noexcept;
    void ProcessCommands() noexcept;
};
