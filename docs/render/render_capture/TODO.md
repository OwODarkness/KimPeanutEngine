# Render Capture TODO

**Status: active.** Architecture: [PLANS.md](PLANS.md). Concrete stage designs
are in [`.plan/`](.plan/). Runtime owns screenshot paths/export, Render owns
semantic view policy/conversion, and Graphics owns GPU readback.

## Stages

- [x] [C1 — capture service and pixel callback policy](.plan/C1.md)
- [x] [C2 — common-RHI readback contract](.plan/C2.md)
- [x] [C3 — Vulkan/OpenGL implementations](.plan/C3.md)
- [x] [C4 — PNG export and visual smoke evidence](.plan/C4.md)

## Deferred follow-ups

- [ ] Support multiple concurrent or cancellable capture jobs only after a real
  consumer requires them.
- [ ] Add HDR/EXR export only with an explicit output consumer and readback
  format contract.
- [ ] Add image-difference baselines after the capture artifact is trusted.

## Evidence

See the relevant [execution journal](../../../.spec/journal/render-deferred-pbr.md)
for current Render capture integration evidence.
