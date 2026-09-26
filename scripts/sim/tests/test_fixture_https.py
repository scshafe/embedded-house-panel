"""Local-only fixture identity expiry and explicit recovery boundaries."""
import contextlib
import io
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock,patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import fixture_https as fixture


class FixtureTlsTests(unittest.TestCase):
    def setUp(self):
        self.directory=tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.sim=Path(self.directory.name)
        self.tls=self.sim/'tls';self.tls.mkdir()
        (self.tls/'certificate.pem').write_text('synthetic-old-certificate')
        (self.tls/'private-key.pem').write_text('synthetic-old-key')
        for name,value in (('SIM',self.sim),('TLS',self.tls)):
            patcher=patch.object(fixture,name,value);patcher.start();self.addCleanup(patcher.stop)

    def test_expiration_reads_openssl_date(self):
        response=subprocess.CompletedProcess([],0,'notAfter=Jan  1 00:00:00 2000 GMT\n','')
        with patch.object(fixture,'openssl_program',return_value='/fixture/openssl'), \
             patch.object(fixture.subprocess,'run',return_value=response) as command, \
             patch.object(fixture.time,'time',return_value=1_000_000_000):
            self.assertTrue(fixture.certificate_expired())
        self.assertEqual(command.call_args.args[0],['/fixture/openssl','x509','-in',
            str(self.tls/'certificate.pem'),'-noout','-enddate'])

    def test_expired_identity_fails_before_spawn_or_network(self):
        old_mask=os.umask(0o077);self.addCleanup(os.umask,old_mask)
        with patch.object(fixture,'certificate_expired',return_value=True), \
             patch.object(fixture.subprocess,'Popen') as spawn, \
             patch.object(fixture.socket,'create_connection') as connect:
            with self.assertRaisesRegex(ValueError,'FIXTURE_TLS.EXPIRED'):
                fixture.ensure_server()
            spawn.assert_not_called();connect.assert_not_called()
        self.assertEqual((self.tls/'certificate.pem').read_text(),'synthetic-old-certificate')

    def test_valid_identity_is_never_rotated(self):
        with patch.object(fixture,'certificate_expired',return_value=False), \
             patch.object(fixture,'owned_server_pid') as identity:
            with self.assertRaisesRegex(ValueError,'CERTIFICATE_NOT_EXPIRED'):
                fixture.rotate_expired_identity()
            identity.assert_not_called()

    def test_stale_pid_reuse_is_never_signalled(self):
        (self.sim/'fixture-server.pid').write_text('123\n')
        response=subprocess.CompletedProcess([],0,str(os.geteuid())+' S /bin/sleep 30\n','')
        with patch.object(fixture,'certificate_expired',return_value=True), \
             patch.object(fixture.subprocess,'run',return_value=response), \
             patch.object(fixture.os,'kill') as kill:
            with self.assertRaisesRegex(ValueError,'SERVER_IDENTITY_INVALID'):
                fixture.rotate_expired_identity()
            kill.assert_not_called()
        self.assertTrue(self.tls.exists())

    def test_exact_framework_process_and_dead_process_identity(self):
        (self.sim/'fixture-server.pid').write_text('123\n')
        interpreter=str(Path(sys.base_prefix)/'Resources/Python.app/Contents/MacOS/Python')
        command=interpreter+' '+' '.join(fixture.server_command()[1:])
        response=subprocess.CompletedProcess([],0,str(os.geteuid())+' S '+command+'\n','')
        with patch.object(fixture.subprocess,'run',return_value=response):
            self.assertEqual(fixture.owned_server_pid(),123)
        response.stdout=str(os.geteuid())+' Z <defunct>\n'
        with patch.object(fixture.subprocess,'run',return_value=response):
            self.assertIsNone(fixture.owned_server_pid())
        response.stdout=str(os.geteuid()+1)+' S '+command+'\n'
        with patch.object(fixture.subprocess,'run',return_value=response):
            with self.assertRaisesRegex(ValueError,'SERVER_IDENTITY_INVALID'):
                fixture.owned_server_pid()

    def test_unknown_listener_preserves_identity(self):
        channel=Mock();channel.bind.side_effect=OSError('address already in use')
        with patch.object(fixture,'certificate_expired',return_value=True), \
             patch.object(fixture,'owned_server_pid',return_value=None), \
             patch.object(fixture.socket,'socket') as socket, \
             patch.object(fixture,'ensure_server') as start:
            socket.return_value.__enter__.return_value=channel
            with self.assertRaisesRegex(ValueError,'PORT_IN_USE'):
                fixture.rotate_expired_identity()
            start.assert_not_called()
        self.assertTrue(self.tls.exists())

    def test_explicit_rotation_stops_exact_pid_and_archives_identity(self):
        (self.sim/'fixture-server.pid').write_text('123\n')
        with patch.object(fixture,'certificate_expired',return_value=True), \
             patch.object(fixture,'owned_server_pid',side_effect=[123,None]), \
             patch.object(fixture.os,'kill') as kill, \
             patch.object(fixture.socket,'socket'), \
             patch.object(fixture,'ensure_server') as start:
            result=fixture.rotate_expired_identity()
            kill.assert_called_once_with(123,signal.SIGTERM)
            start.assert_called_once_with()
        archive=Path(result['archived_identity'])
        self.assertEqual((archive/'certificate.pem').read_text(),'synthetic-old-certificate')
        self.assertEqual((archive/'private-key.pem').read_text(),'synthetic-old-key')
        self.assertEqual((archive/'fixture-server.pid').read_text(),'123\n')
        self.assertTrue(result['guest_reprovision_required'])
        self.assertTrue(result['native_restart_required'])

    def test_failed_shutdown_never_archives_or_force_kills(self):
        with patch.object(fixture,'certificate_expired',return_value=True), \
             patch.object(fixture,'owned_server_pid',return_value=123), \
             patch.object(fixture.os,'kill') as kill, \
             patch.object(fixture.time,'sleep'), \
             patch.object(fixture,'ensure_server') as start:
            with self.assertRaisesRegex(ValueError,'SERVER_STOP_TIMEOUT'):
                fixture.rotate_expired_identity()
            kill.assert_called_once_with(123,signal.SIGTERM)
            start.assert_not_called()
        self.assertTrue(self.tls.exists())

    def test_rotation_dry_run_has_no_effect(self):
        output=io.StringIO()
        with patch.object(sys,'argv',['fixture_https.py','--rotate-expired']), \
             patch.object(fixture,'rotate_expired_identity') as rotate, \
             contextlib.redirect_stdout(output):
            self.assertEqual(fixture.main(),0)
            rotate.assert_not_called()
        self.assertFalse(json.loads(output.getvalue())['execute'])


if __name__=='__main__':unittest.main()
