#!/usr/bin/env python3
"""Verify orchestration failures without contacting EOS or requiring Docker."""

import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import MagicMock, patch


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("eos_acceptance", ROOT / "dist-scripts/eos/run_acceptance.py")
runner = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(runner)


class EOSAcceptanceRunnerTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.cleanup = patch.object(runner.subprocess, "run", return_value=
                                    subprocess.CompletedProcess([], 0, b"", b""))
        self.clean = self.cleanup.start()
        self.addCleanup(self.cleanup.stop)

    def test_pair_uses_separate_containers_and_read_only_build(self):
        processes = [MagicMock(), MagicMock()]
        for process in processes:
            process.wait.return_value = 0
        with patch.object(runner.subprocess, "Popen", side_effect=processes) as launch:
            runner.run_scenario(self.root, "image", "fixture", "relay")
        commands = [call.args[0] for call in launch.call_args_list]
        self.assertEqual(len(commands), 2)
        self.assertNotEqual(commands[0][commands[0].index("--name") + 1],
                            commands[1][commands[1].index("--name") + 1])
        for role, command in zip(("host", "guest"), commands):
            self.assertIn(f"type=bind,source={self.root},target=/workspace,readonly", command)
            self.assertEqual(command[-3:], ["+online_acceptance", role, "ci-fixture-relay"])
            self.assertEqual(command[command.index("+online_force_relay") + 1], "1")
        self.assertEqual(self.clean.call_count, 2)

    def test_failed_host_fails_job_and_cleans_up_both_containers(self):
        processes = [MagicMock(), MagicMock()]
        processes[0].wait.return_value = 1
        with patch.object(runner.subprocess, "Popen", side_effect=processes):
            with self.assertRaisesRegex(RuntimeError, "host exited with status 1"):
                runner.run_scenario(self.root, "image", "fixture", "default")
        self.assertEqual(self.clean.call_count, 2)

    def test_watchdog_timeout_fails_job_and_cleans_up(self):
        process = MagicMock()
        process.wait.side_effect = [subprocess.TimeoutExpired("docker", 220), 0]
        with patch.object(runner.subprocess, "Popen", return_value=process):
            with self.assertRaisesRegex(RuntimeError, "deadline"):
                runner.run_scenario(self.root, "image", "fixture", "solo")
        self.clean.assert_called_once()

    def test_crash_host_requires_successful_guest_cleanup(self):
        processes = [MagicMock(), MagicMock()]
        processes[0].wait.return_value = 0
        processes[1].wait.return_value = 1
        with patch.object(runner.subprocess, "Popen", side_effect=processes) as launch:
            with self.assertRaisesRegex(RuntimeError, "guest exited with status 1"):
                runner.run_scenario(self.root, "image", "fixture", "crash")
        self.assertEqual(launch.call_args_list[0].args[0][-2], "crash-host")


if __name__ == "__main__":
    unittest.main()
