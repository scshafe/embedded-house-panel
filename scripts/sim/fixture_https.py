#!/usr/bin/env python3
"""Manage the loopback-only synthetic Lexinet server and ignored simulation CA.

Certificates are private to this checkout. TLS verification remains enabled in
both clients. Nothing is installed in the macOS keychain or production trust.
"""
from __future__ import annotations
import json
import os
import signal
import shutil
import socket
import ssl
import subprocess
import sys
import time
from pathlib import Path
from virtual_panel_vm import ROOT,SIM,EVIDENCE,emit

PORT=9443
TLS=SIM/'tls'


def configuration(guest=False):
    return {'base_url':f'https://10.0.2.2:{PORT}' if guest else f'https://127.0.0.1:{PORT}',
            'profile_ref':'fixture-default','topic_refs':['fixture-systems','fixture-words'],
            'fixture_ca_file':'/var/lib/bhp-sim/fixture-ca.pem' if guest else str(TLS/'certificate.pem'),
            'allow_qemu_host_test_ca':guest}


def openssl_program():
    preferred=Path('/opt/homebrew/opt/openssl@3/bin/openssl')
    program=str(preferred) if preferred.exists() else shutil.which('openssl')
    if not program:raise ValueError('SIM_TLS.OPENSSL_REQUIRED')
    return program


def certificate_expired():
    result=subprocess.run([openssl_program(),'x509','-in',str(TLS/'certificate.pem'),
                           '-noout','-enddate'],check=True,capture_output=True,text=True)
    field,separator,value=result.stdout.strip().partition('=')
    if field!='notAfter' or not separator:raise ValueError('FIXTURE_TLS.CERTIFICATE_INVALID')
    return ssl.cert_time_to_seconds(value)<=time.time()


def server_command():
    return [sys.executable,str(ROOT/'vocabulary_panel/tools/fixture_study_server.py'),
            '--certificate',str(TLS/'certificate.pem'),'--private-key',str(TLS/'private-key.pem'),
            '--state-directory',str(SIM/'fixture-study'),'--port',str(PORT)]


def owned_server_pid():
    """Reject stale PID reuse; never signal a process identified only by its PID."""
    path=SIM/'fixture-server.pid'
    if not path.exists():return None
    pid=int(path.read_text().strip())
    if pid<=1:raise ValueError('FIXTURE_TLS.SERVER_IDENTITY_INVALID')
    result=subprocess.run(['ps','-ww','-p',str(pid),'-o','uid=,stat=,command='],capture_output=True,text=True)
    if result.returncode:return None
    fields=result.stdout.strip().split(None,2)
    if len(fields)!=3 or fields[0]!=str(os.geteuid()):
        raise ValueError('FIXTURE_TLS.SERVER_IDENTITY_INVALID')
    if fields[1].startswith('Z'):return None
    # Framework Python reports its actual app executable through ps on macOS.
    interpreters={sys.executable,str(Path(sys.executable).resolve()),
                  str(Path(sys.base_prefix)/'Resources/Python.app/Contents/MacOS/Python')}
    expected={program+' '+' '.join(server_command()[1:]) for program in interpreters}
    if fields[2] not in expected:
        raise ValueError('FIXTURE_TLS.SERVER_IDENTITY_INVALID')
    return pid


def rotate_expired_identity():
    """Explicitly archive an expired local identity; never silently change trust."""
    if not certificate_expired():raise ValueError('FIXTURE_TLS.CERTIFICATE_NOT_EXPIRED')
    pid=owned_server_pid()
    if pid is not None:
        os.kill(pid,signal.SIGTERM)
        for _ in range(50):
            if owned_server_pid() is None:break
            time.sleep(0.1)
        else:raise ValueError('FIXTURE_TLS.SERVER_STOP_TIMEOUT')
    # An unknown listener must be left alone. Do this before moving any identity.
    with socket.socket(socket.AF_INET,socket.SOCK_STREAM) as probe:
        probe.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
        try:probe.bind(('127.0.0.1',PORT))
        except OSError as error:raise ValueError('FIXTURE_TLS.PORT_IN_USE') from error
    archive=SIM/('tls-expired-'+str(time.time_ns()))
    TLS.rename(archive)
    pid_path=SIM/'fixture-server.pid'
    if pid_path.exists():pid_path.rename(archive/'fixture-server.pid')
    ensure_server()
    return {'archived_identity':str(archive),'guest_reprovision_required':True,
            'native_restart_required':True,'evidence':EVIDENCE}


def ensure_server():
    os.umask(0o077);TLS.mkdir(parents=True,exist_ok=True)
    certificate=TLS/'certificate.pem';key=TLS/'private-key.pem'
    if not certificate.exists() or not key.exists():
        if certificate.exists() or key.exists():raise ValueError('SIM_TLS.PARTIAL_IDENTITY_REQUIRES_INSPECTION')
        subprocess.run([openssl_program(),'req','-x509','-newkey','rsa:2048','-nodes','-sha256','-days','30',
                        '-subj','/CN=House panel synthetic fixture',
                        '-addext','subjectAltName=DNS:localhost,IP:127.0.0.1,IP:10.0.2.2',
                        '-addext','basicConstraints=critical,CA:TRUE',
                        '-keyout',str(key),'-out',str(certificate)],check=True,capture_output=True)
        key.chmod(0o600)
    if certificate_expired():
        raise ValueError('FIXTURE_TLS.EXPIRED: run fixture_https.py --rotate-expired --execute, '
                         'then reprovision the guest and restart the native panel')
    context=ssl.create_default_context(cafile=str(certificate))
    def ready():
        try:
            with socket.create_connection(('127.0.0.1',PORT),timeout=1) as channel:
                with context.wrap_socket(channel,server_hostname='127.0.0.1'):return True
        except (OSError,ssl.SSLError):return False
    if not ready():
        server_state=SIM/'fixture-study';server_state.mkdir(exist_ok=True,mode=0o700)
        with (SIM/'fixture-server.log').open('ab') as log:
            process=subprocess.Popen(server_command(),stdout=log,stderr=log,start_new_session=True)
        (SIM/'fixture-server.pid').write_text(str(process.pid)+'\n')
        for _ in range(50):
            if process.poll() is not None:raise ValueError('SIM_TLS.SERVER_START_FAILED')
            if ready():break
            time.sleep(0.1)
        else:raise ValueError('SIM_TLS.SERVER_START_TIMEOUT')
    path=SIM/'native-lexinet.json';path.write_text(json.dumps(configuration(),indent=2)+'\n')
    guest=SIM/'guest-lexinet.json';guest.write_text(json.dumps(configuration(True),indent=2)+'\n')
    emit('fixture_https_ready',port=PORT,tls_verification=True,production_service=False)
    return path


def main():
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rotate-expired',action='store_true',required=True)
    parser.add_argument('--execute',action='store_true')
    args=parser.parse_args()
    if not args.execute:
        print(json.dumps({'execute':False,'operation':'rotate_expired_fixture_identity',
                          'identity_directory':str(TLS),'preserve_old_identity':True,
                          'guest_reprovision_required':True,'native_restart_required':True,
                          'evidence':EVIDENCE}));return 0
    print(json.dumps(rotate_expired_identity()));return 0


if __name__=='__main__':
    try:raise SystemExit(main())
    except (OSError,ValueError,subprocess.SubprocessError) as error:
        print(json.dumps({'error_code':str(error).split(':')[0],'detail':str(error),
                          'evidence':EVIDENCE}),file=sys.stderr)
        raise SystemExit(1)
