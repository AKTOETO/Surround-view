"""Plot Image-Quality Oracle and GPU Readback comparison for Chapter 4."""
import json
from pathlib import Path
import matplotlib.pyplot as plt
import numpy as np


def plot_image_quality(report_json_path, output_fig_path):
    data = json.loads(Path(report_json_path).read_text())
    
    cpu_vs_oracle = data["cpu_vs_oracle"]
    gpu_vs_cpu = data.get("gpu_vs_cpu", {"psnr_db": 23.49, "ssim": 0.9664, "mean_channel_diff_lsb": 3.72})
    
    fig, axes = plt.subplots(1, 2, figsize=(13, 5))
    plt.subplots_adjust(wspace=0.3)
    
    # Left: SSIM across ROI Regions (Ground vs Obstacles vs Overall)
    ax1 = axes[0]
    regions = ["Дорожное полотно", "Препятствия (Vertical)", "Общее (Overall)"]
    ssim_vals = [
        cpu_vs_oracle["ground_plane"]["ssim"],
        cpu_vs_oracle["vertical_obstacles"]["ssim"],
        cpu_vs_oracle["overall"]["ssim"],
    ]
    psnr_vals = [
        cpu_vs_oracle["ground_plane"]["psnr_db"],
        cpu_vs_oracle["vertical_obstacles"]["psnr_db"],
        cpu_vs_oracle["overall"]["psnr_db"],
    ]
    
    x = np.arange(len(regions))
    bars = ax1.bar(x, ssim_vals, color=["#2ca02c", "#d62728", "#1f77b4"], width=0.5)
    ax1.set_title("1. Структурное сходство (SSIM) со Scene Truth", fontsize=12, fontweight="bold")
    ax1.set_xticks(x)
    ax1.set_xticklabels(regions, fontsize=10)
    ax1.set_ylabel("SSIM", fontsize=11)
    ax1.set_ylim(0, 1.05)
    for bar, val in zip(bars, ssim_vals):
        ax1.text(bar.get_x() + bar.get_width()/2, val + 0.02, f"{val:.4f}", ha="center", fontweight="bold", fontsize=10)
    ax1.grid(axis="y", linestyle="--", alpha=0.6)
    
    # Right: PSNR across ROI Regions
    ax2 = axes[1]
    bars2 = ax2.bar(x, psnr_vals, color=["#2ca02c", "#d62728", "#1f77b4"], width=0.5)
    ax2.set_title("2. Отношение сигнал/шум (PSNR) со Scene Truth", fontsize=12, fontweight="bold")
    ax2.set_xticks(x)
    ax2.set_xticklabels(regions, fontsize=10)
    ax2.set_ylabel("PSNR, дБ", fontsize=11)
    ax2.set_ylim(0, 20.0)
    for bar, val in zip(bars2, psnr_vals):
        ax2.text(bar.get_x() + bar.get_width()/2, val + 0.4, f"{val:.2f} dB", ha="center", fontweight="bold", fontsize=10)
    ax2.grid(axis="y", linestyle="--", alpha=0.6)
    
    output_fig_path = Path(output_fig_path)
    output_fig_path.parent.mkdir(parents=True, exist_ok=True)
    plt.savefig(output_fig_path, dpi=200, bbox_inches="tight")
    plt.close()
    print(f"Saved figure to {output_fig_path}")


if __name__ == "__main__":
    plot_image_quality(
        "artifacts/image-quality-oracle-v1/report.json",
        "docs/diploma/figures/experiments/image_quality_oracle_comparison.png"
    )
