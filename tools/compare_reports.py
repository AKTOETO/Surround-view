#!/usr/bin/env python3
"""Compare native platform reports only when their workload and source match."""
import argparse
import json
import re
import statistics
from pathlib import Path


def load_report(path):
    text = Path(path).read_text()
    if Path(path).suffix.lower() == ".md":
        block = re.search(r"<!-- sv-platform-report:v1 -->\s*```json\s*(.*?)\s*```", text, re.S)
        if not block:
            raise ValueError("native report block not found")
        text = block.group(1)
    report = json.loads(text)
    if report.get("schema_version") != 1 or report.get("suite_id") != "surround-view-platform-v1":
        raise ValueError("unsupported platform report schema/suite")
    return report


def compare_reports(base, target):
    reasons = []
    for name in ["suite_id", "config_sha256", "profile_id"]:
        if base[name] != target[name]:
            reasons.append(f"Mismatch: {name}")
    for name in ["kind", "sha256"]:
        if base["fixture"].get(name) != target["fixture"].get(name):
            reasons.append(f"Mismatch: fixture.{name}")
    for name in ["iterations", "warmup", "repeats", "repeat_scope", "timing_scope", "opencv_threads"]:
        if base["settings"][name] != target["settings"][name]:
            reasons.append(f"Mismatch: settings.{name}")
    if base["build"]["source_fingerprint"] != target["build"]["source_fingerprint"]:
        reasons.append("Mismatch: implementation source fingerprint")
    if base["build"]["type"] != "Release" or target["build"]["type"] != "Release":
        reasons.append("Both timing reports must use Release builds")
    if base["status"] != "passed" or target["status"] != "passed":
        reasons.append("Qualification is incomplete or failed on at least one platform")
    groups = []
    variants = sorted({r["variant"] for r in base["render"]} | {r["variant"] for r in target["render"]})
    for variant in variants:
        left = [r for r in base["render"] if r["variant"] == variant]
        right = [r for r in target["render"] if r["variant"] == variant]
        if not left or not right:
            reasons.append(f"Missing render variant: {variant}")
            continue
        workloads = lambda rows: {(r["effective_config_sha256"], r["input_mode"], r["triangles"], r["width"], r["height"]) for r in rows}
        if workloads(left) != workloads(right) or len(left) != len(right):
            reasons.append(f"Mismatch: workload/repeat count for {variant}")
        a = statistics.median(r["render_readback_ms"]["p95"] for r in left)
        b = statistics.median(r["render_readback_ms"]["p95"] for r in right)
        groups.append(dict(variant=variant, baseline_p95_ms=a, target_p95_ms=b))
    for group in groups:
        group["target_to_baseline_ratio"] = group["target_p95_ms"] / group["baseline_p95_ms"] if not reasons and group["baseline_p95_ms"] > 0 else None
    return dict(schema_version=1, comparable=not reasons, reasons=reasons, render=groups)


def write_comparison(path, base, target, result):
    lines = ["# Сравнение платформ", "", f"Базовая платформа: **{base['label']}**. Целевая: **{target['label']}**.", "",
             "Сопоставимость нагрузки: **" + ("подтверждена" if result["comparable"] else "не подтверждена") + "**.", "",
             "Отношение времени относится к медиане p95 повторов render/readback. Оно не является отношением FPS всей системы или физической задержки.", ""]
    if result["reasons"]:
        lines.extend("- " + reason for reason in result["reasons"])
        lines.append("")
    lines.extend(["| Вариант | База p95, мс | Цель p95, мс | Цель / база |", "|---|---:|---:|---:|"])
    for row in result["render"]:
        ratio = "—" if row["target_to_baseline_ratio"] is None else f"{row['target_to_baseline_ratio']:.3f}"
        lines.append(f"| {row['variant']} | {row['baseline_p95_ms']:.5f} | {row['target_p95_ms']:.5f} | {ratio} |")
    lines.extend(["", "| Критерий | База | Цель |", "|---|---|---|"])
    checks = [{c["id"]: c["status"] for c in report["criteria"]} for report in [base, target]]
    for identifier in sorted(set(checks[0]) | set(checks[1])):
        lines.append(f"| {identifier} | {checks[0].get(identifier, 'absent')} | {checks[1].get(identifier, 'absent')} |")
    lines.extend(["", "Версии OpenCV и компилятора, архитектура CPU и GL_RENDERER остаются частью паспортов платформ; они могут различаться при переносе.", "",
                  "```json", json.dumps(result, indent=2, ensure_ascii=False), "```", ""])
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text("\n".join(lines))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("baseline", type=Path)
    parser.add_argument("target", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--strict", action="store_true")
    args = parser.parse_args()
    baseline, target = load_report(args.baseline), load_report(args.target)
    result = compare_reports(baseline, target)
    write_comparison(args.output, baseline, target, result)
    print(args.output, "comparable=" + str(result["comparable"]))
    raise SystemExit(2 if args.strict and not result["comparable"] else 0)
