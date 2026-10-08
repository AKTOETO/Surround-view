"""Plot E-STITCH-01 experimental results for Chapter 4 of the diploma thesis."""
import json
from pathlib import Path
import matplotlib.pyplot as plt
import numpy as np


def plot_e_stitch_results(summary_json_path, output_fig_path):
    data = json.loads(Path(summary_json_path).read_text())
    rows = data["results"]
    
    # Filter low-view results
    low_rows = [r for r in rows if r["view"] == "low"]
    
    carrier_labels = ["Плоскость", "Чаша (Bowl)", "Купол (Dome)", "Цилиндр", "Куб", "Burger-like"]
    modes = ["hard_best_angle", "edge_feather", "angular_feather", "seam_distance_feather", "graph_cut_seam", "multi_band", "graph_cut_multi_band"]
    mode_labels = ["Hard Angle", "Edge Feather", "Angular Feather", "Seam Distance", "Graph Cut", "Multi-Band", "GC + Multi-Band"]
    
    fig, axes = plt.subplots(2, 2, figsize=(14, 10))
    plt.subplots_adjust(hspace=0.35, wspace=0.25)
    
    # Plot 1: Seam Delta E (p95) across fusion modes (grouped for Dome & Plane)
    ax1 = axes[0, 0]
    dome_seam = [r["seam"]["p95_delta_e"] for r in low_rows if r["carrier"] == "dome_floor"]
    plane_seam = [r["seam"]["p95_delta_e"] for r in low_rows if r["carrier"] == "plane"]
    
    x = np.arange(len(modes))
    width = 0.35
    ax1.bar(x - width/2, dome_seam, width, label="Купол (Dome)", color="#2b5c8f")
    ax1.bar(x + width/2, plane_seam, width, label="Плоскость (Plane)", color="#d95f02")
    ax1.set_title(r"1. Скачок цвета на шве $\Delta E$ ($p_{95}$)", fontsize=12, fontweight="bold")
    ax1.set_xticks(x)
    ax1.set_xticklabels(mode_labels, rotation=35, ha="right", fontsize=9)
    ax1.set_ylabel(r"$\Delta E$ в пространстве Lab", fontsize=10)
    ax1.legend(loc="upper right")
    ax1.grid(axis="y", linestyle="--", alpha=0.6)
    
    # Plot 2: Gradient Discontinuity across fusion modes
    ax2 = axes[0, 1]
    dome_grad = [r["seam"]["gradient_discontinuity"] for r in low_rows if r["carrier"] == "dome_floor"]
    plane_grad = [r["seam"]["gradient_discontinuity"] for r in low_rows if r["carrier"] == "plane"]
    ax2.plot(x, dome_grad, "o-", label="Купол (Dome)", color="#2b5c8f", linewidth=2, markersize=7)
    ax2.plot(x, plane_grad, "s--", label="Плоскость (Plane)", color="#d95f02", linewidth=2, markersize=7)
    ax2.set_title("2. Разрыв градиента яркости на шве", fontsize=12, fontweight="bold")
    ax2.set_xticks(x)
    ax2.set_xticklabels(mode_labels, rotation=35, ha="right", fontsize=9)
    ax2.set_ylabel("Gradient Discontinuity Magnitude", fontsize=10)
    ax2.legend()
    ax2.grid(True, linestyle="--", alpha=0.6)
    
    # Plot 3: Exact Depth-Visible Coverage across Carriers (low view)
    ax3 = axes[1, 0]
    exact_cov = [r["depth_consistency"]["0.05"]["exact_visible_coverage_fraction"] * 100.0 for r in low_rows if r["mode"] == "angular_feather"]
    colors = ["#1b9e77", "#d95f02", "#7570b3", "#e7298a", "#66a61e", "#e6ab02"]
    x_c = np.arange(len(carrier_labels))
    ax3.bar(x_c, exact_cov, color=colors, width=0.55)
    ax3.set_title(r"3. Точная глубинная видимость ($\tau=0.05$ м)", fontsize=12, fontweight="bold")
    ax3.set_xticks(x_c)
    ax3.set_xticklabels(carrier_labels, rotation=25, ha="right", fontsize=9)
    ax3.set_ylabel("Exact Depth-Consistent Coverage, %", fontsize=10)
    ax3.set_ylim(0, 30)
    for i, v in enumerate(exact_cov):
        ax3.text(i, v + 0.8, f"{v:.1f}%", ha="center", fontweight="bold", fontsize=9)
    ax3.grid(axis="y", linestyle="--", alpha=0.6)
    
    # Plot 4: Fusion Computation Time (ms)
    ax4 = axes[1, 1]
    timings = [r["timing_ms"]["fusion_ms"] for r in low_rows if r["carrier"] == "dome_floor"]
    ax4.bar(x, timings, color="#4575b4", width=0.5)
    ax4.axhline(33.3, color="red", linestyle="--", label="Лимит 30 FPS (33.3 ms)")
    ax4.set_title("4. Вычислительное время слияния (CPU)", fontsize=12, fontweight="bold")
    ax4.set_xticks(x)
    ax4.set_xticklabels(mode_labels, rotation=35, ha="right", fontsize=9)
    ax4.set_ylabel("Время расчета fusion, мс", fontsize=10)
    ax4.legend(loc="upper left")
    ax4.grid(axis="y", linestyle="--", alpha=0.6)
    
    output_fig_path = Path(output_fig_path)
    output_fig_path.parent.mkdir(parents=True, exist_ok=True)
    plt.savefig(output_fig_path, dpi=200, bbox_inches="tight")
    plt.close()
    print(f"Saved figure to {output_fig_path}")


if __name__ == "__main__":
    plot_e_stitch_results(
        "artifacts/e-stitch-01-v1/e_stitch_01_summary.json",
        "docs/diploma/figures/experiments/e_stitch_01_comparison.png"
    )
