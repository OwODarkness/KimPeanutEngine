# Native runtime fixture

This fixture is the checked-in multi-material runtime contract used by
`NativeModelRuntimeIntegrationTest`. The two Material sources are hashed and
published as test-owned entries under the local `.archive` directory during
the test; the test then serializes a deterministic two-section native Model
that refers to those hashes and removes the entries before returning.

`multi_material.level` uses the logical model key that a packaged Level would
author. The runtime integration test loads the resulting native Model directly
through `AssetManager` so it does not require a mutable archive database or
depend on importer code.
