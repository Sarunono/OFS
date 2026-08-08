# Extension API Reference

**The reference lives in [`LuaDox/stubs.lua`](LuaDox/stubs.lua), and the guide in
[`LuaDox/intro.md`](LuaDox/intro.md).** Those are the source the published docs
at <https://openfunscripter.github.io/API/> are generated from, and they are
kept current with `src/lua/api/`. Read them, not a second copy.

This file used to be a hand-written duplicate of the API, and it drifted: it
still described the 1.4.4 shape (`ofs.AddAction`, `ofs.Commit`, `ofs.Bind`) long
after that had been replaced. It is deliberately no longer a duplicate. What is
left below is only what `LuaDox/` cannot tell you: the calls this fork adds, and
the places where the old text actively misleads.

If you change the Lua API, update `LuaDox/stubs.lua` in the same commit.

## Additions in this fork

Documented in full in `LuaDox/stubs.lua`; summarised here because upstream has
no equivalent.

| Call | Returns | Notes |
| --- | --- | --- |
| `ofs.AddFunscript(path)` | `bool` | Loads an existing funscript as an extra track in the open project. |
| `ofs.Chapters()` | `table[]` | Entries have `startTime`, `endTime`, `name`. Ordered by start time. |
| `ofs.AddChapter(startTime, endTime, name [, "#RRGGBB"])` | `bool` | False on a non-positive span or an overlap. Touching spans are allowed. |
| `ofs.RemoveChapter(index)` | `bool` | 1-based, matching `ofs.Chapters()`. |
| `ofs.ClearChapters()` | `nil` | |
| `ofs.Bookmarks()` | `table[]` | Entries have `time`, `name`. |
| `ofs.AddBookmark(time, name)` | `bool` | False if a bookmark is already within one second. |
| `ofs.ClearBookmarks()` | `nil` | |

Times are in seconds. Chapter and bookmark mutations do nothing and report
failure when no project is loaded — the project state they would write to is
discarded the next time a project is opened, so a success there would be a lie.

There is also a websocket control plane for driving OFS from outside; see the
README, not this file.

## Corrections to the pre-1.4.4 API

If you find a snippet online using these, it is written against the old API and
will not run:

| Old and gone | Current |
| --- | --- |
| `ofs.AddAction(script, at, pos, sel)` | `script.actions:add(Action.new(at, pos, sel))` |
| `ofs.RemoveAction(script, action)` | `script:markForRemoval(idx)` then `script:removeMarked()` |
| `ofs.Commit(script)` | `script:commit()` |
| `ofs.Bind(name, description)` | Nothing. See below. |
| `ofs.Task(functionName)` | Nothing. There is no background-thread API. |

**Keybindings are not registered by a call.** Every entry in the global
`binding` table becomes a keybinding, discovered after `init()` returns and
listed under "Dynamic" in the keybinding dialog. Defining the function *is* the
registration:

```lua
function binding.do_the_thing()
    -- appears as "<ExtensionName>::do_the_thing"
end
```

The `binding` table already exists — the prelude in
`src/lua/OFS_LuaExtensionAPI.cpp` creates it before your `main.lua` runs — so
you never need to declare it.

## Things worth knowing before you write against this

Verified against `src/lua/api/`, and each one has cost someone an hour:

- **`action.at` is in seconds**, floating point. The 1.4.4 docs said
  milliseconds.
- **`script.actions` is a bound C++ container, not a table.** `ipairs`, `#` and
  indexing work; `table.insert` / `table.remove` silently do not. Grow it with
  `actions:add()`, shrink it with `markForRemoval()` + `removeMarked()`.
- **Setting `action.at` to a negative value clamps it to 0** rather than
  failing. Shift several actions past the start of the video and they collapse
  onto each other, and the `commit()` after that raises "Tried adding multiple
  actions with the same timestamp".
- **`script:commit()` rejects duplicate timestamps** with a Lua error, so check
  for collisions before committing rather than after.
- **`ofs.Script(idx)` returns nil** for an out-of-range index, including when
  no project is loaded. Guard it.
- **`ofs.Script()` returns a snapshot**, not a live view. Changes reach OFS only
  on `commit()`, and a commit writes back the whole action set including the
  `selected` flags.
- **`ofs.Undo()` only undoes Lua changes**, never the user's own edits. It
  exists for live-preview sliders that re-apply on every frame; a one-shot
  button does not need it, since `commit()` takes its own undo snapshot.
- **Bindings run on the main thread**, so committing from one is fine.
