#!/usr/bin/env python3
"""Exercise the real Unix server, Native Messaging helper and monitor consumer."""
import contextlib
import errno
import json
import os
from pathlib import Path
import selectors
import socket
import stat
import struct
import subprocess
import sys
import tempfile
import time
import unittest

SERVER, HELPER = sys.argv[1:3]
sys.argv[1:] = []
MAX_FRAME = 1024 * 1024
MESSAGE = json.dumps({'type': 'tab', 'browser': 'chrome', 'rendererClientId': 42,
                      'url': 'https://example.com/中文', 'withMetric': True, 'requestCount': 2},
                     ensure_ascii=False, separators=(',', ':')).encode()

def framed(data):
    return struct.pack('=I', len(data)) + data

def exact(stream, length):
    data = b''
    deadline = time.monotonic() + 5
    while len(data) < length:
        remaining = deadline - time.monotonic()
        assert remaining > 0, 'Timed out receiving a frame'
        with selectors.DefaultSelector() as selector:
            selector.register(stream, selectors.EVENT_READ)
            assert selector.select(remaining), 'Timed out receiving a frame'
        part = os.read(stream.fileno(), length - len(data))
        assert part, 'Unexpected EOF'
        data += part
    return data

def receive(stream):
    size, = struct.unpack('=I', exact(stream, 4))
    assert 0 < size <= MAX_FRAME
    return exact(stream, size)

def line(stream):
    # Use unbuffered Popen pipes, so selector readiness matches the reads.
    data = b''
    while not data.endswith(b'\n'):
        data += exact(stream, 1)
    return data.rstrip(b'\n')

@contextlib.contextmanager
def process(executable, directory):
    child = subprocess.Popen([executable], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, bufsize=0,
                             env={**os.environ, 'ARGUS_RUNTIME_DIR': str(directory)})
    try:
        yield child
    finally:
        if child.poll() is None:
            child.kill()
            child.wait(timeout=5)
        for stream in (child.stdin, child.stdout, child.stderr):
            stream.close()

@contextlib.contextmanager
def server(directory):
    with process(SERVER, directory) as child:
        assert line(child.stdout) == b'READY', child.stderr.read().decode()
        try:
            yield child
        finally:
            if child.poll() is None:
                child.stdin.close()
                assert child.wait(timeout=5) == 0, child.stderr.read().decode()

def connect(directory):
    peer = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    peer.settimeout(5)
    peer.connect(str(directory / 'channel.sock'))
    assert receive(peer) == b'native service connected'
    return peer

class NativeIntegration(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='argus-ipc-', dir='/tmp')
        self.directory = Path(self.temporary.name)
        self.addCleanup(self.temporary.cleanup)

    def test_fragmented_headers_bodies_and_concatenated_frames(self):
        with server(self.directory) as service, connect(self.directory) as peer:
            data = framed(MESSAGE)
            for split in range(1, len(data)):
                peer.sendall(data[:split])
                time.sleep(0.001)
                peer.sendall(data[split:])
                self.assertEqual(line(service.stdout), b'MESSAGE ' + MESSAGE)
            peer.sendall(data * 100)
            for _ in range(100):
                self.assertEqual(line(service.stdout), b'MESSAGE ' + MESSAGE)

    def test_invalid_and_truncated_clients_do_not_kill_service(self):
        with server(self.directory) as service:
            for invalid in (struct.pack('=I', 0), struct.pack('=I', MAX_FRAME + 1),
                            struct.pack('=I', 0xffffffff), b'\x05', struct.pack('=I', 100) + b'partial'):
                with connect(self.directory) as bad:
                    bad.sendall(invalid)
                    try:
                        bad.shutdown(socket.SHUT_WR)
                    except OSError as error:
                        self.assertEqual(error.errno, errno.ENOTCONN)  # Server already rejected this frame.
                    try:
                        self.assertEqual(bad.recv(1), b'')
                    except ConnectionResetError:
                        pass
                with connect(self.directory) as good:
                    good.sendall(framed(MESSAGE))
                    self.assertEqual(line(service.stdout), b'MESSAGE ' + MESSAGE)
            with connect(self.directory) as peer:
                for invalid_json in (b'[]', b'null', b'{"type":"tab","browser":false}',
                                     b'{"type":"tab","browser":"chrome","rendererClientId":{}}'):
                    peer.sendall(framed(invalid_json))
                    self.assertEqual(line(service.stdout), b'MESSAGE ' + invalid_json)
                peer.sendall(framed(MESSAGE))
                self.assertEqual(line(service.stdout), b'MESSAGE ' + MESSAGE)

    def test_exclusive_owner_permissions_and_clean_shutdown(self):
        with server(self.directory) as service:
            path = self.directory / 'channel.sock'
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)
            with process(SERVER, self.directory) as duplicate:
                self.assertNotEqual(duplicate.wait(timeout=5), 0)
            with connect(self.directory) as peer:
                peer.sendall(framed(MESSAGE))
                self.assertEqual(line(service.stdout), b'MESSAGE ' + MESSAGE)
        self.assertFalse((self.directory / 'channel.sock').exists())
        with server(self.directory):  # The stable lock inode can be reused.
            pass

    def test_stale_socket_is_recovered_but_files_symlinks_and_public_dirs_are_rejected(self):
        path = self.directory / 'channel.sock'
        stale = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        stale.bind(str(path))
        stale.close()
        with server(self.directory):
            pass
        path.write_text('keep this file')
        with process(SERVER, self.directory) as rejected:
            self.assertNotEqual(rejected.wait(timeout=5), 0)
        self.assertEqual(path.read_text(), 'keep this file')
        path.unlink()
        path.symlink_to(self.directory / 'missing')
        with process(SERVER, self.directory) as rejected:
            self.assertNotEqual(rejected.wait(timeout=5), 0)
        self.assertTrue(path.is_symlink())
        path.unlink()
        self.directory.chmod(0o755)
        with process(SERVER, self.directory) as rejected:
            self.assertNotEqual(rejected.wait(timeout=5), 0)
        self.directory.chmod(0o700)
        alias = self.directory / 'alias'
        alias.symlink_to(self.directory, target_is_directory=True)
        with process(SERVER, alias) as rejected:
            self.assertNotEqual(rejected.wait(timeout=5), 0)

    def test_shutdown_preserves_a_replacement_path(self):
        path = self.directory / 'channel.sock'
        with server(self.directory):
            path.unlink()
            path.write_text('replacement must survive')
        self.assertEqual(path.read_text(), 'replacement must survive')

    def test_shutdown_closes_live_clients(self):
        peers = []
        try:
            with server(self.directory):
                peers = [connect(self.directory) for _ in range(16)]
            for peer in peers:
                self.assertEqual(peer.recv(1), b'')
        finally:
            for peer in peers:
                peer.close()

    def test_helper_bridge_browser_exit_and_service_restart(self):
        with server(self.directory) as service:
            with process(HELPER, self.directory) as helper:
                self.assertEqual(json.loads(receive(helper.stdout)), 'native service connected')
                for byte in framed(MESSAGE):
                    helper.stdin.write(bytes([byte]))
                self.assertEqual(line(service.stdout), b'MESSAGE ' + MESSAGE)
                helper.stdin.close()
                self.assertEqual(helper.wait(timeout=5), 0)
            # Browser closing its native port only kills that Helper.
            with process(HELPER, self.directory) as helper:
                receive(helper.stdout)
                service.stdin.close()
                self.assertEqual(service.wait(timeout=5), 0)
                self.assertNotEqual(helper.wait(timeout=5), 0)  # stdin still open.
        # A replacement Helper reaches the restarted service, as extension retry does.
        with server(self.directory) as service, process(HELPER, self.directory) as helper:
            receive(helper.stdout)
            helper.stdin.write(framed(MESSAGE))
            self.assertEqual(line(service.stdout), b'MESSAGE ' + MESSAGE)

    def test_browser_truncated_message_and_closed_stdout_exit_helper(self):
        with server(self.directory):
            with process(HELPER, self.directory) as helper:
                receive(helper.stdout)
                helper.stdin.write(struct.pack('=I', 100) + b'partial')
                helper.stdin.close()
                self.assertNotEqual(helper.wait(timeout=5), 0)
            with process(HELPER, self.directory) as helper:
                helper.stdout.close()
                code = helper.wait(timeout=5)
                self.assertGreater(code, 0)  # Ordinary error, not termination by SIGPIPE.

    def test_browser_eof_drains_a_large_complete_frame(self):
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as listener:
            listener.bind(str(self.directory / 'channel.sock'))
            listener.listen(1)
            listener.settimeout(5)
            with process(HELPER, self.directory) as helper:
                peer, _ = listener.accept()
                with peer:
                    payload = b'x' * MAX_FRAME
                    helper.stdin.write(framed(payload))
                    helper.stdin.close()
                    self.assertEqual(receive(peer), payload)
                    self.assertEqual(helper.wait(timeout=5), 0)

    def test_browser_exit_is_bounded_when_service_stops_reading(self):
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as listener:
            listener.bind(str(self.directory / 'channel.sock'))
            listener.listen(1)
            listener.settimeout(5)
            with process(HELPER, self.directory) as helper:
                peer, _ = listener.accept()
                with peer:
                    peer.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024)
                    for _ in range(4):
                        helper.stdin.write(framed(b'x' * MAX_FRAME))
                    helper.stdin.close()
                    self.assertGreater(helper.wait(timeout=5), 0)

    def test_service_messages_are_decoded_incrementally_in_helper(self):
        listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.addCleanup(listener.close)
        listener.bind(str(self.directory / 'channel.sock'))
        listener.listen(1)
        listener.settimeout(5)
        with process(HELPER, self.directory) as helper:
            peer, _ = listener.accept()
            with peer:
                for byte in framed('service 中文'.encode()):
                    peer.sendall(bytes([byte]))
                self.assertEqual(json.loads(receive(helper.stdout)), 'service 中文')
                peer.sendall(struct.pack('=I', MAX_FRAME + 1))
                self.assertNotEqual(helper.wait(timeout=5), 0)

if __name__ == '__main__':
    unittest.main(verbosity=2)
