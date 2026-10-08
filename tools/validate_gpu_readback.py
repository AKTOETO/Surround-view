"""Validation of GPU GLES readback frames against CPU analytic reference and scene truth."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent / "blender"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from image_quality_oracle import compute_psnr, compute_ssim, evaluate_image_quality, render_scene_oracle
from reference import render


def run_gpu_readback_comparison(config_path, dataset_dir, binary_path, output_dir):
    """Render frame on GPU using sv-bench, compare with CPU reference and scene truth oracle."""
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    
    cfg = json.loads(Path(config_path).read_text())
    dataset_dir = Path(dataset_dir)
    manifest_path = dataset_dir / "manifest.json"
    manifest = json.loads(manifest_path.read_text())
    
    # 1. Load input images
    images = []
    for p in manifest["frames"][0]["paths"]:
        images.append(np.asarray(Image.open(dataset_dir / p).convert("RGB"), dtype=float))
        
    # 2. CPU Analytic Reference
    cpu_result = render(cfg, images)
    cpu_rgb = cpu_result["rgb"]
    Image.fromarray(cpu_rgb).save(output_dir / "cpu_reference.png")
    
    # 3. Ground Truth Scene Oracle
    oracle_result = render_scene_oracle(cfg, width=cfg["output"]["width"], height=cfg["output"]["height"])
    oracle_rgb = np.uint8(np.rint(oracle_result["rgb"] * 255.0))
    Image.fromarray(oracle_rgb).save(output_dir / "scene_oracle.png")
    
    # 4. GPU GLES Render via sv-bench (if binary exists)
    binary = Path(binary_path)
    gpu_available = binary.exists() and binary.is_file()
    gpu_dir = output_dir / "gpu_bench"
    
    if gpu_available:
        try:
            cmd = [
                str(binary),
                "--config", str(config_path),
                "--manifest", str(manifest_path),
                "--output", str(gpu_dir),
                "--egl-platform", "surfaceless",
                "--iterations", "1",
                "--warmup", "0",
            ]
            res = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
            if res.returncode == 0 and (gpu_dir / "preview.ppm").exists():
                gpu_rgb = np.asarray(Image.open(gpu_dir / "preview.ppm").convert("RGB"))
                Image.fromarray(gpu_rgb).save(output_dir / "gpu_readback.png")
            else:
                gpu_rgb = None
        except Exception:
            gpu_rgb = None
    else:
        gpu_rgb = None
        
    # 5. Evaluate Metrics
    # CPU vs Scene Oracle
    cpu_vs_oracle = evaluate_image_quality(cpu_rgb, oracle_result)
    
    # Difference maps
    diff_cpu_oracle = np.abs(cpu_rgb.astype(float) - oracle_rgb.astype(float)).astype(np.uint8)
    Image.fromarray(diff_cpu_oracle).save(output_dir / "diff_cpu_vs_oracle.png")
    
    results = {
        "schema_version": 1,
        "suite_id": "image-quality-oracle-v1",
        "cpu_vs_oracle": cpu_vs_oracle,
        "gpu_evaluated": gpu_rgb is not None,
    }
    
    if gpu_rgb is not None:
        psnr_gpu_cpu = compute_psnr(gpu_rgb, cpu_rgb)
        ssim_gpu_cpu = compute_ssim(gpu_rgb, cpu_rgb)
        max_diff = int(np.max(np.abs(gpu_rgb.astype(int) - cpu_rgb.astype(int))))
        mean_diff = float(np.mean(np.abs(gpu_rgb.astype(float) - cpu_rgb.astype(float))))
        
        gpu_vs_oracle = evaluate_image_quality(gpu_rgb, oracle_result)
        results["gpu_vs_cpu"] = {
            "psnr_db": psnr_gpu_cpu,
            "ssim": ssim_gpu_cpu,
            "max_channel_diff_lsb": max_diff,
            "mean_channel_diff_lsb": mean_diff,
        }
        results["gpu_vs_oracle"] = gpu_vs_oracle
        
    (output_dir / "report.json").write_text(json.dumps(results, indent=2) + "\n")
    
    # Markdown Report
    lines = [
        "# Независимый Image-Quality Oracle и валидация GPU Readback",
        "",
        "Сравнение сшитого кругового изображения с прямым аналитическим рендером 3D-сцены (Scene Oracle) и CPU reference.",
        "",
        "### Метрики качества сшивки относительно Scene Truth",
        "",
        "| Область | PSNR, dB | SSIM | MAE | Пиксели ROI |",
        "|---|---:|---:|---:|---:|",
        f"| **Overall (без кузова)** | {cpu_vs_oracle['overall']['psnr_db']:.2f} | {cpu_vs_oracle['overall']['ssim']:.4f} | {cpu_vs_oracle['overall']['mae']:.4f} | {cpu_vs_oracle['overall']['roi_pixels']} |",
        f"| **Дорожное полотно (Ground)** | {cpu_vs_oracle['ground_plane']['psnr_db']:.2f} | {cpu_vs_oracle['ground_plane']['ssim']:.4f} | {cpu_vs_oracle['ground_plane']['mae']:.4f} | {cpu_vs_oracle['ground_plane']['pixels']} |",
        f"| **Препятствия (Vertical)** | {cpu_vs_oracle['vertical_obstacles']['psnr_db']:.2f} | {cpu_vs_oracle['vertical_obstacles']['ssim']:.4f} | {cpu_vs_oracle['vertical_obstacles']['mae']:.4f} | {cpu_vs_oracle['vertical_obstacles']['pixels']} |",
        "",
    ]
    
    if gpu_rgb is not None:
        lines.extend([
            "### Согласованность GPU Readback и CPU Reference",
            "",
            f"- **PSNR (GPU vs CPU):** {results['gpu_vs_cpu']['psnr_db']:.2f} dB",
            f"- **SSIM (GPU vs CPU):** {results['gpu_vs_cpu']['ssim']:.4f}",
            f"- **Максимальное расхождение:** {results['gpu_vs_cpu']['max_channel_diff_lsb']} LSB",
            f"- **Среднее расхождение:** {results['gpu_vs_cpu']['mean_channel_diff_lsb']:.3f} LSB",
            "",
        ])
        
    (output_dir / "REPORT.md").write_text("\n".join(lines))
    print(f"Image quality oracle report written to {output_dir / 'REPORT.md'}", flush=True)
    return results


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", default="artifacts/blender-depth-truth-dataset-fixed/config.json")
    parser.add_argument("--dataset", default="artifacts/blender-depth-truth-dataset-fixed")
    parser.add_argument("--binary", default="build/sv-bench")
    parser.add_argument("--output", default="artifacts/image-quality-oracle-v1")
    args = parser.parse_args()
    run_gpu_readback_comparison(args.config, args.dataset, args.binary, args.output)
