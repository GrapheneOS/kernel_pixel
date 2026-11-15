#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

"""Sign the testing archive's modules with the matching GKI build key."""

import argparse
from pathlib import Path
import subprocess
import tempfile
import zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("gki_out", type=Path)
parser.add_argument("archive", type=Path)
args = parser.parse_args()

with tempfile.TemporaryDirectory(dir=args.archive.parent) as temporary:
    temporary = Path(temporary)
    output = temporary / args.archive.name
    module = temporary / "module.ko"
    with zipfile.ZipFile(args.archive) as source, zipfile.ZipFile(output, "w") as target:
        for entry in source.infolist():
            data = source.read(entry)
            if entry.filename.endswith(".ko"):
                module.write_bytes(data)
                subprocess.run([
                    str(args.gki_out / "scripts/sign-file"), "sha256",
                    str(args.gki_out / "certs/signing_key.pem"),
                    str(args.gki_out / "certs/signing_key.x509"), str(module),
                ], check=True)
                data = module.read_bytes()
            target.writestr(entry, data)
    output.replace(args.archive)
