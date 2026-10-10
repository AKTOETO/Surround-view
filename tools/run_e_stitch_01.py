#!/usr/bin/env python3
"""Execute the exploratory E-STITCH-01 carrier/fusion matrix.

Compares 6 carriers (plane, bowl, dome+floor, cylinder+floor, cube, burger-like)
x 2 virtual views (oblique, low)
x 7 fusion strategies (hard_best_angle, edge_feather, angular_feather,
                       seam_distance_feather, graph_cut_seam, multi_band, graph_cut_multi_band).
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import time

import numpy as np
from PIL import Image

from fusion import FUSION_MODES
from reference import intersect, rays, sample
from stitch_metrics import compute_depth_consistency, compute_ghost_contours, compute_seam_metrics


CARRIERS = {
    "plane": {
        "type": "rectangular_bowl_v1",
        "flat_half_length_m": 2.6,
        "flat_half_width_m": 1.2,
        "outer_half_length_m": 12.0,
        "outer_half_width_m": 12.0,
        "corner_height_m": 0.0,
        "uniform_cells": [64, 64],
    },
    "bowl": {
        "type": "rectangular_bowl_v1",
        "flat_half_length_m": 2.6,
        "flat_half_width_m": 1.2,
        "outer_half_length_m": 12.0,
        "outer_half_width_m": 12.0,
        "corner_height_m": 1.5,
        "uniform_cells": [64, 64],
    },
    "dome_floor": {
        "type": "dome_floor_v1",
        "dome_radius_m": 12.0,
        "dome_latitude_cells": 64,
        "dome_longitude_cells": 128,
        "floor_radial_cells": 32,
    },
    "cylinder_floor": {
        "type": "cylinder_floor_v1",
        "radius_m": 12.0,
        "height_m": 12.0,
        "vertical_cells": 32,
        "angular_cells": 128,
        "floor_radial_cells": 32,
    },
    "cube_floor": {
        "type": "cube_floor_v1",
        "half_extent_m": 12.0,
        "height_m": 12.0,
        "face_cells": 32,
    },
    "burger_like": {
        "type": "burger_like_v1",
        "outer_radius_m": 12.0,
        "fillet_radius_m": 2.0,
        "height_m": 12.0,
    },
}

VIEWS = {
    "oblique": {"azimuth_rad": 0.8, "elevation_rad": 1.0, "distance_m": 8.5},
    "low": {"azimuth_rad": 0.8, "elevation_rad": 0.35, "distance_m": 8.5},
}


def sha256_file(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _camera_id_from_path(path):
    match = re.search(r"camera(\d+)", Path(path).name)
    if not match:
        raise ValueError(f"dataset path does not identify a camera: {path}")
    return int(match.group(1))


def load_experiment_inputs(config_path, dataset_dir):
    """Load a complete, checksum-verified synchronous RGB/depth frame set."""
    config_path = Path(config_path)
    dataset_dir = Path(dataset_dir)
    cfg = json.loads(config_path.read_text())
    manifest_path = dataset_dir / "manifest.json"
    manifest = json.loads(manifest_path.read_text())
    if manifest.get("schema_version") != 1 or not manifest.get("frames"):
        raise ValueError("dataset manifest must use schema_version 1 and contain a frame")

    cameras = sorted(cfg.get("cameras", []), key=lambda camera: camera["id"])
    camera_ids = [camera["id"] for camera in cameras]
    frame = manifest["frames"][0]
    frame_paths = frame.get("paths", [])
    if len(frame_paths) != len(cameras) or len(set(camera_ids)) != len(camera_ids):
        raise ValueError("RGB frame count must match unique configured camera IDs")
    offsets = frame.get("offset_ns", [0] * len(frame_paths))
    if len(offsets) != len(frame_paths) or any(offset != 0 for offset in offsets):
        raise ValueError("E-STITCH-01 requires a synchronized camera frame")
    path_ids = [_camera_id_from_path(path) for path in frame_paths]
    if path_ids != camera_ids:
        raise ValueError(f"RGB camera order {path_ids} does not match config IDs {camera_ids}")
    calibration_ids = manifest.get("calibration_ids")
    configured_calibrations = [camera.get("calibration_id") for camera in cameras]
    if calibration_ids != configured_calibrations:
        raise ValueError("manifest calibration IDs do not match configured cameras")

    images = []
    for camera, relative_path in zip(cameras, frame_paths):
        image_path = dataset_dir / relative_path
        expected_hash = manifest.get("sha256", {}).get(relative_path)
        if not expected_hash or sha256_file(image_path) != expected_hash:
            raise ValueError(f"RGB checksum missing or invalid: {relative_path}")
        image = np.asarray(Image.open(image_path).convert("RGB"), dtype=float)
        resolution = camera.get("resolution", {})
        expected_shape = (resolution.get("height"), resolution.get("width"))
        if image.shape[:2] != expected_shape:
            raise ValueError(
                f"RGB dimensions for camera {camera['id']} are {image.shape[:2]}, "
                f"expected {expected_shape}"
            )
        images.append(image)

    truth_path = dataset_dir / "ground_truth.json"
    if not truth_path.is_file():
        raise ValueError("E-STITCH-01 requires radial-depth ground truth")
    truth = json.loads(truth_path.read_text())
    truth_frames = truth.get("depth_truth", {}).get("frames", [])
    if not truth_frames:
        raise ValueError("ground_truth.json does not contain a depth frame")
    depth_files = truth_frames[0].get("files", [])
    if len(depth_files) != len(cameras):
        raise ValueError("depth frame count must match configured camera IDs")
    if [_camera_id_from_path(path) for path in depth_files] != camera_ids:
        raise ValueError("depth camera order does not match configured camera IDs")

    depth_maps = []
    depth_hashes = truth["depth_truth"].get("sha256", {})
    for camera, relative_path in zip(cameras, depth_files):
        depth_path = dataset_dir / relative_path
        expected_hash = depth_hashes.get(relative_path)
        if not expected_hash or sha256_file(depth_path) != expected_hash:
            raise ValueError(f"depth checksum missing or invalid: {relative_path}")
        depth = np.load(depth_path, allow_pickle=False)
        resolution = camera.get("resolution", {})
        expected_shape = (resolution.get("height"), resolution.get("width"))
        if depth.shape[:2] != expected_shape:
            raise ValueError(
                f"depth dimensions for camera {camera['id']} are {depth.shape[:2]}, "
                f"expected {expected_shape}"
            )
        if not np.issubdtype(depth.dtype, np.floating):
            raise ValueError(f"depth map must use floating-point radial metres: {relative_path}")
        depth_maps.append(depth)

    provenance = {
        "config_sha256": sha256_file(config_path),
        "manifest_sha256": sha256_file(manifest_path),
        "ground_truth_sha256": sha256_file(truth_path),
        "implementation_sha256": {
            name: sha256_file(Path(__file__).with_name(name))
            for name in ("run_e_stitch_01.py", "fusion.py", "stitch_metrics.py", "reference.py")
        },
    }
    return cfg, images, depth_maps, provenance


def evaluate_case(config, images, depth_maps, surface_name, view_name, mode):
    """Run single carrier x view x fusion case and compute all metrics."""
    cfg = copy.deepcopy(config)
    cfg["surface"] = CARRIERS[surface_name]
    cfg["virtual_camera"].update(VIEWS[view_name])
    cfg["fusion"] = {"mode": mode, "edge_width_px": 24.0, "angle_power": 2.0, "num_pyramid_levels": 4}
    
    eye, directions, depth_factor = rays(cfg)
    near, far = cfg["virtual_camera"]["clip_m"]
    
    t0 = time.perf_counter()
    points, hit, distance = intersect(cfg["surface"], eye, directions, near, far, depth_factor)
    t_intersect = time.perf_counter() - t0
    
    colors, validity, thetas, edges = [], [], [], []
    for camera, image in zip(sorted(cfg["cameras"], key=lambda c: c["id"]), images):
        rgb, valid, edge, theta = sample(camera, points, image)
        valid &= hit
        colors.append(rgb)
        validity.append(valid)
        thetas.append(theta)
        edges.append(edge)
        
    colors = np.stack(colors, axis=-2)
    validity = np.stack(validity, axis=-1)
    thetas = np.stack(thetas, axis=-1)
    edges = np.stack(edges, axis=-1)
    
    # Linear color conversion
    linear_colors = np.where(colors <= 0.04045, colors / 12.92, ((colors + 0.055) / 1.055) ** 2.4)
    
    from fusion import fuse_samples
    t1 = time.perf_counter()
    blended_linear, weights = fuse_samples(
        linear_colors, validity, thetas, edges, points,
        mode=mode, edge_width_px=24.0, angle_power=2.0, num_pyramid_levels=4
    )
    t_fusion = time.perf_counter() - t1
    
    # Gamma correction to sRGB
    rgb = np.where(blended_linear <= 0.0031308, blended_linear * 12.92, 1.055 * np.maximum(blended_linear, 0) ** (1 / 2.4) - 0.055)
    total_weights = np.sum(weights, axis=-1)
    
    fallback = np.array([0.23, 0.24, 0.24])
    rgb = np.where((total_weights > 1e-6)[..., None], rgb, fallback)
    rgb = np.where(hit[..., None], rgb, 0.0)
    final_rgb = np.uint8(np.rint(np.clip(rgb, 0.0, 1.0) * 255.0))
    
    # ROI masking (excluding vehicle footprint)
    vehicle = cfg["vehicle"]
    footprint = (
        (np.abs(points[..., 0]) <= vehicle["length_m"] / 2 + vehicle["mask_margin_m"])
        & (np.abs(points[..., 1]) <= vehicle["width_m"] / 2 + vehicle["mask_margin_m"])
    )
    roi = hit & ~footprint
    
    # Seam metrics
    seam_res = compute_seam_metrics(final_rgb, weights, validity)
    
    # Ghost contours
    ghost_res = compute_ghost_contours(final_rgb, colors, validity)
    
    # Depth consistency
    depth_res = compute_depth_consistency(points, weights, depth_maps, cfg["cameras"])
    
    coverage = np.sum(validity, axis=-1).astype(np.uint8)
    valid_fraction = float((coverage[roi] > 0).mean()) if roi.any() else 0.0
    
    return {
        "carrier": surface_name,
        "view": view_name,
        "mode": mode,
        "timing_ms": {
            "intersection_ms": t_intersect * 1000.0,
            "fusion_ms": t_fusion * 1000.0,
            "total_render_ms": (t_intersect + t_fusion) * 1000.0,
        },
        "coverage": {
            "roi_pixels": int(roi.sum()),
            "miss_pixels": int((~hit).sum()),
            "valid_projection_fraction": valid_fraction,
        },
        "seam": seam_res,
        "ghosting": ghost_res,
        "depth_consistency": depth_res,
        "final_rgb": final_rgb,
    }


def run_experiment(config_path, dataset_dir, output_dir):
    """Execute matrix over all carriers, views, and fusion modes."""
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    
    dataset_dir = Path(dataset_dir)
    cfg, images, depth_maps, provenance = load_experiment_inputs(config_path, dataset_dir)
            
    rows = []
    cases_total = len(CARRIERS) * len(VIEWS) * len(FUSION_MODES)
    print(f"Executing E-STITCH-01 matrix ({cases_total} cases)...", flush=True)
    
    case_idx = 0
    for carrier in CARRIERS:
        for view in VIEWS:
            for mode in FUSION_MODES:
                case_idx += 1
                name = f"{carrier}_{view}_{mode}"
                print(f"[{case_idx:2d}/{cases_total}] Running {name}...", flush=True)
                
                result = evaluate_case(cfg, images, depth_maps, carrier, view, mode)
                
                # Save preview image
                preview_path = output_dir / f"{name}.png"
                Image.fromarray(result.pop("final_rgb")).save(preview_path)
                result["preview_file"] = f"{name}.png"
                rows.append(result)
                
    summary = {
        "schema_version": 1,
        "experiment_id": "E-STITCH-01",
        "fusion_implementation": "validity_zero_extension_v2",
        "timestamp_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "dataset": str(dataset_dir),
        "provenance": provenance,
        "cases_count": len(rows),
        "results": rows,
    }
    
    (output_dir / "e_stitch_01_summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    
    # Generate Markdown Report
    lines = [
        "# Exploratory matrix E-STITCH-01: варианты сшивки и геометрии",
        "",
        "Одна статическая capture × 6 carriers × 2 virtual views × 7 fusion variants (84 cases).",
        "Это exploratory screen, а не независимая подтверждающая серия; для выводов нужны holdout scenes/seeds.",
        "`fusion_ms` измеряет только последовательные Python/NumPy/SciPy операции на CPU, не GPU pipeline.",
        "Ghost fraction — source-disagreement proxy, зависящий от fused output; он ещё должен быть сопоставлен с object-ID truth.",
        "",
        "| Носитель | Ракурс | Стратегия Fusion | Seam pixels | Seam CIE76 p95 | L-gradient jump | Fused ghost proxy % | Exact Depth Cov % | Consistent Weight % | Python CPU ms |",
        "|---|---|---|---:|---:|---:|---:|---:|---:|---:|",
    ]
    
    for r in rows:
        seam_p95 = r["seam"]["p95_delta_e"]
        grad_disc = r["seam"]["gradient_discontinuity"]
        ghost_pct = r["ghosting"]["ghost_fraction_of_overlap"] * 100.0
        depth_cov = r["depth_consistency"]["0.05"]["exact_visible_coverage_fraction"] * 100.0
        weight_frac = r["depth_consistency"]["0.05"]["depth_consistent_weight_fraction"] * 100.0
        t_fus = r["timing_ms"]["fusion_ms"]
        lines.append(
            f"| `{r['carrier']}` | `{r['view']}` | `{r['mode']}` | {r['seam']['seam_pixels']} | "
            f"{seam_p95:.2f} | {grad_disc:.2f} | "
            f"{ghost_pct:.2f}% | {depth_cov:.2f}% | {weight_frac:.2f}% | {t_fus:.2f} |"
        )
        
    lines.extend([
        "",
        "## Границы вывода",
        "",
        "Варианты fusion здесь исполняются на CPU в offline reference, а не в GLES shader. Seam CIE76 — шаг между соседними output pixels на границе argmax-weight labels. Ghost proxy показывает только сохранение обоих разнесённых source contours в fused image; он не заменяет независимую semantic/object truth.",
        "",
        "Эта матрица не является независимым повтором: все 84 случая используют один capture. Нельзя по ней объявлять лучший carrier/fusion или target-device frame rate.",
        "",
    ])
    
    (output_dir / "REPORT.md").write_text("\n".join(lines))
    print(f"E-STITCH-01 completed. Report generated at {output_dir / 'REPORT.md'}", flush=True)
    return summary


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", default="tests/data/e_stitch_01_v1/config.json")
    parser.add_argument("--dataset", default="tests/data/e_stitch_01_v1")
    parser.add_argument("--output", default="artifacts/e-stitch-01-v2")
    args = parser.parse_args()
    run_experiment(args.config, args.dataset, args.output)
