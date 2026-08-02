#!/usr/bin/env python3
from pathlib import Path

from PIL import Image

root = Path(__file__).resolve().parent.parent
source = Image.open(root / "data" / "logo.bmp").convert("RGB")
edge = min(source.size)
left = (source.width - edge) // 2
top = (source.height - edge) // 2
logo = source.crop((left, top, left + edge, top + edge))
logo.thumbnail((156, 156), Image.Resampling.LANCZOS)

canvas = Image.new("RGB", (164, 314), "white")
position = ((canvas.width - logo.width) // 2, (canvas.height - logo.height) // 2)
canvas.paste(logo, position)
canvas.save(root / "data" / "installer_welcome.bmp", format="BMP")
