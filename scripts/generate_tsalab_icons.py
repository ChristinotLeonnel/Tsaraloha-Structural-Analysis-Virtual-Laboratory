"""Génère les icônes raster de TSALab à partir des SVG officiels (resources/icons/TSALab*.svg).

Usage (depuis n'importe quel dossier) : python scripts/generate_tsalab_icons.py
Dépendances : PySide6 (rendu SVG Qt, identique à celui de l'application), Pillow (écriture .ico).
Produit : resources/icons/TSALab.ico (16–256 px, icône de l'exécutable et des fichiers .tsalab)
          resources/icons/TSALab_256.png (aperçu)
"""
import io
import pathlib

from PIL import Image
from PySide6.QtCore import QBuffer, QByteArray, QIODevice, Qt
from PySide6.QtGui import QGuiApplication, QImage, QPainter
from PySide6.QtSvg import QSvgRenderer

ROOT = pathlib.Path(__file__).resolve().parent.parent
ICONS = ROOT / "resources" / "icons"
SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]


def render(renderer: QSvgRenderer, size: int) -> Image.Image:
    img = QImage(size, size, QImage.Format_ARGB32)
    img.fill(Qt.transparent)
    p = QPainter(img)
    p.setRenderHint(QPainter.Antialiasing)
    p.setRenderHint(QPainter.SmoothPixmapTransform)
    renderer.render(p)
    p.end()
    data = QByteArray()
    buf = QBuffer(data)
    buf.open(QIODevice.WriteOnly)
    img.save(buf, "PNG")
    return Image.open(io.BytesIO(bytes(data))).convert("RGBA")


def main() -> None:
    _app = QGuiApplication([])
    renderer = QSvgRenderer(str(ICONS / "TSALab.svg"))
    if not renderer.isValid():
        raise SystemExit("SVG invalide : TSALab.svg")
    images = [render(renderer, s) for s in SIZES]
    big = images[-1]
    big.save(ICONS / "TSALab.ico", format="ICO", sizes=[(s, s) for s in SIZES], append_images=images[:-1])
    big.save(ICONS / "TSALab_256.png")
    print("Icônes écrites dans", ICONS)


if __name__ == "__main__":
    main()
