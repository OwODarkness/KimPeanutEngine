# Command Transport Lifecycle

`Engine::Initialize()` starts the local transport only when startup enables it.
The listener is bound to `127.0.0.1`; start waits for bind/listen success so a
port conflict is logged at boot rather than discovered by a client later.

```text
I/O worker: accept/read JSON line -> bounded inbound queue
GameTick:   drain queue -> CommandAgentEndpoint -> CommandRegistry
I/O worker: bounded outbound queue -> JSON-line response
```

The worker never invokes a command handler. `GameTick` first drains transport
requests and then pumps Registry Game-lane work. A Game command therefore
returns `Pending` to the initial request and keeps its ordinary completion
mechanism; the client polls the request ID for its terminal result.

On Engine shutdown, `Run()` stops and joins the worker before waking/joining the
render thread and before `RuntimeContext::Clear()` shuts down the registry. The
transport destructor and `Engine::Clear()` also make stop idempotent.

`RuntimeContext::FinalizeGameStartup()` constructs `LuaCommandBridge` on the
Game thread after the generic VM has initialized. The bridge records that
thread and rejects command calls made from any other thread; `LuaVM` itself is
not thread-safe. During `RuntimeContext::Clear()`, the bridge removes every
`engine.command.*` closure before the registry or Lua state are released.
