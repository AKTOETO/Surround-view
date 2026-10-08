#!/usr/bin/env python3
"""Measure Blender cube-depth -> fisheye radial-range error on analytic planes.

The ray/plane intersection below is an independent oracle. The cube-face ray
directions are written out explicitly rather than obtained from rig.face_basis.
"""
import argparse
import json
from pathlib import Path

import numpy as np

from blender.rig import configuration
from depth_truth import convert_camera_depth
from simulator import camera_rays


FACE_DIRECTIONS = {
    "px": lambda a, b: (np.ones_like(a), b, -a),
    "nx": lambda a, b: (-np.ones_like(a), b, a),
    "py": lambda a, b: (a, np.ones_like(a), -b),
    "ny": lambda a, b: (a, -np.ones_like(a), b),
    "pz": lambda a, b: (a, b, np.ones_like(a)),
    "nz": lambda a, b: (-a, b, -np.ones_like(a)),
}
FACE_FORWARD_COMPONENT = {
    "px": lambda direction: direction[..., 0],
    "nx": lambda direction: -direction[..., 0],
    "py": lambda direction: direction[..., 1],
    "ny": lambda direction: -direction[..., 1],
    "pz": lambda direction: direction[..., 2],
    "nz": lambda direction: -direction[..., 2],
}


def analytic_plane_faces(face_size, normal, offset_m):
    """Build camera-axis Z values where an infinite plane crosses each ray."""
    coordinate = (np.arange(face_size, dtype=float) + 0.5) * (2.0 / face_size) - 1.0
    a, b = np.meshgrid(coordinate, coordinate)
    n = np.asarray(normal, dtype=float)
    n /= np.linalg.norm(n)
    faces = {}
    for name, components in FACE_DIRECTIONS.items():
        direction = np.stack(components(a, b), axis=-1)
        direction /= np.linalg.norm(direction, axis=-1, keepdims=True)
        denominator = direction @ n
        distance = np.divide(offset_m, denominator,
                             out=np.full_like(denominator, np.inf),
                             where=denominator > 1e-8)
        # Blender's Z pass is axial depth along the cube face's own forward
        # axis, not the original fisheye camera's +Z axis.
        z = distance * FACE_FORWARD_COMPONENT[name](direction)
        faces[name] = np.where(np.isfinite(z) & (z > 0), z, 1e10).astype(np.float32)
    return faces


def evaluate(face_size, normal, offset_m, camera=None):
    camera = camera or configuration()["cameras"][0]
    n = np.asarray(normal, dtype=float)
    n /= np.linalg.norm(n)
    faces = analytic_plane_faces(face_size, n, offset_m)
    measured = convert_camera_depth(camera, faces, face_size)
    rays, theta = camera_rays(camera)
    denominator = rays @ n
    expected = np.divide(offset_m, denominator,
                         out=np.full_like(denominator, np.nan),
                         where=denominator > 1e-8)
    fov = theta <= camera["projection"]["theta_max_rad"]
    comparable = fov & np.isfinite(expected) & (expected <= 30.0)
    valid = comparable & np.isfinite(measured)
    error_map = np.abs(measured.astype(float) - expected)

    def summary(mask):
        values = error_map[mask & valid]
        if not values.size:
            return {"samples": 0, "p50_m": None, "p95_m": None,
                    "p99_m": None, "max_m": None, "rmse_m": None}
        return {
            "samples": int(values.size),
            "p50_m": float(np.percentile(values, 50)),
            "p95_m": float(np.percentile(values, 95)),
            "p99_m": float(np.percentile(values, 99)),
            "max_m": float(values.max()),
            "rmse_m": float(np.sqrt(np.mean(values**2))),
        }

    range_masks = {
        "0_to_5m": expected <= 5.0,
        "5_to_10m": (expected > 5.0) & (expected <= 10.0),
        "10_to_20m": (expected > 10.0) & (expected <= 20.0),
        "20_to_30m": (expected > 20.0) & (expected <= 30.0),
    }
    angle_masks = {
        "0_to_0.6rad": theta <= 0.6,
        "0.6_to_1.0rad": (theta > 0.6) & (theta <= 1.0),
        "1.0_to_1.2rad": (theta > 1.0) & (theta <= 1.2),
        "1.2_to_fov_rad": theta > 1.2,
    }
    return {
        "face_size": int(face_size),
        "plane_normal_camera": n.tolist(),
        "plane_offset_m": float(offset_m),
        "fov_rays": int(fov.sum()),
        "comparable_rays": int(comparable.sum()),
        "valid_rays": int(valid.sum()),
        "valid_fraction_of_comparable": float(valid.sum() / max(1, comparable.sum())),
        "absolute_error_m": summary(comparable),
        "error_by_expected_range_m": {name: summary(mask) for name, mask in range_masks.items()},
        "error_by_incidence_angle_m": {name: summary(mask) for name, mask in angle_masks.items()},
        "nearby_core_roi_m": summary((expected <= 30.0) & (theta <= 0.6)),
    }


def run(output, sizes):
    normals = ([0.0, 0.0, 1.0], [0.16, -0.11, 0.98])
    offsets = (2.0, 6.0, 20.0)
    rows = [evaluate(size, normal, distance)
            for size in sizes for normal in normals for distance in offsets]
    report = {
        "schema_version": 1,
        "suite_id": "depth-plane-conversion-v1",
        "method": "analytic infinite-plane ray intersection vs cube-depth conversion",
        "depth_limit_m": 30.0,
        "rows": rows,
        "limitations": [
            "synthetic analytic planes; no Blender rasterization or EXR I/O is exercised",
            "no discontinuous object boundaries or occlusion labels",
            "radial-depth conversion accuracy is not camera calibration or stitching quality",
        ],
    }
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    lines = [
        "# Аналитическая проверка преобразования depth",
        "",
        "Сравнивается преобразование cube-face camera-Z → fisheye radial range с независимым пересечением луча и бесконечной плоскости.",
        "Строки с разной нормалью/дистанцией и разрешением face показывают сходимость дискретной выборки.",
        "Основная статистика ограничена передними пересечениями и дальностью до 30 м; почти касательные лучи вынесены в отдельные диапазоны по углу и дальности.",
        "",
        "| Face | Нормаль | Offset, м | Валидно | p95 до 30 м, мм | p95 core θ≤0.6 rad, мм | p99 до 30 м, мм |",
        "|---:|---|---:|---:|---:|---:|---:|",
    ]
    for row in rows:
        e = row["absolute_error_m"]
        core = row["nearby_core_roi_m"]
        lines.append(
            f"| {row['face_size']} | `{[round(v, 3) for v in row['plane_normal_camera']]}` | "
            f"{row['plane_offset_m']:.1f} | {row['valid_fraction_of_comparable']:.3%} | "
            f"{e['p95_m']*1000:.3f} | {core['p95_m']*1000:.3f} | {e['p99_m']*1000:.3f} |"
        )
    lines += [
        "",
        "Range/θ bins, p50/p99/max/RMSE и число измерений по bin находятся в `report.json`.",
        "На cube face измеряется осевая Z конкретной грани; конечное разрешение и билинейная выборка дают ошибку, зависящую от face size, дальности и угла.",
        "",
        "Ограничения и первичные значения находятся в `report.json`.",
        "",
    ]
    (output / "REPORT.md").write_text("\n".join(lines))
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--sizes", type=int, nargs="+", default=[32, 64, 128, 256])
    arguments = parser.parse_args()
    if any(size < 8 or size > 4096 for size in arguments.sizes):
        parser.error("face sizes must be in 8..4096")
    print(json.dumps(run(arguments.output, arguments.sizes), indent=2))
