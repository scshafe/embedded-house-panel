"""Exercise the public bootstrap CLI without installing packages on the host."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[3]
SCRIPT=ROOT/'scripts/environment/bootstrap_arch_build_host.sh'


class ArchBootstrapTests(unittest.TestCase):
    def setUp(self):
        directory=tempfile.TemporaryDirectory();self.addCleanup(directory.cleanup)
        self.directory=Path(directory.name);self.marker=self.directory/'pacman-was-called'
        # Force the platform guard independent of the test runner's operating system.
        for name,body in (('uname','printf "Darwin\\n"'),('pacman','touch "'+str(self.marker)+'"; exit 99')):
            path=self.directory/name;path.write_text('#!/bin/sh\n'+body+'\n');path.chmod(0o755)
        self.env={**os.environ,'PATH':str(self.directory)+os.pathsep+os.environ['PATH']}

    def run_cli(self,*args):
        return subprocess.run([str(SCRIPT),*args],env=self.env,capture_output=True,text=True,timeout=10)

    def test_default_dry_run_reports_complete_plan_without_package_execution(self):
        result=self.run_cli();self.assertEqual(result.returncode,0,result.stderr)
        plan=json.loads(result.stdout)
        self.assertEqual(plan['message'],'bootstrap_dry_run')
        self.assertIn('pacman -Syu --needed',plan['command'])
        for package in ('sdl2-compat','nlohmann-json','libpng','libdrm','shellcheck','dtc'):
            self.assertIn(package,plan['packages'].split())
        self.assertFalse(self.marker.exists())

    def test_execute_refuses_non_arch_platform_before_package_execution(self):
        result=self.run_cli('--execute')
        self.assertNotEqual(result.returncode,0)
        self.assertIn('BOOTSTRAP.ARCH_X86_64_REQUIRED',result.stdout+result.stderr)
        self.assertFalse(self.marker.exists())

    def test_unknown_or_extra_arguments_cannot_trigger_installation(self):
        for args in (('--unexpected',),('--execute','--unexpected')):
            with self.subTest(args=args):self.assertEqual(self.run_cli(*args).returncode,2)
        self.assertFalse(self.marker.exists())


if __name__=='__main__':unittest.main()
