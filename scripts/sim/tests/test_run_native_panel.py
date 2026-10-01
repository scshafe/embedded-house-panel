"""Portable launcher boundaries: dry runs, platform selection and local state."""
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import run_native_panel as launcher


class NativePanelLauncherTests(unittest.TestCase):
    def setUp(self):
        directory=tempfile.TemporaryDirectory();self.addCleanup(directory.cleanup)
        self.root=Path(directory.name).resolve();self.sim=self.root/'.build/sim'
        self.sim.mkdir(parents=True)
        self.source=self.root/'.build/panel-native/simulator/bhp-panel-sim'
        for name,value in (('ROOT',self.root),('SIM',self.sim)):
            patcher=patch.object(launcher,name,value);patcher.start();self.addCleanup(patcher.stop)

    def invoke(self,platform,*arguments):
        output=io.StringIO()
        with patch.object(sys,'platform',platform),patch.object(sys,'argv',['run_native_panel.py',*arguments]),contextlib.redirect_stdout(output):
            result=launcher.main()
        return result,json.loads(output.getvalue())

    def test_linux_dry_run_never_creates_bundle_server_or_state(self):
        with patch.object(launcher,'prepare_bundle') as bundle,patch.object(launcher,'ensure_server') as server,patch.object(launcher.subprocess,'call') as call:
            result,plan=self.invoke('linux')
            self.assertEqual(result,0)
            self.assertEqual(plan['command'][0],str(self.source))
            self.assertIsNone(plan['app_bundle'])
            bundle.assert_not_called();server.assert_not_called();call.assert_not_called()
            self.assertFalse((self.sim/'native-study').exists())

    def test_linux_execute_uses_binary_and_preserves_exit_status(self):
        self.source.parent.mkdir(parents=True);self.source.write_text('test executable')
        with patch.object(launcher,'prepare_bundle') as bundle,patch.object(launcher,'ensure_server') as server,patch.object(launcher.subprocess,'call',return_value=7) as call:
            result,plan=self.invoke('linux','--execute','--launcher-config',str(self.root/'custom-apps.json'))
            self.assertEqual(result,7);bundle.assert_not_called();server.assert_called_once()
            call.assert_called_once_with(plan['command'],cwd=self.root)
            self.assertIn(str(self.root/'custom-apps.json'),plan['command'])
            self.assertTrue((self.sim/'native-study').is_dir())

    def test_mac_execute_retains_signed_bundle_path(self):
        self.source.parent.mkdir(parents=True);self.source.write_text('test executable')
        with patch.object(launcher,'prepare_bundle') as bundle,patch.object(launcher,'ensure_server'),patch.object(launcher.subprocess,'call',return_value=0):
            _,plan=self.invoke('darwin','--execute')
            bundle.assert_called_once_with(self.source,self.sim/'HousePanelSimulator.app')
            self.assertEqual(plan['command'][0],str(self.sim/'HousePanelSimulator.app/Contents/MacOS/bhp-panel-sim'))

    def test_missing_build_fails_before_server_or_durable_state(self):
        with patch.object(launcher,'ensure_server') as server:
            with self.assertRaisesRegex(ValueError,'SIM_NATIVE.BUILD_REQUIRED'):self.invoke('linux','--execute')
            server.assert_not_called();self.assertFalse((self.sim/'native-study').exists())

    def test_unsupported_platform_fails_explicitly(self):
        with self.assertRaisesRegex(ValueError,'SIM_NATIVE.PLATFORM_UNSUPPORTED'):self.invoke('win32')


if __name__=='__main__':unittest.main()
