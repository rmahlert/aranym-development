#!/usr/bin/env python3
from pathlib import Path

from PIL import Image

root = Path(__file__).resolve().parent.parent
source = Image.open(root / "data" / "logo.bmp").convert("RGB")
edge = min(source.size)
left = (source.width - edge) // 2
top = (source.height - edge) // 2
icon = source.crop((left, top, left + edge, top + edge))
icon.save(root / "src" / "Unix" / "cygwin" / "aranym.ico", format="ICO", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
