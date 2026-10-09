# E-STITCH-01 v1 input fixture

This compact, checked-in fixture contains one synchronized four-camera RGB/depth frame exported from the authored Blender metric-street scene (`assets/scenes/metric-street/street.blend`). It is a synthetic exploratory input, not a real-world dataset or independent holdout. RGB frames are 400×400 PPM; radial range maps are float32 NPY in metres. `manifest.json` and `ground_truth.json` carry SHA-256 checksums used by the runner.

Reproduce the 84-case exploratory matrix from the repository root:

```sh
python3 tools/run_e_stitch_01.py \
  --config tests/data/e_stitch_01_v1/config.json \
  --dataset tests/data/e_stitch_01_v1 \
  --output artifacts/e-stitch-01-v2
```

See [validation report](../../../docs/validation/E_STITCH_01_V2.md) for scope, aggregate results, caveats, and source hashes.
