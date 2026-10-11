import importlib.util
from pathlib import Path
import unittest


SCRIPT = Path(__file__).parents[1] / "tools" / "benchmark-vulkan-matrix.py"
SPEC = importlib.util.spec_from_file_location("benchmark_vulkan_matrix", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class VulkanMatrixTests(unittest.TestCase):
    def test_positive_csv(self):
        self.assertEqual(MODULE.positive_csv("32,256,2048", 32, 2048), [32, 256, 2048])
        with self.assertRaises(Exception):
            MODULE.positive_csv("16", 32, 2048)

    def test_summary_ignores_failed_runs(self):
        runs = [
            {"decode_tokens_per_second": 10.0},
            {"error": "failed"},
            {"decode_tokens_per_second": 14.0},
        ]
        self.assertEqual(MODULE.summarize(runs), {
            "passed": 2,
            "failed": 1,
            "median_decode_tokens_per_second": 12.0,
        })


if __name__ == "__main__":
    unittest.main()
