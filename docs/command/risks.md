# Command Transport Risks and Limits

The initial C6.1 transport is a development tool, disabled by default, and
listens only on IPv4 loopback. Loopback is a local-user trust boundary, not
authentication: another process under the same OS user can issue any command
whose Agent flag and configured capabilities allow it. Do not enable it for a
shipped or remotely exposed build.

It accepts one client at a time, one JSON-lines request at a time. Defaults are
64 KiB per request, 128 inbound/outbound entries, and 32 requests drained per
game tick. A full inbound queue receives a structured `busy` response. A client
disconnect does not forcibly cancel work already accepted by the registry; a
later remote-control phase needs explicit client/session cancellation policy.

The transport has an automated loopback/`GameTick` handoff test and a manual
live-GPU screenshot smoke test. Deterministic capture inputs and visual-image
comparison are still deferred, so this is a debugging path rather than a
visual-regression framework.

Lua command access is in-process and has no transport authentication boundary.
It is restricted to the designated Game/Lua thread and the existing Lua
sandbox/instruction budget. It is a developer workflow, not a replacement for
the loopback agent endpoint; project-script asset loading and a ScriptSystem
remain separate work.
