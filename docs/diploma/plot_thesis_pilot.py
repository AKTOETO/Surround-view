"""Audit and illustrate the bounded native pilot; no product algorithms or quality oracle."""
import argparse
import hashlib
import json
import struct
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from PIL import Image


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--scenario", type=Path, required=True)
    parser.add_argument("--figure", type=Path, required=True)
    parser.add_argument("--audit", type=Path, required=True)
    args = parser.parse_args()
    report = json.loads(args.report.read_text())
    scenario = json.loads(args.scenario.read_text())
    manifest = json.loads(args.manifest.read_text())
    config = json.loads(args.config.read_text())
    assert report["success"] and report["restored"]
    assert not report["cursor_restored"]
    assert report["scenario_input_sha256"] == sha(args.scenario)
    assert len(report["samples"]) == 54 and len(report["scenario"]["variants"]) == 6
    assert scenario["frames"] == 1 and scenario["warmup"] == 2 and scenario["repeats"] == 7
    baseline = report["frame_baselines"][0]["metadata"]
    assert baseline["width"] == config["output"]["width"] == 320
    assert baseline["height"] == config["output"]["height"] == 180
    assert report["initial_state"]["surface"] == config["surface"]
    assert all(baseline["view"][key] == value for key, value in config["virtual_camera"].items())
    assert manifest["frames"][0]["scenario_timestamp_ns"] == "0"
    fields = ("camera_id", "calibration_id", "source_timestamp_ns", "image_identity", "camera_model_sha256")
    identities = [{key: item[key] for key in fields} for item in baseline["inputs"]]
    assert len(identities) == 4 and all(item["used"] for item in baseline["inputs"])
    for index, item in enumerate(identities):
        assert item["camera_id"] == index
        assert item["calibration_id"] == config["cameras"][index]["calibration_id"]
        assert item["source_timestamp_ns"] == "0"
    input_hashes = []
    for index, filename in enumerate(manifest["frames"][0]["paths"]):
        path = args.manifest.parent / filename
        with Image.open(path) as image:
            packed = image.convert("RGB").tobytes()
            content = b"sv-packed-rgb8-v1\0" + struct.pack(">II", image.width, image.height) + packed
        assert hashlib.sha256(content).hexdigest() == identities[index]["image_identity"]["sha256"]
        input_hashes.append({"path": str(path), "file_sha256": sha(path)})
    assert report["baseline_rgba_sha256"] == report["restored_rgba_sha256"]
    captures = Path(str(args.report) + ".frames")
    rows = []
    fig, axes = plt.subplots(2, 3, figsize=(12, 5.3), constrained_layout=True)
    for variant, (carrier, method) in enumerate(
        (carrier, method) for carrier in ("plane", "dome_floor")
        for method in ("hard_best_angle", "edge_feather", "multi_band")
    ):
        samples = [s for s in report["samples"] if s["variant"] == variant]
        assert len(samples) == 9 and sum(not s["warmup"] for s in samples) == 7
        assert len({s["rgba_sha256"] for s in samples}) == 1
        for sample in samples:
            meta = sample["metadata"]
            assert [{key: item[key] for key in fields} for item in meta["inputs"]] == identities
            assert meta["view"] == baseline["view"] and meta["frame_set_id"] == baseline["frame_set_id"]
            assert meta["fusion_mode"] == method and meta["health"] == "READY" and meta["paused"]
            assert 3190 <= meta["mesh_triangles"] <= 3210
            assert meta["mesh_resources"]["active"]["buffer_bytes"] <= 60500
            if not sample["warmup"]:
                raw = captures / f'frame-0-variant-{variant}-block-{sample["block"]}.rgba'
                assert sha(raw) == sample["rgba_sha256"]
                assert raw.stat().st_size == meta["width"] * meta["height"] * 4
        first = next(s for s in samples if not s["warmup"])
        meta = first["metadata"]
        raw = captures / f'frame-0-variant-{variant}-block-{first["block"]}.rgba'
        image = Image.frombytes("RGBA", (meta["width"], meta["height"]), raw.read_bytes())
        assert image.getchannel("A").getextrema() == (255, 255)
        axes.flat[variant].imshow(image, interpolation="nearest")
        axes.flat[variant].set_title(f"{carrier} / {method}", fontsize=10)
        axes.flat[variant].axis("off")
        rows.append({"variant": variant, "carrier": carrier, "method": method,
                     "measured": 7, "triangles": meta["mesh_triangles"],
                     "mesh_resources": meta["mesh_resources"], "rgba_sha256": first["rgba_sha256"],
                     "timings": report["summaries"][variant]["timings"]})
    fig.suptitle("Native pilot: same four Blender inputs; no independent quality reference", fontsize=12)
    args.figure.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.figure, dpi=140)
    plt.close(fig)
    audit = {"schema_version": 1, "scope": "workflow_pilot_not_quality_or_performance_acceptance",
             "success": True, "restored": True, "cursor_restored": False,
             "quality_status": "not_requested", "samples": 54, "measured": 42,
             "report": str(args.report), "report_sha256": sha(args.report),
             "config_sha256": sha(args.config), "manifest_sha256": sha(args.manifest),
             "scenario_input_sha256": sha(args.scenario), "scenario_sha256": report["scenario_sha256"],
             "trial_plan_sha256": report["trial_plan_sha256"],
             "gl_renderer": report["catalog"]["gl_renderer"],
             "server_build_passport": report["catalog"]["build_passport"],
             "client_build_passport": report["client_build_passport"],
             "view": baseline["view"], "inputs": identities, "files": input_hashes,
             "variants": rows, "figure_sha256": sha(args.figure)}
    args.audit.parent.mkdir(parents=True, exist_ok=True)
    args.audit.write_text(json.dumps(audit, indent=2) + "\n")
    print("verified 54 trials / 42 captures / 6 stable variants; restore RGB exact")


if __name__ == "__main__":
    main()
