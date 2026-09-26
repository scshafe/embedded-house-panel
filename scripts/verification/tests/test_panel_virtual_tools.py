"""Negative guarantees for screenshot comparison and dry-run emulated controls."""
import json
import socket
import sys
import tempfile
import threading
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'scripts/sim'))
from panel_screenshots import write_png,read_png,compare_png
from panel_ctl import QmpClient,coordinates,run_action,pointer_events,select_tablet
from virtual_panel_vm import qemu_command

class Screenshots(unittest.TestCase):
    def test_exact_comparison_tolerance_and_failure_artifact(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);a=root/'a.png';b=root/'b.png';diff=root/'diff.png'
            write_png(a,2,1,bytes((10,20,30,255,40,50,60,255)))
            self.assertEqual(read_png(a)[:2],(2,1))
            self.assertTrue(compare_png(a,a,diff)['passed']);self.assertFalse(diff.exists())
            write_png(b,2,1,bytes((11,20,30,255,40,50,60,255)))
            result=compare_png(a,b,diff)
            self.assertFalse(result['passed']);self.assertEqual(result['changed_pixels'],1)
            self.assertTrue(diff.exists());self.assertTrue(compare_png(a,b,diff,1)['passed'])
            write_png(b,1,1,bytes((0,0,0,255)))
            self.assertFalse(compare_png(a,b,diff)['passed']);self.assertTrue(diff.exists())
    def test_corrupt_crc_and_invalid_budget(self):
        with tempfile.TemporaryDirectory() as directory:
            image=Path(directory)/'bad.png';write_png(image,1,1,bytes((0,0,0,255)))
            value=bytearray(image.read_bytes());value[29]^=1;image.write_bytes(value)
            with self.assertRaisesRegex(ValueError,'CRC_INVALID'):read_png(image)
            with self.assertRaisesRegex(ValueError,'TOLERANCE_INVALID'):compare_png(image,image,image,-1)

class Controls(unittest.TestCase):
    def test_exact_tablet_selected_even_if_reported_current(self):
        class Monitor:
            def __init__(self,mice):self.mice=mice;self.commands=[]
            def command(self,name,arguments=None):
                self.commands.append((name,arguments))
                return self.mice if name=='query-mice' else ''
        tablet={'name':'QEMU Virtio Tablet','absolute':True,'current':True,'index':7}
        monitor=Monitor([tablet]);self.assertEqual(select_tablet(monitor),7)
        self.assertEqual(monitor.commands[-1],('human-monitor-command',{'command-line':'mouse_set 7'}))
        for mice in ([],[tablet,tablet]):
            with self.assertRaisesRegex(ValueError,'EXACT_TABLET_REQUIRED'):select_tablet(Monitor(mice))
    def test_bounds_and_strict_dry_run_validation(self):
        self.assertEqual(coordinates(1023,599),(32767,32767))
        for x,y in ((1024,0),(-1,0),(0,600),(True,0)):
            with self.assertRaises(ValueError):coordinates(x,y)
        self.assertFalse(run_action({'type':'tap','x':1,'y':2},False)['execute'])
        for action in ({'type':'presence','present':1},{'type':'select_touch','protocol':'fake'},
                       {'type':'screenshot','name':'../bad'},{'type':'status','extra':True}):
            with self.assertRaises(ValueError):run_action(action,False)
    def test_multitouch_axes_are_separate_and_release_tracking_id(self):
        begin=pointer_events(500,300,True,True)
        self.assertEqual([event['data']['type'] for event in begin],['begin','data','data'])
        self.assertEqual([event['data']['axis'] for event in begin[1:]],['x','y'])
        self.assertEqual(pointer_events(500,300,False,True,'end')[0]['data']['tracking-id'],-1)
        self.assertEqual(len(pointer_events(501,301,True,True,'update')),2)
    def test_qmp_reply_identity_and_interleaved_events(self):
        # Short path is required by Darwin sockaddr_un.
        with tempfile.TemporaryDirectory(prefix='qmp-',dir='/tmp') as directory:
            path=Path(directory)/'s'
            server=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);server.bind(str(path));server.listen(1)
            failures=[]
            def serve():
                try:
                    connection,_=server.accept()
                    with connection,connection.makefile('rwb') as stream:
                        stream.write(b'{"QMP":{}}\n');stream.flush()
                        for index in range(2):
                            request=json.loads(stream.readline())
                            reply={'return':{},'id':request['id'] if index==0 else 999}
                            stream.write(b'{"event":"IGNORED"}\n'+(json.dumps(reply)+'\n').encode());stream.flush()
                except Exception as error:failures.append(error)
            thread=threading.Thread(target=serve);thread.start()
            with QmpClient(path) as client:
                with self.assertRaisesRegex(ValueError,'ID_MISMATCH'):client.command('query-status')
            thread.join(3);server.close();self.assertFalse(thread.is_alive());self.assertFalse(failures)
    def test_explicit_simulation_qemu_contract(self):
        command=qemu_command()
        for argument in ('hvf','host','cocoa','virtio-tablet-pci','virtio-multitouch-pci','virtio-keyboard-pci'):
            self.assertIn(argument,command)
        self.assertIn('virtio-gpu-pci,id=video0,xres=1024,yres=600',command)

if __name__=='__main__':unittest.main()
