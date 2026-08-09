---
description: Run all unit test suites (GoogleTest via ctest)
---

Run the unit test suites:

1. `cmake --build build --config Debug` (if the build is stale).
2. `ctest --test-dir build -C Debug`.
3. Report pass/fail per suite. If a suite fails, quote the failing assertion and name the test case.

Targets live under `engine/test/unit` and are registered with `gtest_discover_tests`. To run one suite by name: `ctest --test-dir build -C Debug -R <target>`.
