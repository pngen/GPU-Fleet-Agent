# Contributing to GPU Fleet Agent

Thank you for your interest in contributing to GPU Fleet Agent.

By contributing to this repository you agree that your contributions are
licensed to the project under the terms of the Apache License, Version 2.0.
There is no Contributor License Agreement (CLA) requirement. You retain
copyright over your contributions and grant the project the license stated
in the LICENSE file.

## License and attribution

This repository is distributed under the Apache License, Version 2.0 (see
the LICENSE file at the repository root). The NOTICE file at the repository
root contains the required attribution notices. Ensure that any material
you add that carries its own copyright or attribution does not conflict
with the terms of the Apache-2.0 license or the NOTICE file.

## Engineering standards

- The toolchain is C++20 with CMake, MSVC on Windows, and CUDA 13.1 where
  accelerator-backed code is involved. Code must compile cleanly in both
  Release and Debug with the project warning settings.
- Deterministic, explicit state transitions are required. Do not introduce
  hidden global mutable state, implied ordering, or time-dependent behavior
  that cannot be driven deterministically in tests.
- Strong typed identities and separately typed generations must be used for
  all authority, identity, and freshness values.
- Runtime freshness uses real clock-based semantics as part of the product
  behavior; tests drive clocks deterministically instead of relying on
  wall-clock sleeps as substitutes for correct synchronization.
- Do not hold global or master locks across CUDA, NVML, socket, filesystem,
  persistence, or external callback calls. Use deterministic lock ordering
  and document it.
- Aggregate accounting must be exact and cross-checked against canonical
  state. Reject impossible or negative transitions.

## Build and test

See the README for the canonical build and test workflow. Before opening a
pull request, ensure a clean Release build, a clean Debug build, and a full
deterministic, property, concurrency, adversarial, persistence, recovery,
protocol, multiprocess, packaging, and hardware-backed test closure pass
with zero compiler warnings.

## Code review

Prefer small, focused changes. Every behavioral change should be
accompanied by a deterministic test that would fail without the change.

## Reporting issues

Please include the version, the platform (OS, compiler, CUDA toolkit
version), a minimal reproduction, and the expected versus actual behavior.
