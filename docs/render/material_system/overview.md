# Material System Overview

The Material System is Render-owned. It provides generational template and
instance identities, validates typed overrides, resolves render-ready static
resources privately, and produces frame-local bindings for draws.

It does not own Asset CPU lifetime or Graphics GPU lifetime. Read the
[material plans](PLANS.md) and [material roadmap](TODO.md) for V1 data model
and implementation status.
