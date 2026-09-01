# Deferred PBR Overview

Deferred PBR is a Render submodule for the staged cross-backend opaque PBR
renderer. It owns the pass policy for G-buffer generation, shadow production
and consumption, deferred HDR lighting, tone mapping, semantic debug views,
and environment lighting.

Read the [architecture plans](PLANS.md), [roadmap](TODO.md), and concrete
[D-stage plans](.plan/) before implementation. D6.4 runtime evidence and the
fixed-budget profile hook are complete; numeric warm-runtime profile samples,
true cube resources, and multiple punctual jobs remain deferred. Detailed
execution history is in the central
[deferred-PBR journal](../../../.spec/journal/render-deferred-pbr.md).
