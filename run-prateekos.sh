#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$project_dir"

# Store the live image on Windows D:, with the original project disk retained.
if [[ -z "${PRATEEKOS_DISK:-}" && ! -d /mnt/d ]]; then
    echo 'D: is not mounted. Set PRATEEKOS_DISK to a writable .img file path.' >&2
    exit 1
fi
prateekos_disk="${PRATEEKOS_DISK:-/mnt/d/prateekos-storage/prateekos-disk.img}"
python3 scripts/prepare_disk.py --source "$project_dir/prateekos-disk.img" --destination "$prateekos_disk"
if [[ "${1:-}" != --no-build ]]; then
    make
fi
# The guest reads local CMOS time; TZ defines the chosen local timezone.
export TZ="${PRATEEKOS_TZ:-Asia/Kolkata}"
exec python3 scripts/host_tools.py -- qemu-system-x86_64 -M pc -m 2G -cdrom template.iso -boot d \
    -drive "file=$prateekos_disk,format=raw,if=ide,index=0" \
    -rtc base=localtime,clock=host
