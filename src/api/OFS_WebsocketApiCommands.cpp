#include "OFS_WebsocketApiCommands.h"
#include "OFS_WebsocketApiEvents.h"
#include "OFS_EventSystem.h"
#include <optional>
#include <algorithm>

WsCommandBuffer::WsCommandBuffer() noexcept
{

}

inline static std::unique_ptr<WsCmd> CreateCommand(const std::string& name, const nlohmann::json& data) noexcept
{
    if(name == "change_time" && data["time"].is_number())
    {
        float time = data["time"].get<float>();
        return std::make_unique<WsTimeChangeCmd>(time);
    }
    else if(name == "change_play" && data["playing"].is_boolean())
    {
        bool playing = data["playing"].get<bool>();
        return std::make_unique<WsPlayChangeCmd>(playing);
    }
    else if(name == "change_playbackspeed" && data["speed"].is_number())
    {
        float speed = data["speed"].get<float>();
        return std::make_unique<WsPlaybackSpeedChangeCmd>(speed);
    }
    else if(name == "seek_relative" && data["seconds"].is_number())
    {
        float seconds = data["seconds"].get<float>();
        return std::make_unique<WsSeekRelativeCmd>(seconds);
    }
    else if(name == "get_state")
    {
        return std::make_unique<WsGetStateCmd>();
    }
    else if(name == "open_file" && data["path"].is_string())
    {
        return std::make_unique<WsOpenFileCmd>(data["path"].get<std::string>());
    }
    else if(name == "save_project")
    {
        return std::make_unique<WsSaveProjectCmd>();
    }
    else if(name == "set_chapters" && data["chapters"].is_array())
    {
        std::vector<Chapter> chapters;
        for(auto& item : data["chapters"])
        {
            if(!item.is_object()) continue;
            if(!item.contains("startTime") || !item["startTime"].is_number()) continue;
            if(!item.contains("endTime") || !item["endTime"].is_number()) continue;

            Chapter chapter;
            chapter.startTime = item["startTime"].get<float>();
            chapter.endTime = item["endTime"].get<float>();
            if(item.contains("name") && item["name"].is_string())
            {
                chapter.name = item["name"].get<std::string>();
            }
            chapters.emplace_back(std::move(chapter));
        }
        return std::make_unique<WsSetChaptersCmd>(std::move(chapters));
    }
    return {};
}

bool WsCommandBuffer::AddCmd(const nlohmann::json& jsonCmd) noexcept
{
    auto& type = jsonCmd["type"];
    if(!type.is_string() || type != "command") return false;

    auto& name = jsonCmd["name"];
    if(!name.is_string()) return false;

    auto& data = jsonCmd["data"];
    if(data.is_null()) return false;

    std::string id;
    if(jsonCmd.contains("id") && jsonCmd["id"].is_string())
    {
        id = jsonCmd["id"].get<std::string>();
    }
    const auto& cmdName = name.get_ref<const std::string&>();

    auto cmd = CreateCommand(cmdName, data);
    if(!cmd)
    {
        if(id.empty()) return false;
        // Report the rejection instead of dropping it silently. Queued rather
        // than sent here because this runs on a civetweb worker thread.
        cmd = std::make_unique<WsFailedCmd>("unknown command or invalid parameters");
    }

    cmd->id = std::move(id);
    cmd->name = cmdName;

    SDL_AtomicLock(&commandLock);
    commands.emplace_back(std::move(cmd));
    SDL_AtomicUnlock(&commandLock);
    return true;
}

#include "OpenFunscripter.h"

void WsCommandBuffer::ProcessCommands() noexcept
{
    if(commands.empty()) return;
    SDL_AtomicLock(&commandLock);
    for(auto& cmd : commands)
    {
        bool ok = cmd->Run();
        if(!cmd->id.empty())
        {
            auto app = OpenFunscripter::ptr;
            if(app->webApi)
            {
                app->webApi->PushCommandResult(cmd->id, cmd->name, ok, cmd->error);
            }
        }
    }
    commands.clear();
    SDL_AtomicUnlock(&commandLock);
}

bool WsPlayChangeCmd::Run() noexcept
{
    auto app = OpenFunscripter::ptr;
    app->player->SetPaused(!playing);
    return true;
}

bool WsPlaybackSpeedChangeCmd::Run() noexcept
{
    auto app = OpenFunscripter::ptr;
    app->player->SetSpeed(speed);
    return true;
}

bool WsTimeChangeCmd::Run() noexcept
{
    auto app = OpenFunscripter::ptr;
    app->player->SetPositionExact(time);
    return true;
}

bool WsSeekRelativeCmd::Run() noexcept
{
    auto app = OpenFunscripter::ptr;
    app->player->SeekRelative(seconds);
    return true;
}

bool WsGetStateCmd::Run() noexcept
{
    // Each connected client answers this by re-sending everything it knows.
    EV::Queue().directDispatch(WsProjectChange::EventType, EV::Make<WsProjectChange>());
    return true;
}

bool WsOpenFileCmd::Run() noexcept
{
    if(!Util::FileExists(path))
    {
        error = "file not found: " + path;
        return false;
    }
    auto app = OpenFunscripter::ptr;
    // Note: with unsaved edits this raises the usual confirmation dialog, so the
    // load completes only once the user answers. Deliberate -- a remote command
    // must not discard local work.
    app->openFile(path);
    return true;
}

bool WsSaveProjectCmd::Run() noexcept
{
    auto app = OpenFunscripter::ptr;
    if(!app->LoadedProject || !app->LoadedProject->IsValid())
    {
        error = "no project loaded";
        return false;
    }
    app->saveProject();
    return true;
}

bool WsSetChaptersCmd::Run() noexcept
{
    // Atomic: validate the whole request before touching anything. Clearing
    // first and inserting as we go would let a single bad span destroy the
    // user's existing chapters and leave a partial timeline behind -- while
    // still reporting failure.
    auto sorted = newChapters;
    std::sort(sorted.begin(), sorted.end(),
        [](const Chapter& a, const Chapter& b) noexcept { return a.startTime < b.startTime; });

    for(int i = 0, size = sorted.size(); i < size; i += 1)
    {
        if(!(sorted[i].endTime > sorted[i].startTime))
        {
            error = "chapter " + std::to_string(i + 1) + " has a non-positive duration";
            return false;
        }
        // Touching is allowed (contiguous spans); real overlap is not.
        if(i > 0 && sorted[i].startTime < sorted[i - 1].endTime)
        {
            error = "chapters " + std::to_string(i) + " and " + std::to_string(i + 1)
                  + " overlap";
            return false;
        }
    }

    auto app = OpenFunscripter::ptr;
    auto& state = app->chapterMgr->State();

    state.chapters.clear();
    for(auto& chapter : sorted)
    {
        auto inserted = state.AddChapterRange(chapter.startTime, chapter.endTime);
        if(inserted)
        {
            inserted->name = chapter.name;
        }
    }

    EV::Enqueue<ChapterStateChanged>();
    return true;
}
