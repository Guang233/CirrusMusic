#!/usr/bin/env python3
"""Measures PoC renders (raw interleaved stereo float32) against a reference render.

  analyze.py joint REF TEST --at FRAME   gapless joint: length, shift, error at the joint
  analyze.py seek REF TEST --rate R --ms M   seek accuracy: where TEST starts inside REF
"""
import argparse
import sys

import numpy as np


def load(path):
    data = np.fromfile(path, dtype=np.float32)
    return data[0::2]  # left channel is enough


def best_lag(ref, test, centre, radius, n):
    """Lag (in frames, relative to `centre`) where test[:n] matches ref best."""
    seg = test[:n]
    best, best_err = 0, float("inf")
    for lag in range(-radius, radius + 1):
        start = centre + lag
        if start < 0 or start + n > len(ref):
            continue
        err = float(np.mean((ref[start:start + n] - seg) ** 2))
        if err < best_err:
            best, best_err = lag, err
    return best, best_err


def joint(args):
    ref, test = load(args.ref), load(args.test)
    at = args.at
    print(f"length: ref {len(ref)}  test {len(test)}  diff {len(test) - len(ref)}")
    # Alignment after the joint: 0 means no frames were lost or added there.
    lag, _ = best_lag(ref, test[at + 4096:], at + 4096, 64, 4096)
    print(f"shift after the joint: {lag} frames")
    n = min(len(ref), len(test))
    diff = np.abs(ref[:n] - test[:n])
    w = 2048
    near = float(diff[max(0, at - w):at + w].max())
    away = np.concatenate([diff[4096:max(4096, at - w)], diff[at + w:n - 4096]])
    print(f"max error near the joint {near:.5f}, elsewhere {float(away.max()):.5f} "
          f"(rms {float(np.sqrt(np.mean(away ** 2))):.5f})")
    return 0 if lag == 0 and len(test) == len(ref) and near <= away.max() * 1.5 + 1e-4 else 1


def seek(args):
    ref, test = load(args.ref), load(args.test)
    centre = args.ms * args.rate // 1000
    lag, err = best_lag(ref, test, centre, args.rate // 10, args.rate)
    print(f"seek to {args.ms} ms landed {lag:+d} frames ({lag * 1000 / args.rate:+.3f} ms) "
          f"off, residual {err:.2e}")
    return 0 if abs(lag) * 1000 / args.rate < 10 else 1


def main():
    p = argparse.ArgumentParser()
    p.add_argument("mode", choices=["joint", "seek"])
    p.add_argument("ref")
    p.add_argument("test")
    p.add_argument("--at", type=int, default=0)
    p.add_argument("--rate", type=int, default=44100)
    p.add_argument("--ms", type=int, default=0)
    a = p.parse_args()
    return joint(a) if a.mode == "joint" else seek(a)


if __name__ == "__main__":
    sys.exit(main())
