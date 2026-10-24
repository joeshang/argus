#!/usr/bin/env python3
"""Regression: an offline service must close the native host, even with stdin open."""
import os
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix='argus-offline-', dir='/tmp') as directory:
    child = subprocess.Popen([sys.argv[1]], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, env={**os.environ, 'ARGUS_RUNTIME_DIR': directory})
    try:
        try:
            code = child.wait(timeout=2)
        except subprocess.TimeoutExpired:
            raise AssertionError('Helper stayed alive with an offline Native Service and open browser stdin')
        assert code != 0, 'Offline connection must report failure'
        print('PASS: offline Helper exits while browser stdin stays open')
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()
        child.stdin.close()
        child.stdout.close()
        child.stderr.close()
