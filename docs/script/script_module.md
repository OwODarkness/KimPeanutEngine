# Script Module

**Snapshot: 2026-08-29.** Hosts the Lua scripting layer. Two layers: a generic, engine-agnostic VM wrapper and an engine-facing command bridge.

## Layering

```
engine/runtime/script/
  CMakeLists.txt      Script   (STATIC)  — engine-facing binding layer
  script_core.cpp     future broader engine bindings
  command/
    lua_command_bridge.h/.cpp — engine.command registry adapter
  lua/
    CMakeLists.txt    ScriptLua (STATIC) — generic VM hosting
    lua_vm.h/.cpp     LuaVM
```

- **`ScriptLua` / `LuaVM`** — owns one sol2 state (`sol::state` behind a `unique_ptr`). Knows nothing about the engine: no asset paths, no `kpengine` classes. Depends only on `lua`/`sol2`/`Log`.
- **`Script`** — owns engine-facing bindings. It links `ScriptLua` and
  `RuntimeCommand`; the latter does not link back to Script or sol2. Everything
  engine-specific (exposing actors/components to Lua, wiring `package.path` to
  the project script dir, hot-reload, script lifecycle) belongs here, not in
  `LuaVM`.
- **Engine-owned instance** — `RuntimeContext::lua_vm_` (created in the ctor) is `Initialize`d at boot in `RuntimeContext::Initialize` and released in `Clear()`. The VM is a game-thread object; its creation happens on the render thread during boot, but execution must stay on the game thread (see Threading).

This mirrors the industry pattern (Unity/Unreal/Godot/CryEngine host the scripting runtime inside the engine runtime, never the editor, and keep the language VM separate from the engine bindings). `RuntimeLib` links `Script` PUBLIC; nothing in the script layer depends on the editor.

## Command bridge

`LuaCommandBridge` is created in `RuntimeContext::FinalizeGameStartup()` on the
Game thread. It exposes `engine.command.list`, `help`, `execute`, `poll`, and
`cancel`. Lua tables convert to the same typed `CommandCall` used by other
frontends, and results convert to tables with `status`, `message`, `request_id`,
and `data`.

The bridge only invokes `LuaAllowed` commands and applies normal capability
checks. Game-lane commands return `pending`; scripts poll the returned request
ID. Before the VM or registry is released, the bridge removes its sol2 callback
closures. Lua calls from a thread other than the bridge’s Game/Lua thread are
rejected.

## API

`LuaVM` is non-throwing and follows the runtime's error conventions (`bool` + `KP_LOG` for failures, `std::optional<T>` for lookups):

- `bool Initialize(uint64_t instruction_limit = kDefaultInstructionLimit)` — creates the state, opens the library set, installs the instruction hook. False (+ logged error) if already initialized.
- `void Shutdown()` / `bool IsInitialized()` — `Shutdown` closes the state and frees all script memory; `Initialize` may be called again after it.
- `bool ExecuteString(code)` / `bool ExecuteFile(path)` — syntax/runtime errors are logged, stored in `LastError()`, and returned as `false` (no exception).
- `bool RegisterFunction(name, func)` / `bool SetGlobal(name, value)` — bind C++ into the global scope; false (+ warning) before `Initialize`.
- `std::optional<T> GetGlobal<T>(name)` — nullopt for a missing/nil/type-mismatched global.
- `std::optional<sol::protected_function_result> CallFunction(name, args...)` — `nullopt` when the VM isn't initialized or the function is missing; otherwise a result the caller validates with `valid()` and reads via `get<T>()` / `get<sol::error>()`.
- `void SetInstructionLimit(uint64_t)` / `const std::string& LastError()`.

`RegisterFunction`/`SetGlobal`/`GetGlobal`/`CallFunction` are templates, so `LuaVM` leaks `sol2` types into its public API — by design, since the binding layer above it is sol2-native. `lua`/`sol2` are therefore PUBLIC links on `ScriptLua`.

## Sandbox decisions

- **Libraries opened:** `base`, `string`, `table`, `math`, `package`. `os`, `io`, `debug`, `coroutine` are never opened.
- **`package.loadlib` and `package.cpath` are stripped** — they load arbitrary native modules from disk. `require` (pure-Lua modules) is kept for modular first-party scripts.
- **Instruction budget** — a `lua_sethook` counter (stored in the state's `lua_getextraspace`) aborts a single top-level execution once it exceeds the budget (`luaL_error`, caught by the surrounding protected call → an error result instead of a hung game thread). The budget resets at the entry of each `Execute*`/`CallFunction`, so one long-running script can't starve later ones. Default `kDefaultInstructionLimit = 10'000'000` instructions; `0` disables.
- **`SOL_ALL_SAFETIES_ON`** (PUBLIC compile definition on `ScriptLua`) turns on sol2's runtime type checks. Only `lua_vm.*` and its consumers include sol2 today, so no ODR risk.

## Follow-up seams (binding layer, `engine/runtime/script/`)

- **`package.path` from the project script dir** — `core/config/path.h::GetScriptDirectory()` already returns `asset/script/`; root `package.path` there so `require "module"` resolves to project scripts. Deliberately *not* in `LuaVM` (it would break the engine-agnostic layer).
- **Engine bindings** — expose actors/components/events to Lua (sol2 `usertype`, `new_usertype`), likely in a `ScriptSystem` owned by `RuntimeContext` alongside the other systems.
- **Scripts as assets** — load through the asset/resource pipeline (`AssetLoadRequest` / async queue) instead of `ExecuteFile` hitting disk directly.
- **Hot reload** — `Shutdown` + `Initialize` on a `LuaVM` already supports reload; the binding layer decides when and how state is preserved.

## Threading

One `LuaVM` per game thread. A Lua state is not thread-safe; concurrent access from multiple threads must be serialized externally. The engine's game thread is the owner of script execution.
