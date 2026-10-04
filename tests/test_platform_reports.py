import copy
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from compare_reports import compare_reports


def report():
    return dict(suite_id="surround-view-platform-v1", config_sha256="config", profile_id="profile", status="passed",
                fixture=dict(kind="gradient", sha256="fixture"), build=dict(type="Release", source_fingerprint="code"),
                settings=dict(iterations=60, warmup=10, repeats=3, opencv_threads=1, repeat_scope="contexts", timing_scope="render/readback"),
                render=[dict(variant="bowl", effective_config_sha256="case", input_mode="cached", triangles=2312,
                             width=640, height=360, render_readback_ms=dict(p95=value)) for value in [1., 2., 3.]])


class ReportTests(unittest.TestCase):
    def test_matching_workloads_use_median(self):
        left, right = report(), report()
        for row in right["render"]:
            row["render_readback_ms"]["p95"] *= 2
        result = compare_reports(left, right)
        self.assertTrue(result["comparable"])
        self.assertEqual(result["render"][0]["target_to_baseline_ratio"], 2.)

    def test_mismatches_suppress_ratio(self):
        for change in [lambda r: r.update(config_sha256="other"), lambda r: r["fixture"].update(sha256="other"),
                       lambda r: r["build"].update(source_fingerprint="other"), lambda r: r["build"].update(type="Debug"),
                       lambda r: r.update(status="partial"), lambda r: r["render"][0].update(input_mode="new_image_owners")]:
            right = copy.deepcopy(report())
            change(right)
            result = compare_reports(report(), right)
            self.assertFalse(result["comparable"])
            self.assertIsNone(result["render"][0]["target_to_baseline_ratio"])


if __name__ == "__main__":
    unittest.main()
