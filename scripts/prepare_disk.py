#!/usr/bin/env python3
"""Grow a regular raw-image file to 5 GB. Never shrink or format a disk."""
import argparse
import fcntl
import os
from pathlib import Path
import shutil
import stat
import tempfile

DISK_BYTES = 5_000_000_000

def lock_image(stream):
    try:
        # QEMU's byte-range image locks conflict with this full-file POSIX lock.
        fcntl.lockf(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError as exc:
        raise RuntimeError('Disk is in use. Close QEMU before updating storage.') from exc

def regular_file(path):
    if path.is_symlink() or not stat.S_ISREG(path.stat().st_mode):
        raise RuntimeError(f'Refusing non-regular disk image: {path}')

def prepare(source, destination):
    source = Path(source).absolute()
    destination = Path(destination).absolute()
    destination.parent.mkdir(parents=True, exist_ok=True)
    if not destination.exists():
        # Stage a copy, so interruption cannot publish a partly copied image.
        tmp_name = None
        try:
            with tempfile.NamedTemporaryFile(prefix='.prateekos-copy-', dir=destination.parent, delete=False) as tmp:
                tmp_name = tmp.name
                if source.exists():
                    regular_file(source)
                    with source.open('r+b') as original:
                        lock_image(original)
                        shutil.copyfileobj(original, tmp, 1024*1024)
                    print(f'Copied saved disk from {source}')
                else:
                    print('New blank virtual disk. Initialize it once with diskformat in PRATEEKOS.')
                tmp.flush()
                os.fsync(tmp.fileno())
            # Fail rather than replace a disk created by another invocation.
            os.link(tmp_name, destination)
        finally:
            if tmp_name is not None:
                Path(tmp_name).unlink(missing_ok=True)
    regular_file(destination)
    with destination.open('r+b') as disk:
        lock_image(disk)
        before = os.fstat(disk.fileno()).st_size
        if before < DISK_BYTES:
            disk.truncate(DISK_BYTES)
            disk.flush()
            os.fsync(disk.fileno())
        size = os.fstat(disk.fileno()).st_size
    print(f'Virtual disk: {destination}\nCapacity: {size:,} bytes (minimum 5 GB). Existing sectors preserved.')
    return destination

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True)
    parser.add_argument('--destination', required=True)
    args = parser.parse_args()
    try:
        prepare(args.source, args.destination)
    except (OSError, RuntimeError) as exc:
        parser.exit(1, f'Storage preparation failed: {exc}\n')
