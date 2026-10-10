# Frozen protocol: coded target moving across camera overlaps

Status: frozen before the capture and metric implementation, E-STITCH-object-motion-01, 10.10.2026. This is a next-stage synthetic screen. Existing static target and source-support results stay in [[../validation/OBJECT_STITCH]].

## Question and boundary

Measure how per-frame source-ID support and final RGB target shape change while a known coded object crosses the surround-view camera sectors. This tests cross-camera duplication under motion. It does **not** measure temporal persistence/lag: each frame is rendered from synchronous camera inputs and the current fusion renderer has no temporal history.

## Locked scene and capture

- Start from `assets/scenarios/object-stitch-v1.json`, preserving seed 15, mounts, street geometry, target material and camera calibration.
- Use nine timestamps at frame indices 0, 3, …, 24 (10 Hz sampling at 30 fps), with vehicle translation inherited from `vehicle_pose`.
- Animate the coded target linearly through the nine world positions in `assets/scenarios/object-stitch-motion-v1.json`; target height and shape stay fixed. Capture source cameras and direct RGB/object-ID truth at each same timestamp.
- Keep camera resolution, surface/fusion matrix, target threshold and object-support definitions fixed. Run all carriers and fusion modes; report each timestamp separately and summarize within this one trajectory only.
- Verify exact capture recipe, frame count/timestamps/poses, target positions, input hashes, and code/protocol hashes before analysis. A capture mismatch invalidates the run.

## Outcomes and interpretation

For each frame report final RGB target IoU/recall/precision, the fixed-threshold source-support metrics from [[STITCH_OBJECT_SUPPORT_PROTOCOL]], and false source support outside direct truth. Do not pool adjacent timestamps as independent observations. A trend across the path is descriptive and cannot rank algorithms beyond this target/scene/trajectory.

No optical-flow compensation, camera exposure/noise, rolling shutter, target occlusion by another moving object, or temporal state is introduced. To test true stale trails later, add a separate asynchronous/delayed-camera or renderer-history condition with a frozen delay and moving-object truth; do not infer that property from this synchronous capture.
