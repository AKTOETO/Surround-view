# Frozen protocol: interleaved coded-object CPU timing

Status: frozen before the follow-up run, E-STITCH-coded-object-timing-02, 10.10.2026. The first coded-object timing screen is retained unchanged in [[../validation/OBJECT_STITCH]]; this follow-up addresses its fixed execution order only.

## Question and scope

Does alternating the execution order across repeated runs reduce fixed-order bias in the measured CPU cost of the existing carrier/fusion combinations? This protocol does not test image quality hypotheses or declare a winning renderer. The existing analytic reference renderer is timed on the same two-frame coded-object fixture; GPU, server, camera capture, GUI, metric calculation and file I/O are excluded.

## Fixed workload and controls

- Use the checked-in `tests/data/object_stitch_v1` fixture, unchanged, including its config, RGB, depth, source-camera object IDs and direct truth. Verify fixture hashes before analysis.
- Keep the declared carriers, all fusion modes, both frames, render dimensions and configuration fixed. Do not normalize or equalize mesh budgets in this follow-up; report their absence as a comparison limitation.
- Run two warmups for every case. Then perform seven timed blocks. Each block contains every case exactly once in a pseudorandom permutation generated from seed `20261010`; record the full order and each raw duration.
- Use one process and one NumPy RNG for scheduling. Do not discard outliers. Report raw samples and descriptive median/p95/max, without inferential claims from repeated measurements of the same two frames.
- Record Python/NumPy versions, OS, machine/processor, logical CPU count, and common thread-control environment variables. Do not claim controlled temperature, exclusive cores, or stable frequency.
- Save all outputs in a new directory; preserve the prior artifact and report source/fixture hashes.

## Interpretation gates

The result can establish that the measured execution order was interleaved and provide a host-specific descriptive timing record. It cannot rank carrier geometry fairly until equal output/mesh/memory budgets are defined, and it cannot establish GPU/server/Aurora latency. Scene, mount and capture independence remain absent. Any future method ranking needs those controls and scene-level repeats.

## Frozen scheduler invariant

For each timed block, the permutation must contain each `(carrier, fusion mode, frame)` key exactly once. Orders are deterministic for the frozen seed, differ between blocks, and are written to the report. A test validates coverage, determinism and block variation before the experiment is run.
