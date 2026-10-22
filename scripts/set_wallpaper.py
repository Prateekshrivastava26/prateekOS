#!/usr/bin/env python3
"""Convert a chosen picture to PRATEEKOS's 24-bit BMP wallpaper asset."""
import argparse
from pathlib import Path
import os
import tempfile
try:
    from PIL import Image, ImageOps
except ImportError:
    raise SystemExit('Install image support first: sudo apt install python3-pil')
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('image',type=Path)
args=parser.parse_args()
root=Path(__file__).resolve().parent.parent
output=root/'ramfs/wallpaper.bmp'
with Image.open(args.image) as source:
    image=ImageOps.exif_transpose(source).convert('RGBA')
    background=Image.new('RGBA',image.size,'white');background.alpha_composite(image)
    image=background.convert('RGB');image.thumbnail((1600,1000),Image.Resampling.LANCZOS)
    output.parent.mkdir(parents=True,exist_ok=True)
    temporary=None
    try:
        with tempfile.NamedTemporaryFile(dir=output.parent,suffix='.bmp',delete=False) as f:temporary=Path(f.name)
        image.save(temporary,format='BMP');os.replace(temporary,output)
    finally:
        if temporary is not None:temporary.unlink(missing_ok=True)
print('Wallpaper updated. Close QEMU, run make run, then choose Custom in Settings.')
