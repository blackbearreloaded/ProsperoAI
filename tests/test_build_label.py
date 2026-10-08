# ProsperoAI - A pull request's build names itself.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

import re
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class BuildLabelTests(unittest.TestCase):
    def test_pull_request_artifact_and_label(self):
        workflow = (ROOT / ".github/workflows/build.yml").read_text(encoding="utf-8")
        self.assertIn(
            'echo "artifact=${GITHUB_REPOSITORY##*/}-PR$PR_NUMBER-$short" >> "$GITHUB_OUTPUT"',
            workflow,
        )
        self.assertIn("PR_HEAD: ${{ github.event.pull_request.head.sha }}", workflow)
        self.assertIn('echo "BUILD_LABEL=PR $PR_NUMBER, $short" >> "$GITHUB_ENV"', workflow)
        self.assertIn("name: ${{ steps.label.outputs.artifact }}", workflow)
        # The release job still finds a tag's build under its commit.
        self.assertIn('echo "artifact=prosperoai-release-$GITHUB_SHA" >> "$GITHUB_OUTPUT"', workflow)
        self.assertIn('--name "prosperoai-release-$GITHUB_SHA"', workflow)
        # A contributor's code is never built with write access or secrets.
        self.assertNotIn("pull_request_target:", workflow)

    def test_build_checks_the_label_first_and_writes_it(self):
        build = (ROOT / "tools/build.sh").read_text(encoding="utf-8")
        self.assertIn("printf '%s\\n' \"$BUILD_LABEL\" > \"$app/build-label.txt\"", build)
        self.assertLess(build.index("BUILD_LABEL must be"), build.index("setup-native-dependencies.sh"))
        self.assertLess(build.index('rm -rf -- "$app"'), build.index('> "$app/build-label.txt"'))

    def test_label_rule(self):
        build = (ROOT / "tools/build.sh").read_text(encoding="utf-8")
        check = re.search(r"^if \[\[ -n \$\{BUILD_LABEL:-\} \]\]; then\n.*?^fi\n", build, re.S | re.M)
        self.assertIsNotNone(check)

        def accepted(label):
            return subprocess.run(
                ["bash", "-c", "set -euo pipefail\n" + check.group(0)],
                env={"PATH": "/usr/bin:/bin", "BUILD_LABEL": label},
                capture_output=True,
            ).returncode == 0

        for label in ("PR 12, 0a1b2c3", "pacing test 2", "a", "v1.0_rc-2 #3", "x" * 40):
            self.assertTrue(accepted(label), label)
        for label in ("x" * 41, "PR 12; rm", "a/b", "$(id)", "line\nbreak", "caf\u00e9", "<b>", "a&b"):
            self.assertFalse(accepted(label), label)
        self.assertTrue(accepted(""))  # unset or empty: no label, no file


if __name__ == "__main__":
    unittest.main()
