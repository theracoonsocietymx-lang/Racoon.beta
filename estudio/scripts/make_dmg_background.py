# Genera el fondo de la ventana del instalador (.dmg) de Racoon con la marca (Bandido de Callejón).
# Uso: python3 make_dmg_background.py carpeta_salida
import sys, os
from PIL import Image, ImageDraw, ImageFont

out = sys.argv[1] if len(sys.argv) > 1 else "."
here = os.path.dirname(os.path.abspath(__file__))
fonts = os.path.join(here, "..", "Resources", "fonts")

ROSA = (224, 120, 154); LIMON = (217, 196, 82); MENTA = (127, 191, 168)
LECHE = (247, 245, 242); CIRUELA = (31, 26, 28)

def render(s):
    W, H = 660 * s, 420 * s
    im = Image.new("RGB", (W, H), LIMON)
    d = ImageDraw.Draw(im)
    dela = ImageFont.truetype(os.path.join(fonts, "DelaGothicOne.ttf"), 30 * s)
    alum = ImageFont.truetype(os.path.join(fonts, "AlumniSans-ExtraBold.ttf"), 19 * s)
    golos = ImageFont.truetype(os.path.join(fonts, "GolosText-SemiBold.ttf"), 13 * s)
    card = (24 * s, 22 * s, W - 30 * s, 92 * s)
    d.rectangle((card[0] + 6 * s, card[1] + 6 * s, card[2] + 6 * s, card[3] + 6 * s), fill=CIRUELA)
    d.rectangle(card, fill=LECHE, outline=CIRUELA, width=3 * s)
    d.text((card[0] + 22 * s, (card[1] + card[3]) // 2), "Racoon", font=dela, fill=CIRUELA, anchor="lm")
    d.text((card[2] - 20 * s, (card[1] + card[3]) // 2), "EL DAW DE LA FAMILIA", font=alum, fill=CIRUELA, anchor="rm")
    y = 205 * s; x0, x1 = 252 * s, 408 * s
    d.rectangle((x0 + 4 * s, y - 5 * s + 4 * s, x1 - 18 * s + 4 * s, y + 5 * s + 4 * s), fill=CIRUELA)
    d.polygon([(x1 + 4 * s, y + 4 * s), (x1 - 26 * s + 4 * s, y - 18 * s + 4 * s), (x1 - 26 * s + 4 * s, y + 18 * s + 4 * s)], fill=CIRUELA)
    d.rectangle((x0, y - 5 * s, x1 - 18 * s, y + 5 * s), fill=ROSA, outline=CIRUELA, width=2 * s)
    d.polygon([(x1, y), (x1 - 26 * s, y - 18 * s), (x1 - 26 * s, y + 18 * s)], fill=ROSA, outline=CIRUELA)
    strip = (24 * s, 300 * s, W - 30 * s, 396 * s)
    d.rectangle((strip[0] + 6 * s, strip[1] + 6 * s, strip[2] + 6 * s, strip[3] + 6 * s), fill=ROSA)
    d.rectangle(strip, fill=CIRUELA)
    lines = ["1  Arrastra Racoon a Aplicaciones",
             "2  Ábrelo. Si trae Badger y Hamster, los instala solo",
             "3  Pulsa espacio: suena el proyecto de ejemplo Housito"]
    for i, t in enumerate(lines):
        d.text((strip[0] + 20 * s, strip[1] + (22 + i * 26) * s), t, font=golos, fill=LIMON if i == 0 else LECHE, anchor="lm")
    return im

os.makedirs(out, exist_ok=True)
render(1).save(os.path.join(out, "background.png"))
render(2).save(os.path.join(out, "background@2x.png"))
print("ok")
