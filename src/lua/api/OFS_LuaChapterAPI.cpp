#include "OFS_LuaChapterAPI.h"
#include "OFS_LuaExtensionAPI.h"

#include "OpenFunscripter.h"
#include "OFS_EventSystem.h"
#include "state/states/ChapterState.h"

#include <cstdio>

OFS_ChapterAPI::~OFS_ChapterAPI() noexcept
{

}

OFS_ChapterAPI::OFS_ChapterAPI(sol::usertype<OFS_ExtensionAPI>& ofs) noexcept
{
    ofs["Chapters"] = OFS_ChapterAPI::Chapters;
    ofs["AddChapter"] = OFS_ChapterAPI::AddChapter;
    ofs["RemoveChapter"] = OFS_ChapterAPI::RemoveChapter;
    ofs["ClearChapters"] = OFS_ChapterAPI::ClearChapters;

    ofs["Bookmarks"] = OFS_ChapterAPI::Bookmarks;
    ofs["AddBookmark"] = OFS_ChapterAPI::AddBookmark;
    ofs["ClearBookmarks"] = OFS_ChapterAPI::ClearBookmarks;
}

inline static ChapterState& State() noexcept
{
    return OpenFunscripter::ptr->chapterMgr->State();
}

// Chapters/bookmarks live in project state, so any mutation has to announce
// itself for the timeline, the funscript metadata and the websocket API to
// pick it up.
inline static void NotifyChanged() noexcept
{
    EV::Enqueue<ChapterStateChanged>();
}

// OFS starts with an invalid placeholder project, and the state slot above
// exists regardless. Writing to it looks like it works -- right up until
// openFile() calls ClearProjectAll() and drops everything -- so mutations
// refuse rather than report a change that won't survive. Readers are fine:
// no project simply means no chapters.
inline static bool ProjectLoaded() noexcept
{
    auto app = OpenFunscripter::ptr;
    return app->LoadedProject && app->LoadedProject->IsValid();
}

inline static ImColor ParseColor(const std::string& hex, ImColor fallback) noexcept
{
    const char* s = hex.c_str();
    if(*s == '#') s += 1;
    unsigned int r = 0, g = 0, b = 0;
    if(std::sscanf(s, "%02x%02x%02x", &r, &g, &b) == 3)
    {
        return IM_COL32(r, g, b, 255);
    }
    return fallback;
}

sol::table OFS_ChapterAPI::Chapters(sol::this_state L) noexcept
{
    FUN_ASSERT(Util::InMainThread(), "Not in main thread.");
    sol::state_view lua(L);
    auto result = lua.create_table();

    auto& state = State();
    for(int i = 0, size = state.chapters.size(); i < size; i += 1)
    {
        auto& chapter = state.chapters[i];
        auto entry = lua.create_table();
        entry["startTime"] = chapter.startTime;
        entry["endTime"] = chapter.endTime;
        entry["name"] = chapter.name;
        // Lua convention: 1-based, so it can be handed straight to RemoveChapter.
        result[i + 1] = entry;
    }
    return result;
}

bool OFS_ChapterAPI::AddChapter(lua_Number startTime, lua_Number endTime,
    const char* name, sol::optional<std::string> color) noexcept
{
    FUN_ASSERT(Util::InMainThread(), "Not in main thread.");
    if(!ProjectLoaded()) return false;
    auto& state = State();

    auto chapter = state.AddChapterRange((float)startTime, (float)endTime);
    if(!chapter)
    {
        // invalid range or it overlaps an existing chapter
        return false;
    }

    if(name) chapter->name = name;
    if(color.has_value())
    {
        chapter->color = ParseColor(color.value(), chapter->color);
    }

    NotifyChanged();
    return true;
}

bool OFS_ChapterAPI::RemoveChapter(lua_Integer index) noexcept
{
    FUN_ASSERT(Util::InMainThread(), "Not in main thread.");
    if(!ProjectLoaded()) return false;
    auto& state = State();

    index -= 1; // Lua is 1-based
    if(index < 0 || index >= (lua_Integer)state.chapters.size())
    {
        return false;
    }

    state.chapters.erase(state.chapters.begin() + index);
    NotifyChanged();
    return true;
}

void OFS_ChapterAPI::ClearChapters() noexcept
{
    FUN_ASSERT(Util::InMainThread(), "Not in main thread.");
    if(!ProjectLoaded()) return;
    auto& state = State();
    if(state.chapters.empty()) return;
    state.chapters.clear();
    NotifyChanged();
}

sol::table OFS_ChapterAPI::Bookmarks(sol::this_state L) noexcept
{
    FUN_ASSERT(Util::InMainThread(), "Not in main thread.");
    sol::state_view lua(L);
    auto result = lua.create_table();

    auto& state = State();
    for(int i = 0, size = state.bookmarks.size(); i < size; i += 1)
    {
        auto& bookmark = state.bookmarks[i];
        auto entry = lua.create_table();
        entry["time"] = bookmark.time;
        entry["name"] = bookmark.name;
        result[i + 1] = entry;
    }
    return result;
}

bool OFS_ChapterAPI::AddBookmark(lua_Number time, const char* name) noexcept
{
    FUN_ASSERT(Util::InMainThread(), "Not in main thread.");
    if(!ProjectLoaded()) return false;
    auto& state = State();

    auto bookmark = state.AddBookmark((float)time);
    if(!bookmark)
    {
        // too close to an existing bookmark (1 second minimum)
        return false;
    }

    if(name) bookmark->name = name;
    NotifyChanged();
    return true;
}

void OFS_ChapterAPI::ClearBookmarks() noexcept
{
    FUN_ASSERT(Util::InMainThread(), "Not in main thread.");
    if(!ProjectLoaded()) return;
    auto& state = State();
    if(state.bookmarks.empty()) return;
    state.bookmarks.clear();
    NotifyChanged();
}
