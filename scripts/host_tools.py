#!/usr/bin/env python3
"""Local QEMU/Windows settings bridge. Only two fixed settings URIs are allowed."""
import argparse
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time

URIS = {'WIFI': 'ms-settings:network-wifi', 'BLUETOOTH': 'ms-settings:bluetooth'}
PREFIX = 'PRATEEKOS/1 '

def find_windows_cmd():
    path = shutil.which('cmd.exe')
    if path:
        return path
    candidate = Path('/mnt/c/Windows/System32/cmd.exe')
    return str(candidate) if candidate.is_file() else None

def windows_ready(command):
    if not command:
        return False
    try:
        result = subprocess.run([command, '/d', '/c', 'exit', '0'], capture_output=True, timeout=5)
        return result.returncode == 0
    except (OSError, subprocess.SubprocessError):
        return False

class HostTools:
    def __init__(self, command=None, ready=None):
        self.command = command or find_windows_cmd()
        self.ready = windows_ready(self.command) if ready is None else ready
        self.last_open = -100.0

    def handshake(self):
        return PREFIX + ('READY WINDOWS' if self.ready else 'READY UNAVAILABLE')

    def handle(self, line):
        if line == PREFIX + 'PING':
            return self.handshake()
        # Exact strings only. No guest-provided URI, executable or shell text.
        action = next((key for key in URIS if line == PREFIX + key), None)
        if action is None:
            return PREFIX + 'ERROR UNKNOWN'
        if not self.ready:
            return PREFIX + 'READY UNAVAILABLE'
        if time.monotonic() - self.last_open < 1.0:
            return PREFIX + 'ERROR BUSY'
        self.last_open = time.monotonic()
        try:
            result = subprocess.run(
                [self.command, '/d', '/c', 'start', URIS[action]],
                capture_output=True, timeout=5,
                cwd='/mnt/c/Windows' if Path('/mnt/c/Windows').is_dir() else None,
            )
            return PREFIX + ('OK ' + action if result.returncode == 0 else 'ERROR OPEN')
        except (OSError, subprocess.SubprocessError):
            return PREFIX + 'ERROR OPEN'

def serve_connection(connection, tools, process):
    connection.settimeout(0.25)
    connection.sendall((tools.handshake() + '\n').encode('ascii'))
    data = bytearray()
    dropping = False
    while process.poll() is None:
        try:
            chunk = connection.recv(256)
        except socket.timeout:
            continue
        if not chunk:
            return
        for byte in chunk:
            if byte == 10:
                if not dropping:
                    response = tools.handle(data.decode('ascii'))
                    connection.sendall((response + '\n').encode('ascii'))
                data.clear()
                dropping = False
            elif byte == 13:
                continue
            elif byte < 32 or byte > 126 or len(data) >= 95:
                data.clear()
                dropping = True
            elif not dropping:
                data.append(byte)

def launch_qemu(command):
    tools = HostTools()
    with tempfile.TemporaryDirectory(prefix='prateekos-host-', dir='/tmp') as runtime:
        path = str(Path(runtime) / 'settings.sock')
        args = command + [
            '-chardev', f'socket,id=prateekhost,path={path},server=on,wait=off',
            '-device', 'isa-serial,chardev=prateekhost,index=1,iobase=0x2f8,irq=3',
        ]
        process = subprocess.Popen(args, umask=0o077)
        connection = None
        try:
            deadline = time.monotonic() + 15
            while process.poll() is None and time.monotonic() < deadline:
                try:
                    candidate = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                except OSError:
                    break
                try:
                    candidate.connect(path)
                    connection = candidate
                    break
                except OSError:
                    candidate.close()
                    time.sleep(0.05)
            if connection is not None:
                try:
                    serve_connection(connection, tools, process)
                except OSError:
                    print('Host settings bridge disconnected; PRATEEKOS can continue.')
            else:
                print('Host settings bridge unavailable; PRATEEKOS can continue.')
            return process.wait()
        except KeyboardInterrupt:
            process.terminate()
            try:
                return process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                return process.wait()
        finally:
            if connection is not None:
                connection.close()

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--wifi', action='store_true')
    group.add_argument('--bluetooth', action='store_true')
    parser.add_argument('qemu', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.wifi or args.bluetooth:
        reply = HostTools().handle(PREFIX + ('WIFI' if args.wifi else 'BLUETOOTH'))
        print(reply)
        raise SystemExit(0 if reply.startswith(PREFIX + 'OK ') else 1)
    command = args.qemu[1:] if args.qemu[:1] == ['--'] else args.qemu
    if not command:
        parser.error('Pass --wifi, --bluetooth or -- followed by a QEMU command.')
    try:
        raise SystemExit(launch_qemu(command))
    except OSError as exc:
        parser.exit(1, f'Could not start QEMU: {exc}\n')
