#!/usr/bin/env python3
"""Compare carrier/fusion variants against Blender's per-camera depth truth."""
import argparse
import copy
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image

from compare_surfaces import MODES, SURFACES
from depth_truth import _sample_depth
from reference import render


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def checked_file(root, name, hashes):
    path = (root / name).resolve()
    if not path.is_relative_to(root.resolve()) or not path.is_file():
        raise ValueError(f"unsafe or missing dataset file: {name}")
    expected = hashes.get(name)
    if not expected or sha256(path) != expected:
        raise ValueError(f"missing checksum or checksum mismatch: {name}")
    return path


def project(camera, points):
    transform = np.asarray(camera["T_camera_from_vehicle"], dtype=float)
    camera_points = points @ transform[:3, :3].T + transform[:3, 3]
    x, y, z = np.moveaxis(camera_points, -1, 0)
    rho = np.hypot(x, y)
    theta = np.arctan2(rho, z)
    projection = camera["projection"]
    distorted = theta.copy()
    for index, coefficient in enumerate(projection["k"]):
        distorted += coefficient * theta ** (2 * index + 3)
    scale = np.divide(distorted, rho, out=np.zeros_like(rho), where=rho > 1e-14)
    uv = np.stack((projection["fx"] * x * scale + projection["cx"],
                   projection["fy"] * y * scale + projection["cy"]), axis=-1)
    resolution = camera["resolution"]
    valid = ((z > projection["z_epsilon_m"])
             & (theta <= projection["theta_max_rad"])
             & (uv[..., 0] >= 0) & (uv[..., 0] <= resolution["width"] - 1)
             & (uv[..., 1] >= 0) & (uv[..., 1] <= resolution["height"] - 1))
    distance = np.linalg.norm(camera_points, axis=-1)
    return uv, distance, valid


def evaluate(config, images, depth_maps, tolerance_m=0.05, tolerance_fraction=0.01):
    result = render(config, images)
    roi = result["evaluation"]
    weights = result["weights"]
    projected = weights.sum(axis=-1) > 1e-8
    camera_states = []
    for camera, depth in zip(sorted(config["cameras"], key=lambda item: item["id"]), depth_maps):
        uv, distance, projectable = project(camera, result["world"])
        sampled = _sample_depth(depth, uv, invalid_value=1e9)
        tolerance = np.maximum(tolerance_m, tolerance_fraction * distance)
        matched = projectable & np.isfinite(sampled) & (np.abs(sampled - distance) <= tolerance)
        occluded = projectable & np.isfinite(sampled) & (sampled < distance - tolerance)
        target_before_surface = projectable & np.isfinite(sampled) & (sampled > distance + tolerance)
        no_return = projectable & ~np.isfinite(sampled)
        camera_states.append((matched, occluded, target_before_surface, no_return))

    matched = np.stack([state[0] for state in camera_states], axis=-1)
    occluded = np.stack([state[1] for state in camera_states], axis=-1)
    before_surface = np.stack([state[2] for state in camera_states], axis=-1)
    no_return = np.stack([state[3] for state in camera_states], axis=-1)
    any_match = matched.any(axis=-1)
    exact_weight = np.sum(weights * matched, axis=-1)
    occluded_weight = np.sum(weights * occluded, axis=-1)
    before_weight = np.sum(weights * before_surface, axis=-1)
    no_return_weight = np.sum(weights * no_return, axis=-1)
    contributed_weight = np.sum(weights, axis=-1)
    roi_pixels = int(roi.sum())
    active_roi = roi & projected
    active_weight = contributed_weight[active_roi]
    active_pixels = int(active_roi.sum())
    winner = np.argmax(weights, axis=-1)
    winning_match = np.take_along_axis(matched, winner[..., None], axis=-1)[..., 0]
    active_winners = active_roi

    def fraction(mask):
        return float(mask[roi].mean()) if roi_pixels else None

    def contribution_fraction(category):
        return float(category[active_roi].sum() / active_weight.sum()) if active_weight.size and active_weight.sum() else None

    categories = np.stack((exact_weight, occluded_weight, before_weight, no_return_weight), axis=-1)
    categories = np.divide(categories, contributed_weight[..., None],
                           out=np.zeros_like(categories), where=contributed_weight[..., None] > 1e-8)
    # Red=foreground occlusion, green=depth match, blue=point before first hit;
    # no-return weight is added equally to RGB (white). Outside ROI stays black.
    diagnostic = np.stack((categories[..., 1] + categories[..., 3],
                           categories[..., 0] + categories[..., 3],
                           categories[..., 2] + categories[..., 3]), axis=-1)
    diagnostic = np.where(roi[..., None], diagnostic, 0)
    diagnostic = np.uint8(np.rint(np.clip(diagnostic, 0, 1) * 255))
    return {
        "roi_pixels": roi_pixels,
        "projected_pixels": active_pixels,
        "projected_coverage_fraction": fraction(projected),
        "exact_surface_coverage_fraction": fraction(any_match),
        "mean_exact_visible_cameras_per_roi_pixel": float(matched[roi].sum(axis=-1).mean()) if roi_pixels else None,
        "weighted_contribution_fraction": {
            "matches_depth_surface": contribution_fraction(exact_weight),
            "comes_from_foreground_occluder": contribution_fraction(occluded_weight),
            "point_lies_before_first_surface": contribution_fraction(before_weight),
            "depth_has_no_return": contribution_fraction(no_return_weight),
        },
        "max_weight_camera_depth_match_fraction": float(winning_match[active_winners].mean()) if active_pixels else None,
        "coverage_histogram_projection": [int(((result["coverage"] == count) & roi).sum()) for count in range(5)],
        "tolerance": {"absolute_m": tolerance_m, "relative": tolerance_fraction},
        "limitations": [
            "single static Blender frame and one synthetic world",
            "depth match is a point visibility/occlusion proxy, not a photometric seam or ghosting metric",
            "depth maps use finite cube-face rasterization and depth interpolation",
            "carrier ROI differs by shape and excludes only the configured vehicle footprint",
            "reference fusion is the independent analytic CPU implementation, not a server readback",
        ],
    }, diagnostic


def compare(dataset, output, frame_index, carriers, views, modes, tolerance_m, tolerance_fraction):
    dataset = Path(dataset).resolve()
    config_path = dataset / "config.json"
    manifest_path = dataset / "manifest.json"
    truth_path = dataset / "ground_truth.json"
    if not all(path.is_file() for path in (config_path, manifest_path, truth_path)):
        raise ValueError("dataset must contain config.json, manifest.json and ground_truth.json")
    config = json.loads(config_path.read_text())
    manifest = json.loads(manifest_path.read_text())
    truth = json.loads(truth_path.read_text())
    if frame_index >= len(manifest["frames"]) or frame_index >= len(truth["depth_truth"]["frames"]):
        raise ValueError("frame index outside RGB/depth truth manifests")
    if truth["depth_truth"]["meaning"] != "radial range from camera optical center, metres":
        raise ValueError("unsupported depth truth meaning")
    if truth.get("true_config") != config:
        raise ValueError("RGB calibration config and depth-truth calibration differ")
    camera_ids = [camera["calibration_id"] for camera in sorted(config["cameras"], key=lambda item: item["id"])]
    if manifest.get("calibration_ids") != camera_ids:
        raise ValueError("RGB manifest calibration IDs do not match config")

    frame = manifest["frames"][frame_index]
    depth_frame = truth["depth_truth"]["frames"][frame_index]
    if frame.get("offset_ns", [0, 0, 0, 0]) != [0, 0, 0, 0]:
        raise ValueError("visibility comparison requires four synchronous camera frames")
    if frame["scenario_timestamp_ns"] != depth_frame["scenario_timestamp_ns"]:
        raise ValueError("RGB and depth-truth timestamps differ")
    images, depths = [], []
    for camera_id in range(4):
        image_name = frame["paths"][camera_id]
        image_path = checked_file(dataset, image_name, manifest["sha256"])
        images.append(np.asarray(Image.open(image_path).convert("RGB"), dtype=float))
        depth_name = depth_frame["files"][camera_id]
        depth_path = checked_file(dataset, depth_name, truth["depth_truth"]["sha256"])
        depth = np.load(depth_path, allow_pickle=False)
        camera = sorted(config["cameras"], key=lambda item: item["id"])[camera_id]
        expected_shape = (camera["resolution"]["height"], camera["resolution"]["width"])
        if depth.shape != expected_shape:
            raise ValueError(f"depth dimensions do not match camera {camera_id}")
        depths.append(depth)
    if [image.shape[:2] for image in images] != [
            (camera["resolution"]["height"], camera["resolution"]["width"])
            for camera in sorted(config["cameras"], key=lambda item: item["id"])]:
        raise ValueError("RGB dimensions do not match calibration")

    destination = Path(output).resolve()
    destination.mkdir(parents=True, exist_ok=False)
    rows = []
    diagnostic_name = None
    for carrier in carriers:
        for view, elevation in views:
            for mode in modes:
                case = copy.deepcopy(config)
                case["surface"] = SURFACES[carrier]
                case["virtual_camera"].update(azimuth_rad=0.8, elevation_rad=elevation, distance_m=8.5)
                case["fusion"] = dict(mode=mode, edge_width_px=24.0, angle_power=2.0)
                metrics, diagnostic = evaluate(case, images, depths, tolerance_m, tolerance_fraction)
                rows.append({"carrier": carrier, "view": view, "mode": mode, "metrics": metrics})
                if (carrier, view, mode) == ("dome", "low", "edge_feather"):
                    diagnostic_name = "visibility-dome-low-edge_feather.png"
                    Image.fromarray(diagnostic).save(destination / diagnostic_name)
                print(f"{carrier}-{view}-{mode}", flush=True)

    report = {
        "schema_version": 1,
        "suite_id": "blender-depth-visibility-stitching-v1",
        "scope": "static geometric visibility and blend-weight screening; not a complete seam-quality benchmark",
        "dataset": {
            "root_label": dataset.name,
            "capture_sha256": truth.get("capture_sha256"),
            "config_sha256": sha256(config_path),
            "manifest_sha256": sha256(manifest_path),
            "ground_truth_sha256": sha256(truth_path),
            "depth_hashes": {name: truth["depth_truth"]["sha256"][name]
                             for name in truth["depth_truth"]["frames"][frame_index]["files"]},
            "rgb_hashes": {name: manifest["sha256"][name] for name in frame["paths"]},
        },
        "implementation_sha256": {
            "compare_visibility.py": sha256(Path(__file__).resolve()),
            "reference.py": sha256(Path(__file__).with_name("reference.py")),
            "compare_surfaces.py": sha256(Path(__file__).with_name("compare_surfaces.py")),
            "depth_truth.py": sha256(Path(__file__).with_name("depth_truth.py")),
        },
        "frame_index": frame_index,
        "scenario_timestamp_ns": frame["scenario_timestamp_ns"],
        "diagnostic_image": diagnostic_name,
        "rows": rows,
    }
    (destination / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    lines = [
        "# Blender depth-truth visibility screening",
        "",
        "One synchronous RGB/depth frame; shared camera rig and output views. This compares whether a camera's sampled pixel sees the exact 3D carrier point, using the exported radial range as an occlusion oracle.",
        "It is a geometric visibility proxy, not a photometric seam/ghosting score or final ranking.",
        "",
        f"Capture SHA-256: `{report['dataset']['capture_sha256']}`  ",
        f"Config SHA-256: `{report['dataset']['config_sha256']}`  ",
        f"Manifest SHA-256: `{report['dataset']['manifest_sha256']}`",
        "",
        "| Carrier | View | Fusion | Projection coverage | Exact 3D-point visible coverage | Blend weight on matching depth | On foreground occluder | Point before first hit | Max-weight camera matches depth |",
        "|---|---|---|---:|---:|---:|---:|---:|---:|",
    ]
    for row in rows:
        m = row["metrics"]
        w = m["weighted_contribution_fraction"]
        f = lambda key: "n/a" if w[key] is None else f"{100*w[key]:.2f}%"
        winner_fraction = m["max_weight_camera_depth_match_fraction"]
        winner_text = "n/a" if winner_fraction is None else f"{100*winner_fraction:.2f}%"
        lines.append(
            f"| {row['carrier']} | {row['view']} | {row['mode']} | "
            f"{100*m['projected_coverage_fraction']:.2f}% | {100*m['exact_surface_coverage_fraction']:.2f}% | "
            f"{f('matches_depth_surface')} | {f('comes_from_foreground_occluder')} | "
            f"{f('point_lies_before_first_surface')} | "
            f"{winner_text} |"
        )
    lines += [
        "",
        "Exact visibility requires `|measured radial depth − camera-to-carrier-point range| ≤ max(0.05 m, 1% of range)`. Non-matching weighted samples are split into a nearer foreground occluder, a first surface behind the queried point, and no depth return.",
        "Per-camera and full-histogram data plus limitations are in `report.json`.",
    ]
    if diagnostic_name:
        lines += [
            "",
            f"![Доли веса по карте глубины]({diagnostic_name})",
            "",
            "Красный — camera sample попал на поверхность ближе carrier point; зелёный — глубина совпала; синий — carrier point находится перед первым depth hit; белый — в depth truth нет возврата. Смеси цветов показывают смешанные доли веса fusion.",
        ]
    lines.append("")
    (destination / "REPORT.md").write_text("\n".join(lines))
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dataset", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--frame", type=int, default=0)
    parser.add_argument("--carriers", nargs="+", choices=list(SURFACES), default=list(SURFACES))
    parser.add_argument("--views", nargs="+", choices=("oblique", "low"), default=("oblique", "low"))
    parser.add_argument("--modes", nargs="+", choices=MODES, default=MODES)
    parser.add_argument("--depth-tolerance-m", type=float, default=0.05)
    parser.add_argument("--depth-tolerance-fraction", type=float, default=0.01)
    args = parser.parse_args()
    if args.frame < 0 or args.depth_tolerance_m <= 0 or args.depth_tolerance_fraction < 0:
        parser.error("frame must be nonnegative and depth tolerances must be valid")
    selected_views = [(name, 1.0 if name == "oblique" else 0.35) for name in args.views]
    compare(args.dataset, args.output, args.frame, args.carriers, selected_views, args.modes,
            args.depth_tolerance_m, args.depth_tolerance_fraction)
