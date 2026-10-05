#!/usr/bin/env python3
"""Native 0.2.0 figures from immutable Markdown reports, independent of historical 0.1.0."""

import argparse
import os
from pathlib import Path
import statistics
import sys

os.environ.setdefault("MPLCONFIGDIR", "/tmp/sv-matplotlib")
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / "tools"))
from compare_reports import compare_reports, load_report


def generate(baseline, target, output):
    comparison = compare_reports(baseline, target)
    if not comparison["comparable"]:
        raise ValueError(comparison["reasons"])
    output.mkdir(parents=True, exist_ok=True)
    plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 10, "svg.fonttype": "none"})
    variants = ["plane", "bowl", "bowl_dense", "bowl_720p", "bowl_upload", "dome_floor"]
    labels = ["Plane", "Bowl", "Dense", "720p", "New uploads", "Dome + floor"]
    figure, axes = plt.subplots(1, 2, figsize=(13, 4.6), layout="constrained")
    for offset, report, color in [(-.18, baseline, "#287b8e"), (.18, target, "#ce9154")]:
        for axis, metric, title in zip(axes, ["render_readback_ms", "gpu_draw_ms"],
                                      ["CPU wall render / readback", "Только valid GPU draw query"]):
            values = [[row[metric]["p95"] for row in report["render"] if row["variant"] == name and row[metric] is not None]
                      for name in variants]
            medians = [statistics.median(group) if group else np.nan for group in values]
            errors = [[mid - min(group) if group else 0 for mid, group in zip(medians, values)],
                      [max(group) - mid if group else 0 for mid, group in zip(medians, values)]]
            axis.bar(np.arange(len(variants)) + offset, medians, .32, color=color, label=report["label"])
            axis.errorbar(np.arange(len(variants)) + offset, medians, yerr=errors, fmt="none", ecolor="#344052", capsize=3)
            axis.set(xticks=np.arange(len(variants)), xticklabels=labels, ylabel="Медиана p95 повторов, мс", title=title)
            axis.grid(axis="y", alpha=.2)
            axis.set_axisbelow(True)
            axis.legend(fontsize=9)
            axis.tick_params(axis="x", labelrotation=18, labelsize=8)
    figure.suptitle("Native suite 0.2.0: три новых EGL-контекста в одном процессе; ус — диапазон p95", fontsize=11)
    figure.savefig(output / "04_native_timings.svg")
    plt.close(figure)
    identifiers = [check["id"] for check in baseline["criteria"]]
    figure, axis = plt.subplots(figsize=(10, 7.4), layout="constrained")
    colors = {"pass": 0, "skip": 1, "fail": 2}
    statuses = [{check["id"]: check["status"] for check in report["criteria"]} for report in [baseline, target]]
    data = np.array([[colors[status.get(identifier, "skip")] for status in statuses] for identifier in identifiers])
    from matplotlib.colors import ListedColormap
    axis.imshow(data, cmap=ListedColormap(["#dceee7", "#f9e8c9", "#f4c8c3"]), vmin=0, vmax=2, aspect="auto")
    axis.set(yticks=np.arange(len(identifiers)), yticklabels=identifiers, xticks=[0, 1],
             xticklabels=[baseline["label"], target["label"]], title="Критерии native suite: pass / skip / fail")
    for i in range(len(identifiers)):
        for j, status in enumerate(statuses):
            axis.text(j, i, status.get(identifiers[i], "absent"), ha="center", va="center", fontsize=9)
    figure.savefig(output / "04_native_criteria.svg")
    plt.close(figure)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, default=HERE.parent / "validation/baselines/PC_RTX.md")
    parser.add_argument("--target", type=Path, default=HERE.parent / "validation/baselines/PC_MESA.md")
    parser.add_argument("--output", type=Path, default=HERE / "figures/experiments")
    args = parser.parse_args()
    generate(load_report(args.baseline), load_report(args.target), args.output)
    print(args.output)
