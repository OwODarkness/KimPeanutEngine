# RelWithDebInfo GUI launch correction — 2026-09-27

User requested that an agent's optimized engine launch display the GUI without
an accompanying terminal. The root engine target previously used the default
Windows console subsystem.

Changed root `CMakeLists.txt` for MSVC RelWithDebInfo only: generator-controlled
`WIN32_EXECUTABLE` selects the GUI subsystem and `mainCRTStartup` retains the
existing `main(argc, argv)` with CRT initialization. Debug remains console
subsystem. No renderer, GPU lifetime, dependency or common API change; other
targets and configurations retain their existing behavior. Added interactive
launch guidance to `AGENTS.md`; unrelated profiler work was preserved.

References checked: [CMake WIN32_EXECUTABLE](https://cmake.org/cmake/help/latest/prop_tgt/WIN32_EXECUTABLE.html)
and [MSVC CRT entry points](https://learn.microsoft.com/en-us/cpp/build/reference/entry-entry-point-symbol?view=msvc-170).

Validation:

- `tools/kp.ps1 configure` passed.
- `tools/kp.ps1 -Configuration RelWithDebInfo build KimPeanutEngine` passed;
  existing LNK4098 default-library warning remains.
- `tools/kp.ps1 -Configuration Debug build KimPeanutEngine` passed.
- Inspected rebuilt PE headers: RelWithDebInfo subsystem 2 (GUI), Debug
  subsystem 3 (console).
- Launched rebuilt RelWithDebInfo Vulkan with `level/cornell_box.level` and
  `--agent-port 37373`, using a normal shell launch. Process was responsive
  and created its GLFW editor window. CLI fixture selection and deferred
  Runtime screenshot execute/poll succeeded.
- Scene capture `save/screenshots/validation/2026-09-27-relwithdebinfo-gui-scene.png`
  was visually inspected and contains the correctly rendered Cornell scene.
  The first screen-based engine-window capture was white and is not accepted
  as visual evidence; that capture reads screen pixels and can include occlusion.
- Run log `save/logs/2026-09-27/KimPeanutEngineLog-2026.09.27-16.18.36.txt`
  contained no error, VUID or device-loss match at inspection.
- `git diff --check` passed with LF/CRLF notices. No unit tests were run for
  this target launch setting; no performance measurement was made.

The smoke instance exited after a WM_CLOSE request to its process-owned GLFW
window. `Process.MainWindowHandle` initially selected NVIDIA helper windows;
closing those did not close the engine. Recorded the correct GLFW window
selection rule in `AGENTS.md` for agent focus/close operations.
Non-MSVC and other optimized configurations are outside this narrow fix.
GUI launches have no dedicated stdout/stderr console; normal runtime file
logging and the command transport remain available. Early stderr-only argument
diagnostics are therefore not visible in a standalone console-free launch.

## Correction: private sandbox desktop and verified foreground focus

The earlier GUI launch was made from `CodexSandboxDesktop-80998432b20e3d79d13108b3a8e38653`.
Its window/capture existed there, but that desktop was not visible to the user.
The previous claim that the editor was brought to the user's foreground was
incorrect; a successful capture or visible flag does not establish that.

Closed the sandbox retry (PID 7756) through its GLFW window. Relaunched the
RelWithDebInfo Vulkan Sponza executable via approved execution outside the
sandbox (PID 33000). Verification reported desktop `Default`, visible GLFW
HWND 460898. A simple SetForegroundWindow still returned false. Briefly attaching
the current/foreground/target input threads for activation, then detaching them,
succeeded: GetForegroundWindow was exactly 460898. The engine is left running
on the user's desktop. No FPS measurement or render change was made.

AGENTS.md now requires a user-visible launch outside the private desktop and
actual foreground-handle verification. git diff --check passes with LF/CRLF
notices. The GUI subsystem fix remains valid, but it alone cannot move a
sandbox-created window onto the user's desktop.
