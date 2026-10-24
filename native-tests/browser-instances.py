#!/usr/bin/env python3
"""Two real process trees with colliding renderer/tab IDs, plus two profiles."""
import contextlib
import json
import os
from pathlib import Path
import select
import shutil
import subprocess
import sys
import tempfile
import time

server_path, helper_path, fixture_path = sys.argv[1:]

buffers = {}
def line(stream, timeout=15):
    pending = buffers.setdefault(stream.fileno(), bytearray())
    deadline = time.monotonic() + timeout
    while b'\n' not in pending:
        assert select.select([stream], [], [], max(0, deadline-time.monotonic()))[0], 'Timed out'
        chunk = os.read(stream.fileno(), 8192)
        assert chunk, 'Unexpected process exit'
        pending.extend(chunk)
    end = pending.index(b'\n')
    result = bytes(pending[:end])
    del pending[:end+1]
    return result.decode()

with tempfile.TemporaryDirectory(prefix='argus-instances-', dir='/tmp') as directory:
    root = Path(directory)
    shutil.copy2(fixture_path, root/'Google Chrome')
    shutil.copy2(fixture_path, root/'Google Chrome Helper')
    env = {**os.environ, 'ARGUS_RUNTIME_DIR': directory}
    with contextlib.ExitStack() as cleanup:
        def start(args):
            child = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, bufsize=0)
            def stop():
                child.stdin.close()
                try: child.wait(timeout=5)
                except subprocess.TimeoutExpired: child.kill(); child.wait()
                child.stdout.close(); child.stderr.close()
            cleanup.callback(stop)
            return child
        service = start([server_path, '--monitor'])
        assert line(service.stdout) == 'READY'
        one = start([str(root/'Google Chrome'), helper_path, str(root/'Google Chrome Helper')])
        two = start([str(root/'Google Chrome'), helper_path, str(root/'Google Chrome Helper')])
        first, second = json.loads(line(one.stdout)), json.loads(line(two.stdout))
        def send(child, source, url=None, generation=1):
            records = [] if url is None else [{'tabId':1,'frameId':0,'parentFrameId':-1,'documentId':'document',
                'rendererClientId':7,'url':url,'isSubframe':False,'withMetric':True,'requestCount':3}]
            value = {'type':'snapshot','protocolVersion':2,'browser':'chrome','sourceId':source,'generation':generation,
                     'partIndex':0,'partCount':1,'frames':records}
            child.stdin.write(json.dumps(value).encode()+b'\n')
        def sample_until(check):
            deadline = time.monotonic()+15
            while time.monotonic() < deadline:
                value = line(service.stdout)
                if value.startswith('SAMPLE '):
                    rows = {row['pid']:row for row in json.loads(value[7:])}
                    if check(rows): return rows
            raise AssertionError('Browser mapping did not converge')
        send(one,'profile-a','https://one-a.test/')
        send(one,'profile-b','https://one-b.test/')
        send(two,'profile-a','https://two-a.test/')
        rows = sample_until(lambda rows: len(rows.get(first['renderer'],{}).get('frames',[])) == 2 and
                            len(rows.get(second['renderer'],{}).get('frames',[])) == 1)
        assert rows[first['renderer']]['browserPid'] == first['pid']
        assert {frame['url'] for frame in rows[first['renderer']]['frames']} == {'https://one-a.test/','https://one-b.test/'}
        assert rows[second['renderer']]['frames'][0]['url'] == 'https://two-a.test/'
        one.stdin.write(b'drop-a\n')
        sample_until(lambda rows: [f['url'] for f in rows.get(first['renderer'],{}).get('frames',[])] == ['https://one-b.test/'])
        send(two,'profile-a',None,2)
        sample_until(lambda rows: second['renderer'] in rows and not rows[second['renderer']]['frames'])
        one.stdin.close()
        assert one.wait(timeout=5) == 0
        sample_until(lambda rows: first['renderer'] not in rows)
        print('PASS: real browser roots/profiles with colliding renderer IDs; empty snapshots, disconnect and root exit cleanup')
