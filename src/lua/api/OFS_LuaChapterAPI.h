#pragma once
#include "OFS_Lua.h"

class OFS_ChapterAPI
{
    public:
    OFS_ChapterAPI(sol::usertype<class OFS_ExtensionAPI>& ofs) noexcept;
    ~OFS_ChapterAPI() noexcept;

    static sol::table Chapters(sol::this_state L) noexcept;
    static bool AddChapter(lua_Number startTime, lua_Number endTime,
        const char* name, sol::optional<std::string> color) noexcept;
    static bool RemoveChapter(lua_Integer index) noexcept;
    static void ClearChapters() noexcept;

    static sol::table Bookmarks(sol::this_state L) noexcept;
    static bool AddBookmark(lua_Number time, const char* name) noexcept;
    static void ClearBookmarks() noexcept;
};
