# Frozen protocol: source-ID support and merged-copy diagnostic

Status: frozen before implementation/outcome inspection, E-STITCH-object-support-01, 10.10.2026. Parent fixture and RGB coded-target outcomes remain unchanged: [[../validation/OBJECT_STITCH]].

## Question

Can per-camera object identity maps reveal target source support outside the direct-view target mask when the final RGB classifier reports one connected component? This addresses the known blind spot where a ghost copy may merge with the main object.

## Inputs and fixed geometry

- Use the immutable checked-in `tests/data/object_stitch_v1` fixture, which contains source-camera ID maps, direct-view object-ID truth, RGB, depth, calibration, and pose metadata with hash verification.
- Reuse exactly the existing 84 carrier × fusion × frame cases, camera projections, weights, target object ID, and direct Blender truth.
- For output pixel `p`, project each camera's integer ID map to its nearest source pixel at the carrier world point. Define support `s(p) = Σ_i w_i(p) · 1[id_i(p)=target_id]`, using the renderer's returned normalized pointwise camera weights.
- Compare `s(p)` with direct-view target mask `g(p)`. Report thresholded masks at fixed support thresholds 0.25, 0.50, and 0.75, plus mean support within truth, support outside truth divided by total support, and total support mass divided by truth area. Do not select thresholds from observed results.
- Preserve the existing RGB classifier results as a separate measurement. Do not merge support-derived metrics into RGB quality or call them pixel truth for multi-band output: multi-band blending's effective frequency-dependent weights differ from the returned pointwise provenance weights.

## Interpretation limits

This is an identity-provenance diagnostic, not a natural-object ghost detector. Source ID maps are generated from ideal opaque ray casting and nearest-pixel projection; they omit radiometric effects and do not reconstruct exact source contribution after filtering. Support outside direct truth is a candidate ghost/occlusion mismatch, while support inside truth does not prove faithful RGB appearance. The experiment remains one synthetic coded object, two frames, one view, and one fixture; no algorithm ranking or target-platform timing follows.

## Known-answer tests

Synthetic support maps must yield: exact truth support → inside support 1 and outside fraction 0; equal duplicated support in a disjoint region → nonzero outside fraction; no support → zero total support and undefined outside fraction; partial overlap → threshold metrics matching hand-computed masks. These controls validate metric semantics only.
