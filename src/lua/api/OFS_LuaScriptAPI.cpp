#include "OFS_LuaScriptAPI.h"
#include "OpenFunscripter.h"
#include "api/OFS_WebsocketApiEvents.h"
#include "OFS_EventSystem.h"

OFS_ScriptAPI::OFS_ScriptAPI(sol::usertype<class OFS_ExtensionAPI>& ofs) noexcept
{
    auto L = sol::state_view(ofs.lua_state());
    auto script = L.new_usertype<LuaFunscript>("Funscript");
    script["hasSelection"] = &LuaFunscript::HasSelection;
    script["actions"] = sol::readonly_property(&LuaFunscript::Actions);
    script["commit"] = &LuaFunscript::Commit;
    script["sort"] = &LuaFunscript::Sort;
    script["closestAction"] = &LuaFunscript::ClosestAction;
    script["closestActionAfter"] = &LuaFunscript::ClosestActionAfter;
    script["closestActionBefore"] = &LuaFunscript::ClosestActionBefore;
    script["selectedIndices"] = &LuaFunscript::SelectedIndices;
    script["markForRemoval"] = &LuaFunscript::MarkForRemoval;
    script["removeMarked"] = &LuaFunscript::RemoveMarked;
    
    script["path"] = sol::readonly_property(&LuaFunscript::Path);
    script["name"] = sol::readonly_property(&LuaFunscript::Name);

    auto action = L.new_usertype<LuaFunscriptAction>("Action",
        sol::constructors<LuaFunscriptAction(lua_Number, lua_Integer), LuaFunscriptAction(lua_Number, lua_Integer, bool)>());
    action["at"] = sol::property(&LuaFunscriptAction::at, &LuaFunscriptAction::set_at);
    action["pos"] = sol::property(&LuaFunscriptAction::pos, &LuaFunscriptAction::set_pos);
    action["selected"] = &LuaFunscriptAction::selected;

    ofs["ActiveIdx"] = OFS_ScriptAPI::ActiveIdx;
    ofs["Script"] = OFS_ScriptAPI::Script;
    ofs["Clipboard"] = OFS_ScriptAPI::Clipboard;
    ofs["Undo"] = OFS_ScriptAPI::Undo;
    ofs["AddFunscript"] = OFS_ScriptAPI::AddFunscript;
}

// Load an additional funscript into the open project, as its own track.
// Useful for comparing two versions side by side (a generated candidate against
// the script being edited) without leaving the project.
bool OFS_ScriptAPI::AddFunscript(const char* path) noexcept
{
    FUN_ASSERT(Util::InMainThread(), "Not in main thread.");
    if(!path) return false;

    auto app = OpenFunscripter::ptr;
    if(!app->LoadedProject || !app->LoadedProject->IsValid())
    {
        return false;
    }

    // Guard before delegating: OFS_Project::AddFunscript appends an *empty*
    // script when the file is missing or unparseable, so an unchecked call
    // leaves a junk track behind on every typo. That fallback is deliberate
    // there -- "File -> Add new" and ImportFromMedia create tracks for paths
    // that don't exist yet -- so the check belongs here, at the call site that
    // only ever wants an existing script.
    if(!Util::FileExists(path))
    {
        return false;
    }
    bool parsed = false;
    auto json = Util::ParseJson(Util::ReadFileString(path), &parsed);
    if(!parsed || !json.is_object())
    {
        return false;
    }
    // Being valid JSON isn't enough -- any object would slip through and still
    // land an empty track. Require something Funscript::Deserialize can read:
    // "actions" (1.0), "channels" (2.0) or "axes" (1.1).
    bool looksLikeFunscript = (json.contains("actions") && json["actions"].is_array())
                           || (json.contains("channels") && json["channels"].is_object())
                           || (json.contains("axes") && json["axes"].is_array());
    if(!looksLikeFunscript)
    {
        return false;
    }

    // The UI enumerates LoadedFunscripts() every frame, so appending is enough
    // for it. Websocket clients are event-driven though, and no add-event
    // exists, so re-broadcast the project to keep them in sync.
    if(!app->LoadedProject->AddFunscript(path))
    {
        return false;
    }
    EV::Queue().directDispatch(WsProjectChange::EventType, EV::Make<WsProjectChange>());
    return true;
}

lua_Integer OFS_ScriptAPI::ActiveIdx() noexcept
{
    auto app = OpenFunscripter::ptr;
    return app->LoadedProject->ActiveIdx() + 1;
}

std::unique_ptr<LuaFunscript> OFS_ScriptAPI::Script(lua_Integer idx) noexcept
{
    auto app = OpenFunscripter::ptr;
    idx -= 1;
    if(idx < 0 || idx >= app->LoadedFunscripts().size()) {
        return nullptr;
    }
    return std::make_unique<LuaFunscript>(static_cast<int32_t>(idx), app->LoadedFunscripts()[idx]);
}

std::unique_ptr<LuaFunscript> OFS_ScriptAPI::Clipboard() noexcept
{
    auto app = OpenFunscripter::ptr;
    auto clipboard = app->FunscriptClipboard();
    return std::make_unique<LuaFunscript>(clipboard);
}

bool OFS_ScriptAPI::Undo() noexcept
{
    bool undo = false;
    auto app = OpenFunscripter::ptr;
    if(app->undoSystem->MatchUndoTop(StateType::CUSTOM_LUA)) {
        undo = app->undoSystem->Undo();
    }
    return undo;
}

LuaFunscript::LuaFunscript(int32_t scriptIdx, std::weak_ptr<Funscript> script) noexcept
    : script(script), scriptIdx(scriptIdx)
{
    FUN_ASSERT(Util::InMainThread(), "Not in main thread.");
    this->TakeSnapshot();
}

LuaFunscript::LuaFunscript(const FunscriptArray& actions) noexcept
{
    for(auto a : actions) {
        this->actions.emplace_back(a, false);
    }
}

void LuaFunscript::Commit(sol::this_state L) noexcept
{
    FUN_ASSERT(Util::InMainThread(), "Not in main thread.");
    auto app = OpenFunscripter::ptr;
    auto ref = script.lock();
    if(ref) {
        FunscriptArray commit;
        FunscriptArray selection;
        commit.reserve(actions.size());
        for(auto action : actions) {
            auto succ = commit.emplace(action.o);
            if(!succ) {
                luaL_error(L.lua_state(), "Tried adding multiple actions with the same timestamp.");
                return;
            }
            if(action.selected) {
                selection.emplace(action.o);
            }
        }
        app->undoSystem->Snapshot(StateType::CUSTOM_LUA, script);
        ref->SetActions(commit);
        ref->SetSelection(selection);
    }
}

std::string LuaFunscript::Path() const noexcept
{
    auto app = OpenFunscripter::ptr;
    auto ptr = script.lock();
    if(scriptIdx > 0 && scriptIdx < app->LoadedFunscripts().size())
    {
        // This is a safety measure in case the LuaFunscript outlives the actual Funscript
        // in which case this shouldn't be equal.
        if(ptr == app->LoadedFunscripts()[scriptIdx])
        {
            auto relPath = app->LoadedFunscripts()[scriptIdx]->RelativePath();
            return app->LoadedProject->MakePathAbsolute(relPath);
        }
    }
    return {};
}

const char* LuaFunscript::Name() const noexcept
{
    auto ref = script.lock();
    if(ref) {
        return ref->Title().c_str();
    }
    return nullptr;
}

bool LuaFunscript::HasSelection() const noexcept
{
    return std::any_of(actions.begin(), actions.end(), [](auto a) { return a.selected; });
}

sol::optional<std::tuple<LuaFunscriptAction, lua_Integer>> LuaFunscript::ClosestAction(lua_Number time) noexcept
{
    float closestDelta = std::numeric_limits<float>::max();
    int closestIdx = -1;
    LuaFunscriptAction closestAction(FunscriptAction(0, 0), false);

    for(uint32_t i=0, size=actions.size(); i < size; i += 1) {
        auto a = actions[i];
        float delta = std::abs(a.at() - time);
        if(delta < closestDelta) {
            closestDelta = delta;
            closestIdx = i;
            closestAction = a;
        }
    }
    if(closestDelta != std::numeric_limits<float>::max()) {
        return sol::make_optional(std::make_tuple(closestAction, closestIdx + 1));
    }
    return sol::optional<std::tuple<LuaFunscriptAction, lua_Integer>>();
}

sol::optional<std::tuple<LuaFunscriptAction, lua_Integer>> LuaFunscript::ClosestActionAfter(lua_Number time) noexcept
{
    float closestDelta = std::numeric_limits<float>::max();
    int closestIdx = -1;
    LuaFunscriptAction closestAction(FunscriptAction(0, 0), false);

    for(uint32_t i=0, size=actions.size(); i < size; i += 1) {
        auto a = actions[i];
        if(a.at() < time) continue;
        float delta = std::abs(a.at() - time);
        if(delta < closestDelta && delta != 0.f) {
            closestDelta = delta;
            closestIdx = i;
            closestAction = a;
        }
    }
    if(closestDelta != std::numeric_limits<float>::max()) {
        return sol::make_optional(std::make_tuple(closestAction, closestIdx + 1));
    }
    return sol::optional<std::tuple<LuaFunscriptAction, lua_Integer>>();
}

sol::optional<std::tuple<LuaFunscriptAction, lua_Integer>> LuaFunscript::ClosestActionBefore(lua_Number time) noexcept
{
    float closestDelta = std::numeric_limits<float>::max();
    int closestIdx = -1;
    LuaFunscriptAction closestAction(FunscriptAction(0, 0), false);

    for(uint32_t i=0, size=actions.size(); i < size; i += 1) {
        auto a = actions[i];
        if(a.at() > time) continue;
        float delta = std::abs(a.at() - time);
        if(delta < closestDelta && delta != 0.f) {
            closestDelta = delta;
            closestIdx = i;
            closestAction = a;
        }
    }
    if(closestDelta != std::numeric_limits<float>::max()) {
        return sol::make_optional(std::make_tuple(closestAction, closestIdx + 1));
    }
    return sol::optional<std::tuple<LuaFunscriptAction, lua_Integer>>();
}

std::vector<lua_Integer> LuaFunscript::SelectedIndices() const noexcept
{
    std::vector<lua_Integer> selectedIndices;
    for(uint32_t i=0, size=actions.size(); i < size; i += 1) {
        if(actions[i].selected) {
            selectedIndices.emplace_back(i+1);
        }
    }
    return selectedIndices;
}

void LuaFunscript::MarkForRemoval(lua_Integer idx, sol::this_state L) noexcept
{
    idx -= 1;
    if(idx >= 0 && idx < actions.size()) {
        markedIndices.insert(idx);
    }
    else {
        luaL_error(L.lua_state(), "Out of bounds index.");
    }
}

lua_Integer LuaFunscript::RemoveMarked() noexcept
{
    LuaFunscriptArray filteredActions;
    for(uint32_t i=0, size=actions.size(); i < size; i += 1) {
        if(markedIndices.find(i) == markedIndices.end()) {
            filteredActions.emplace_back(actions[i]);
        }
    }
    auto removedCount = actions.size() - filteredActions.size();
    actions = std::move(filteredActions);
    markedIndices.clear();
    return removedCount;
}