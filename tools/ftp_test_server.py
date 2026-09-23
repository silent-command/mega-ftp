#!/usr/bin/env python3
"""A local FTP server for testing the client: anonymous, read and write,
serving a directory of known files on port 2121 (no root needed).

    build/venv/bin/python tools/ftp_test_server.py [dir] [port]

Needs pyftpdlib (python3 -m venv build/venv && build/venv/bin/pip install pyftpdlib)."""
import os, sys, tempfile
from pyftpdlib.authorizers import DummyAuthorizer
from pyftpdlib.handlers import FTPHandler
from pyftpdlib.servers import FTPServer

root = sys.argv[1] if len(sys.argv) > 1 else tempfile.mkdtemp(prefix="ftpc-")
port = int(sys.argv[2]) if len(sys.argv) > 2 else 2121
if not os.listdir(root):
    os.makedirs(os.path.join(root, "docs"), exist_ok=True)
    with open(os.path.join(root, "hello.txt"), "w") as f: f.write("hello from the mac\r\n" * 20)
    with open(os.path.join(root, "pattern.bin"), "wb") as f: f.write(bytes((i * 31 + 7) & 0xff for i in range(20000)))
    with open(os.path.join(root, "docs", "readme.txt"), "w") as f: f.write("a file in a subdirectory\r\n")
auth = DummyAuthorizer(); auth.add_anonymous(root, perm="elradfmwMT")
h = FTPHandler; h.authorizer = auth; h.passive_ports = range(60000, 60010)
print(f"serving {root} on port {port}", flush=True)
FTPServer(("0.0.0.0", port), h).serve_forever()
