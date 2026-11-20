#!/usr/bin/env python3
"""Icons for the family members that were drawn with code (Marmot, Jerboa, Opossum, Otter, Beaver).
Same recipe as the others: dark rounded square, the animal's head in its colour, the yellow visor with white eyes.
Writes Resources/icons/<name>_1024.png and _256.png, and estudio/Resources/animals/<name>.png.
Usage: python3 make_icons.py [name ...]"""
import os, sys
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ICONS = os.path.join(HERE, '..', 'Resources', 'icons')
ANIMALS = os.path.join(HERE, '..', '..', 'estudio', 'Resources', 'animals')
BG, VISOR, WHITE = (31, 26, 28), (217, 196, 82), (247, 245, 242)
S = 4  # drawn 4x larger, then reduced (smooth edges)


def base():
    im = Image.new('RGBA', (1024 * S, 1024 * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([98 * S, 98 * S, 926 * S, 926 * S], radius=168 * S, fill=BG)
    return im, d


def r(*v):
    return [x * S for x in v]


def visor(d):
    d.rectangle(r(206, 494, 820, 636), fill=BG)
    d.rectangle(r(206, 494, 820, 636), outline=VISOR, width=18 * S)
    d.rectangle(r(395, 554, 468, 578), fill=WHITE)
    d.rectangle(r(556, 554, 629, 578), fill=WHITE)


def marmot(d):
    body, dark = (199, 140, 84), (128, 86, 50)
    d.ellipse(r(250, 300, 380, 430), fill=body); d.ellipse(r(644, 300, 774, 430), fill=body)      # small round ears
    d.ellipse(r(282, 332, 348, 398), fill=dark); d.ellipse(r(676, 332, 742, 398), fill=dark)
    d.ellipse(r(218, 290, 806, 800), fill=body)                                                   # wide head
    d.ellipse(r(392, 620, 632, 790), fill=dark)                                                   # muzzle
    d.rectangle(r(486, 650, 538, 690), fill=BG)                                                   # nose
    d.rectangle(r(470, 712, 509, 776), fill=WHITE); d.rectangle(r(515, 712, 554, 776), fill=WHITE)  # buck teeth
    visor(d)


def jerboa(d):
    body, inner = (222, 186, 132), (150, 112, 74)
    d.ellipse(r(230, 110, 400, 520), fill=body); d.ellipse(r(624, 110, 794, 520), fill=body)      # very long ears
    d.ellipse(r(270, 160, 360, 470), fill=inner); d.ellipse(r(664, 160, 754, 470), fill=inner)
    d.ellipse(r(262, 360, 762, 800), fill=body)
    d.rectangle(r(486, 660, 538, 700), fill=BG)
    visor(d)


def opossum(d):
    face, ear, dark = (226, 222, 214), (96, 90, 92), (150, 146, 140)
    d.ellipse(r(236, 268, 386, 418), fill=ear); d.ellipse(r(638, 268, 788, 418), fill=ear)        # dark round ears
    d.ellipse(r(270, 302, 352, 384), fill=(222, 120, 150)); d.ellipse(r(672, 302, 754, 384), fill=(222, 120, 150))
    d.ellipse(r(232, 310, 792, 760), fill=face)
    d.polygon(r(392, 700, 632, 700, 512, 868), fill=face)                                         # pointed snout
    d.polygon(r(452, 432, 572, 432, 512, 300), fill=dark)                                         # dark stripe on the forehead
    d.ellipse(r(486, 800, 538, 852), fill=(222, 120, 150))                                        # pink nose
    visor(d)


def otter(d):
    body, light = (120, 84, 60), (196, 164, 128)
    d.ellipse(r(264, 330, 344, 410), fill=body); d.ellipse(r(680, 330, 760, 410), fill=body)      # tiny ears
    d.ellipse(r(224, 330, 800, 790), fill=body)
    d.ellipse(r(352, 610, 672, 800), fill=light)                                                  # pale muzzle
    d.rectangle(r(486, 640, 538, 684), fill=BG)
    for y in (700, 730):                                                                          # whiskers
        d.rectangle(r(250, y, 380, y + 8), fill=WHITE); d.rectangle(r(644, y, 774, y + 8), fill=WHITE)
    visor(d)


def beaver(d):
    body, tail, dark = (150, 96, 58), (88, 60, 46), (104, 66, 40)
    d.rounded_rectangle(r(690, 180, 860, 600), radius=70 * S, fill=tail)                          # flat tail behind
    for y in range(220, 580, 60):
        d.line(r(700, y, 850, y + 30), fill=(70, 48, 38), width=8 * S)
    d.ellipse(r(254, 300, 374, 420), fill=body); d.ellipse(r(650, 300, 770, 420), fill=body)
    d.ellipse(r(232, 300, 792, 790), fill=body)
    d.ellipse(r(402, 620, 622, 780), fill=dark)
    d.rectangle(r(486, 646, 538, 686), fill=BG)
    d.rectangle(r(466, 706, 508, 790), fill=WHITE); d.rectangle(r(516, 706, 558, 790), fill=WHITE)   # big orange-white teeth
    visor(d)


ANIMAL = {'marmot': marmot, 'jerboa': jerboa, 'opossum': opossum, 'otter': otter, 'beaver': beaver}

if __name__ == '__main__':
    names = sys.argv[1:] or list(ANIMAL)
    for n in names:
        im, d = base()
        ANIMAL[n](d)
        big = im.resize((1024, 1024), Image.LANCZOS)
        big.save(os.path.join(ICONS, n + '_1024.png'))
        small = big.resize((256, 256), Image.LANCZOS)
        small.save(os.path.join(ICONS, n + '_256.png'))
        if os.path.isdir(ANIMALS):
            small.save(os.path.join(ANIMALS, n + '.png'))
        print('ok', n)
