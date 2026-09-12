"""Cross-backend capture comparison for the Live2D validation gate.

Compares two same-dimension RGBA8 captures and splits the report into the
regions the L2D4 tolerance contract distinguishes: non-edge opaque pixels,
partial-alpha overlap, transparent background, and filtered edges. Tolerances
come from the frozen L2D4.0 contract passed on the command line.

Usage:
    python tools/compare_capture_regions.py A.png B.png --tolerance 0.0078431 \
        --edge-tolerance 0.0156863

There is deliberately no blend-mode region. L2D5.3 asked for one, but a
drawable's blend mode is authored inside the .moc3 and the capture carries only
the composited RGBA, so no per-pixel function of the image can recover which
drawable wrote a pixel or under which blend it was written. The regions above
are all derived from alpha topology, which is why they can be derived at all.
Blend-mode coverage is asserted instead from the engine's own model data with
the `live2d.model_report` Runtime command, which reports the loaded product's
normal/additive/multiplicative drawable counts on both backends; comparing
captures can confirm that both backends agree, but it can never establish which
blend modes the fixture exercises.
"""

import argparse
import hashlib
import sys

import numpy as np
from PIL import Image


def load(path):
    with Image.open(path) as image:
        return np.asarray(image.convert("RGBA"), dtype=np.uint8)


def digest(path):
    with open(path, "rb") as handle:
        return hashlib.sha256(handle.read()).hexdigest()


def edge_mask(rgba, threshold=1):
    """Pixels with an alpha discontinuity in their 3x3 neighbourhood."""
    alpha = rgba[:, :, 3].astype(np.int16)
    mask = np.zeros(alpha.shape, dtype=bool)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            if dy == 0 and dx == 0:
                continue
            shifted = np.roll(np.roll(alpha, dy, axis=0), dx, axis=1)
            mask |= np.abs(alpha - shifted) > threshold
    return mask


def region_stats(name, a, b, mask, tolerance):
    count = int(mask.sum())
    if count == 0:
        return f"{name}: no pixels"
    diff = np.abs(a[mask].astype(np.int16) - b[mask].astype(np.int16))
    # Alpha is compared too: an opacity error is a backend disagreement even
    # when the color channels happen to agree.
    max_diff = int(diff.max())
    mean_diff = float(diff.mean())
    over = int((diff > tolerance * 255.0).sum())
    return (
        f"{name}: pixels={count} max_abs={max_diff} mean_abs={mean_diff:.4f} "
        f"channels_over_tolerance={over} "
        f"({'PASS' if max_diff <= tolerance * 255.0 + 0.5 else 'FAIL'})"
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("reference")
    parser.add_argument("candidate")
    parser.add_argument("--tolerance", type=float, default=2.0 / 255.0)
    parser.add_argument("--edge-tolerance", type=float, default=4.0 / 255.0)
    args = parser.parse_args()

    a = load(args.reference)
    b = load(args.candidate)
    if a.shape != b.shape:
        print(f"FAIL dimension mismatch: {a.shape} vs {b.shape}")
        return 1

    print(f"reference: {args.reference}")
    print(f"  sha256 {digest(args.reference)}")
    print(f"candidate: {args.candidate}")
    print(f"  sha256 {digest(args.candidate)}")
    print(f"dimensions: {a.shape[1]}x{a.shape[0]}")
    print(f"identical: {digest(args.reference) == digest(args.candidate)}")

    alpha_a = a[:, :, 3]
    alpha_b = b[:, :, 3]
    edges = edge_mask(a) | edge_mask(b)

    opaque = (alpha_a == 255) & (alpha_b == 255) & ~edges
    # Partial alpha is reported on its own so blend coverage is never hidden by
    # the edge split; its pixels are by construction the edge-adjacent ones.
    partial = ((alpha_a > 0) & (alpha_a < 255)) | ((alpha_b > 0) & (alpha_b < 255))
    transparent = (alpha_a == 0) & (alpha_b == 0)
    filtered_edge = edges & ((alpha_a > 0) | (alpha_b > 0))

    print()
    print(region_stats("non-edge opaque", a, b, opaque, args.tolerance))
    print(region_stats("partial alpha", a, b, partial, args.tolerance))
    print(region_stats("transparent background", a, b, transparent, args.tolerance))
    print(region_stats("filtered edge", a, b, filtered_edge, args.edge_tolerance))
    print()
    print(f"alpha coverage: transparent={int(transparent.sum())} "
          f"partial={int(partial.sum())} opaque={int(opaque.sum())} "
          f"edge={int(filtered_edge.sum())}")

    print()
    diff = np.abs(a.astype(np.int16) - b.astype(np.int16))
    mismatched = int((diff.max(axis=2) > 0).sum())
    total = a.shape[0] * a.shape[1]
    print(f"all pixels: total={total} differing={mismatched} "
          f"({100.0 * mismatched / total:.2f}%) max_abs={int(diff.max())}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
