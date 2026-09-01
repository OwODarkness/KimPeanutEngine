# Render Capture Overview

Render capture exposes a narrow SceneColor request/result boundary. Render owns
the capture policy and CPU image completion; Runtime owns explicit PNG-path
policy and export; Graphics/backends implement readback below the common
contract.

Read the [capture plans](PLANS.md), [capture roadmap](TODO.md), and parent [usage](../usage.md)
for the live agent/debug workflow.
