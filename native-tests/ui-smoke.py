#!/usr/bin/env python3
"""Run the real TUI under a PTY while several clients update monitor state."""
import collections
import fcntl
import os
from pathlib import Path
import pty
import signal
import select
import socket
import struct
import subprocess
import sys
import tempfile
import termios
import threading
import time

with tempfile.TemporaryDirectory(prefix='argus-ui-', dir='/tmp') as directory:
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 32, 140, 0, 0))
    trace = tempfile.TemporaryFile()
    child = subprocess.Popen([sys.argv[1]], stdin=slave, stdout=slave, stderr=trace,
                             env={**os.environ, 'TERM': 'xterm-256color', 'ARGUS_RUNTIME_DIR': directory})
    os.close(slave)
    output = collections.deque(maxlen=128)
    def drain():
        while True:
            try:
                data = os.read(master, 8192)
                if not data:
                    break
                output.append(data)
            except OSError:
                break
    reader = threading.Thread(target=drain, daemon=True)
    reader.start()
    errors = []
    helper = None
    def publish(worker):
        try:
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as peer:
                peer.settimeout(5)
                peer.connect(str(Path(directory) / 'channel.sock'))
                for i in range(2000):
                    message = ('{"browser":"chrome","type":"tab","rendererClientId":%d,'
                               '"url":"https://example.com/","withMetric":true,"requestCount":%d}'
                               % ((i + worker * 1000) % 2000 + 1, i)).encode()
                    peer.sendall(struct.pack('=I', len(message)) + message)
                    if i % 50 == 0:
                        time.sleep(0.01)
        except Exception as error:
            errors.append(error)
    try:
        deadline = time.monotonic() + 10
        while not (Path(directory) / 'channel.sock').exists():
            if child.poll() is not None:
                trace.seek(0)
                raise AssertionError('TUI exited before server was ready: ' + trace.read().decode(errors='replace'))
            assert time.monotonic() < deadline, 'TUI startup timed out'
            time.sleep(0.01)
        if len(sys.argv) > 2:
            helper = subprocess.Popen([sys.argv[2]], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE, bufsize=0,
                                      env={**os.environ, 'ARGUS_RUNTIME_DIR': directory})
            def receive_exact(size):
                result = b''
                while len(result) < size:
                    assert select.select([helper.stdout], [], [], 5)[0], 'Native helper response timed out'
                    chunk = os.read(helper.stdout.fileno(), size - len(result))
                    assert chunk, 'Native helper closed unexpectedly'
                    result += chunk
                return result
            size, = struct.unpack('=I', receive_exact(4))
            assert receive_exact(size) == b'"native service connected"'
            message = b'{"browser":"chrome","type":"tab","rendererClientId":42,"url":"https://example.com/"}'
            helper.stdin.write(struct.pack('=I', len(message)) + message)
        writers = [threading.Thread(target=publish, args=(i,)) for i in range(4)]
        for writer in writers:
            writer.start()
        # Render both process and page-load tabs during incoming updates.
        for _ in range(6):
            os.write(master, b'\x1b[C')
            time.sleep(0.5)
        for writer in writers:
            writer.join(timeout=10)
            assert not writer.is_alive(), 'Client writer hung'
        assert not errors, errors
        os.write(master, b'\x03')
        try:
            code = child.wait(timeout=5)
        except subprocess.TimeoutExpired:
            child.send_signal(signal.SIGINT)
            code = child.wait(timeout=5)
        trace.seek(0)
        diagnostic = trace.read().decode(errors='replace')
        if helper is not None:
            assert helper.wait(timeout=5) > 0, 'Helper did not propagate real service shutdown'
        assert code == 0, f'TUI failed ({code}): {diagnostic}'
        assert 'ThreadSanitizer' not in diagnostic, diagnostic
        assert not (Path(directory) / 'channel.sock').exists(), 'Socket was not removed on orderly TUI shutdown'
        assert b'Argus' in b''.join(output), 'TUI never rendered'
        print('PASS: real TUI renders concurrent snapshots; native bridge propagates shutdown')
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()
        if helper is not None:
            if helper.poll() is None:
                helper.kill()
                helper.wait()
            for stream in (helper.stdin, helper.stdout, helper.stderr):
                stream.close()
        os.close(master)
        reader.join(timeout=2)
        trace.close()
