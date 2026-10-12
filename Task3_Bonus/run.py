#!/usr/bin/env python3
"""Run with standard Node.js, or use an existing Codex bundled Node on this Mac."""
import os
from pathlib import Path
import shutil
import sys

root = Path(__file__).resolve().parent
node = os.environ.get('NODE') or shutil.which('node')
bundled = Path.home() / '.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node'
if not node and bundled.is_file():
    node = str(bundled)
if not node:
    sys.exit('Install Node.js 20 or newer, then run python3 run.py again. No npm install is required.')
os.environ.setdefault('PYTHON', sys.executable)
os.chdir(root)
os.execv(node, [node, str(root / 'server.mjs')])
