#!/usr/bin/env python3
"""Hypra's Rack Extension front-panel draft (a concept, not a built device).

  python3 Tools/panel/render_front.py [path/to/reason-rack-extension]

Retra's style, as the portfolio's rack-panel-design rule 17 asks: 19 px
margins, 7 px gaps, a 44 px rail, flat cards in three greys, cream caps in
yellow rings, a choice as a column of keys, one schematic picture a group.
Signal flow reads left to right, then top to bottom.

Reason's stock widgets, the logo and the font check come from the Rack
checkout and the system at render time; none of them is copied here.
The script fails if captions or controls crowd each other or their card.
"""
import math
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

RACK = Path(sys.argv[1] if len(sys.argv) > 1
            else Path(__file__).resolve().parents[3] / 'reason-rack-extension' / 'main')
RETRA = RACK / 'Examples' / 'Retra' / 'Design'
YOUKNOW = RACK / 'Examples' / 'YouKnow' / 'GUI2D'
OUT = Path(__file__).resolve().parents[2] / 'Docs' / 'screenshots'
ARIAL = Path('/System/Library/Fonts/Supplemental')

Q, SS = 5, 3            # HD art is five pixels to a rack pixel
W, RU, H = 754, 9, 69 * 9
MARGIN, GAP, HEADER = 19, 7, 44
PLATE, RAIL, CARD, EDGE = (38, 41, 45), (24, 26, 29), (27, 30, 34), (17, 19, 22)
CREAM, MUTED = (239, 230, 208), (160, 156, 144)
KEY, KEY_LIT, GLASS, INK = (36, 39, 43), (52, 56, 61), (14, 16, 18), (40, 43, 47)
YELLOW, MINT = (246, 209, 85), (126, 200, 168)

S = Q * SS
image = Image.new('RGB', (W * S, H * S), PLATE)
draw = ImageDraw.Draw(image)
FONTS = {}
boxes = []              # (card, what, ink box) for the clearance check
cards = {}
card_now = None


def font(size, bold=False):
    if (size, bold) not in FONTS:
        FONTS[size, bold] = ImageFont.truetype(str(ARIAL / ('Arial Bold.ttf' if bold else 'Arial.ttf')), round(size * S))
    return FONTS[size, bold]


def claim(what, box):
    boxes.append((card_now, what, box))


def rect(box, radius=0, fill=None, edge=None, width=.6):
    draw.rounded_rectangle([round(v * S) for v in box], radius * S, fill, edge, round(width * S) if edge else 0)


def circle(x, y, r, fill=None, edge=None, width=.8):
    draw.ellipse([round(v * S) for v in (x - r, y - r, x + r, y + r)], fill, edge, round(width * S) if edge else 0)


def line(points, colour=CREAM, width=1.4):
    scaled = [(x * S, y * S) for x, y in points]
    draw.line(scaled, colour, round(width * S), 'curve')
    for x, y in (points[0], points[-1]):
        circle(x, y, width / 2, colour)


def text(x, y, value, size=11, colour=CREAM, anchor='mm', bold=False, track=True):
    f = font(size, bold)
    draw.text((x * S, y * S), value, colour, f, anchor)
    box = [v / S for v in draw.textbbox((x * S, y * S), value, f, anchor)]
    if track:
        claim(value, box)
    return box


def paste(picture, x, y, w, h):
    picture = picture.convert('RGBA').resize((round(w * S), round(h * S)), Image.LANCZOS)
    image.paste(picture, (round(x * S), round(y * S)), picture)


def card(name, x, y, w, h):
    global card_now
    card_now = name
    cards[name] = (x, y, x + w, y + h)
    rect((x, y, x + w, y + h), 6, CARD)
    # No title lamps: none of these groups can be switched off.
    text(x + 12, y + 15, name, 12, anchor='lm', bold=True)


def knob(x, y, caption, value=.5, size=28, centred=False):
    stroke = 2.2 if size > 40 else 1.6
    ring = size / 2 - stroke / 2 - .8
    cap = ring - stroke / 2 - (3 if size > 40 else 2)
    turn = -135 + 270 * value
    box = [round(v * S) for v in (x - ring, y - ring, x + ring, y + ring)]
    draw.arc(box, 135, 405, KEY_LIT, round(stroke * S))
    start = 0 if centred else -135
    if value != (.5 if centred else 0):
        a, b = sorted((start, turn))
        draw.arc(box, a - 90, b - 90, YELLOW, round(stroke * S))
    circle(x, y, cap, CREAM, EDGE)
    a = math.radians(turn)
    line([(x + cap * .3 * math.sin(a), y - cap * .3 * math.cos(a)),
          (x + (cap - 2.4) * math.sin(a), y - (cap - 2.4) * math.cos(a))], INK, 1.6)
    claim('knob ' + caption, (x - size / 2, y - size / 2, x + size / 2, y + size / 2))
    text(x, y + size / 2 + 9, caption, 11)


def keys(x, y, w, labels, chosen=(0,), step=15):
    for i, label in enumerate(labels):
        top = y + step * i
        lit = i in chosen
        rect((x, top, x + w, top + step - 2), 2.5, KEY_LIT if lit else KEY, EDGE)
        circle(x + 8, top + (step - 2) / 2, 2.5, YELLOW if lit else EDGE)
        ink = text(x + 15, top + (step - 2) / 2, label, 11, CREAM if lit else MUTED, 'lm', track=False)
        assert ink[2] <= x + w - 3, f'{label}: wider than its key'
    claim('keys ' + labels[0], (x, y, x + w, y + step * len(labels) - 2))


def picture(name, box):
    claim('picture ' + name, box)
    return box


def saws(box):
    x0, y0, x1, y1 = picture('seven saws', box)
    w, mid = x1 - x0, (y0 + y1) / 2
    for k in (-3, 3, -2, 2, -1, 1, 0):
        period = w / 2 * (1 + k * .075)
        amp = (y1 - y0) / 2 * (1 - abs(k) * .16)
        points = []
        for i in range(161):
            p = (w * i / 160) / period + k * .09
            points.append((x0 + w * i / 160, mid + amp * (1 - 2 * (p - math.floor(p)))))
        line(points, YELLOW if k == 0 else MINT if abs(k) == 1 else (82, 91, 91), 1.6 if k == 0 else .8)


def sync_wave(box):
    x0, y0, x1, y1 = picture('sync', box)
    w, h = x1 - x0, y1 - y0
    points, small = [], []
    for i in range(201):
        p = i / 200 * 2.2 % 1
        points.append((x0 + w * i / 200, y0 + h * .28 - h * .24 * (2 * p - 1)))
        small.append((x0 + w * i / 200, y0 + h * .78 - h * .20 * (2 * (p * 2.6 % 1) - 1)))
    line(points, CREAM, 1.2)
    line(small, YELLOW, 1.2)


def arrow(x0, y0, x1, y1, colour=CREAM):
    line([(x0, y0), (x1, y1)], colour, 1)
    a = math.atan2(y1 - y0, x1 - x0)
    tip = [(x1, y1)] + [(x1 - 4.5 * math.cos(a + d), y1 - 4.5 * math.sin(a + d)) for d in (-.45, .45)]
    draw.polygon([(x * S, y * S) for x, y in tip], colour)


def routing(box):
    x0, y0, x1, y1 = picture('routing', box)
    cx, w = (x0 + x1) / 2, x1 - x0
    for number, y in (('1', y0 + 10), ('2', y1 - 10)):
        rect((x0 + 12, y - 9, x0 + 42, y + 9), 3, KEY, CREAM, .8)
        text(x0 + 27, y, number, 11, bold=True, track=False)
        arrow(x0 + 43, y, cx + 24, (y0 + y1) / 2 + (-6 if number == '1' else 6))
    arrow(x0 + 19, y0 + 20, x0 + 19, y1 - 20, YELLOW)
    arrow(x0 + 35, y1 - 20, x0 + 35, y0 + 20, MINT)
    circle(cx + 33, (y0 + y1) / 2, 9, KEY, CREAM)
    text(cx + 33, (y0 + y1) / 2, '+', 13, track=False)
    arrow(cx + 42, (y0 + y1) / 2, x0 + w - 1, (y0 + y1) / 2)


def response(box):
    x0, y0, x1, y1 = picture('response', box)
    points = []
    for i in range(121):
        r = 2 ** (-4 + 5.2 * i / 120)
        db = -20 * math.log10(math.hypot(1 - r * r, r / 2.5) * math.hypot(1 - r * r, 1.85 * r) + 1e-9)
        points.append((x0 + (x1 - x0) * i / 120, min(y1, max(y0, y0 + (y1 - y0) * (.38 - db / 34)))))
    line([(x0, y1), (x1, y1)], KEY_LIT, 1)
    line(points, YELLOW, 1.5)


def envelope(box):
    x0, y0, x1, y1 = picture('envelope', box)
    w, h = x1 - x0, y1 - y0
    points = [(x0, y1), (x0 + w * .2, y0)]
    points += [(x0 + w * (.2 + .27 * i / 12), y0 + h * .45 * (1 - math.exp(-i / 3.5))) for i in range(13)]
    points += [(x0 + w * .75, y0 + h * .45)]
    points += [(x0 + w * (.75 + .25 * i / 12), y1 - h * .55 * math.exp(-i / 3.0)) for i in range(13)]
    line(points, CREAM, 1.2)
    for x, y in (points[1], points[-14]):
        circle(x, y, 2, MINT)


def lfo_wave(box):
    x0, y0, x1, y1 = picture('lfo', box)
    points = []
    for i in range(81):
        p = i / 80 * 1.5 % 1
        points.append((x0 + (x1 - x0) * i / 80, y0 + (y1 - y0) * abs(2 * p - 1)))
    line(points, YELLOW, 1.4)


def echoes(box):
    x0, y0, x1, y1 = picture('echoes', box)
    mid = (y0 + y1) / 2
    line([(x0, mid), (x1, mid)], KEY_LIT, .8)
    for i in range(6):
        x = x0 + 4 + (x1 - x0 - 8) * i / 5
        reach = (y1 - y0) / 2 * .78 ** i
        line([(x, mid), (x, mid + (-reach if i % 2 == 0 else reach))],
             YELLOW if i == 0 else MINT if i % 2 else CREAM, 2)


def header():
    global card_now
    card_now = 'header'
    cards['header'] = (30, 0, W, HEADER)     # x < 30 is the host's fold control
    rect((0, 0, W, HEADER), 0, RAIL)
    paste(Image.open(RETRA / 'Brand' / 'Protocodus-logo.png'), 36, 10, 29, 24)
    claim('logo', (36, 10, 65, 34))
    text(76, 22, 'HYPRA', 20, anchor='lm', bold=True)
    text(160, 22, 'Virtual analog', 11, MUTED, 'lm')
    rect((337, 12, 583, 32), 3, GLASS)
    claim('patch', (337, 12, 583, 32))
    text(460, 22, 'Seven Suns', 11, track=False)
    stock = RETRA / 'Stock'
    paste(Image.open(stock / 'PatchBrowseGroup.png'), 590, 11, 58, 22)
    claim('browse', (590, 11, 648, 33))
    paste(Image.open(stock / 'TapeHorz.png'), 655, 15.5, 80, 13)
    claim('tape', (655, 15.5, 735, 28.5))
    text(695, 22, 'Hypra 1', 9, INK, track=False)
    circle(298, 22, 3, YELLOW)
    text(305, 22, 'Note', 11, MUTED, 'lm')


def oscillators(y, h):
    x = MARGIN
    card('Oscillator 1', x, y, 232, h)
    saws((x + 12, y + 36, x + 106, y + 118))
    keys(x + 120, y + 26, 102, ['Super Saw', 'Triangle Mod', 'Noise', 'Feedback', 'Pulse', 'Saw', 'Triangle'], step=14.5)
    knob(x + 35, y + 147, 'Detune', .62)
    knob(x + 85, y + 147, 'Mix', .78)
    knob(x + 171, y + 147, 'Shift', .5, centred=True)

    x += 232 + GAP
    card('Oscillator 2', x, y, 208, h)
    sync_wave((x + 12, y + 36, x + 100, y + 106))
    keys(x + 114, y + 33, 84, ['Pulse', 'Saw', 'Triangle'], (1,))
    keys(x + 114, y + 92, 84, ['Sync'], (0,))
    for i, (name, value, centred) in enumerate((('Range', .64, True), ('Fine', .53, True),
                                               ('Width', .2, False), ('PWM', .35, False))):
        knob(x + 29 + 50 * i, y + 147, name, value, centred=centred)

    x += 208 + GAP
    card('Oscillator mix', x, y, 262, h)
    routing((x + 10, y + 38, x + 148, y + 110))
    keys(x + 162, y + 29, 90, ['Ring'], ())
    text(x + 162, y + 59, 'LFO + Env to', 11, MUTED, 'lm')
    keys(x + 162, y + 72, 90, ['Osc 1 + 2', 'Osc 2', 'X-Mod'], (0,))
    for cx, name, value, centred in ((30, 'Balance', .34, True), (76, 'X-Mod', 0, False), (118, 'LFO', .56, True),
                                     (159, 'Env', .5, True), (198, 'A', .05, False), (236, 'D', .3, False)):
        knob(x + cx, y + 147, name, value, centred=centred)


def filter_and_amp(y, h):
    x = MARGIN
    card('Filter', x, y, 420, h)
    response((x + 12, y + 35, x + 110, y + 86))
    keys(x + 122, y + 27, 72, ['Low', 'Band', 'High'], (0,))
    keys(x + 122, y + 78, 72, ['24 dB'], (0,))
    knob(x + 238, y + 62, 'Cutoff', .72, 54)
    knob(x + 292, y + 62, 'Res', .28)
    for cx, name, value in ((338, 'Key', .62), (386, 'LFO', .5)):
        knob(x + cx, y + 62, name, value, centred=True)
    envelope((x + 12, y + 121, x + 110, y + 165))
    for i, (name, value) in enumerate((('A', .05), ('D', .45), ('S', .55), ('R', .38))):
        knob(x + 150 + 45 * i, y + 147, name, value)
    knob(x + 362, y + 147, 'Env', .68, centred=True)

    x += 420 + GAP
    card('Amplifier', x, y, W - MARGIN - x, h)
    keys(x + 12, y + 40.5, 96, ['Tremolo', 'Auto Pan', 'Manual Pan'], (0,))
    knob(x + 152, y + 62, 'Depth', .5, centred=True)
    knob(x + 232, y + 62, 'Level', .8, 54)
    envelope((x + 12, y + 121, x + 94, y + 165))
    for i, (name, value) in enumerate((('A', .08), ('D', .5), ('S', .86), ('R', .4))):
        knob(x + 130 + 42 * i, y + 147, name, value)


def wheel(name, x, y):
    strip = Image.open(YOUKNOW / f'{name}.png')
    frame = strip.height // 64
    index = 32 if name == 'PitchWheel' else 0
    paste(strip.crop((0, index * frame, strip.width, (index + 1) * frame)), x, y, 19, 102)
    claim(name, (x, y, x + 19, y + 102))


def play_and_effects(y, h):
    x = MARGIN
    card('LFO 1', x, y, 142, h)
    lfo_wave((x + 12, y + 45, x + 55, y + 91))
    keys(x + 64, y + 39, 70, ['Triangle', 'Saw', 'Square', 'S & H'])
    knob(x + 37, y + 147, 'Rate', .45)
    knob(x + 103, y + 147, 'Fade', .15)

    x += 142 + GAP
    card('Play', x, y, 272, h)
    wheel('PitchWheel', x + 14, y + 46)
    wheel('ModWheel', x + 45, y + 46)
    text(x + 23.5, y + 170, 'Pitch', 11)
    text(x + 54.5, y + 170, 'Mod', 11)
    keys(x + 80, y + 39, 78, ['Poly', 'Mono', 'Legato', 'Unison'])
    knob(x + 119, y + 147, 'Glide', .2)
    text(x + 217, y + 15, 'LFO 2', 12, bold=True)
    for cx, cy, name, value in ((192, 55, 'Rate', .6), (242, 55, 'Pitch', .62),
                                (192, 147, 'Filter', .5), (242, 147, 'Amp', .5)):
        knob(x + cx, y + cy, name, value, centred=name != 'Rate')

    x += 272 + GAP
    right = W - MARGIN
    card('Effects', x, y, right - x, h)
    text(right - 10, y + 15, 'Super Chorus Slow', 11, MUTED, 'rm')
    for cx, name, value, centred in ((30, 'Bass', .56, True), (78, 'Treble', .6, True), (130, 'Chorus', .4, False)):
        knob(x + cx, y + 53, name, value, centred=centred)
    echoes((x + 12, y + 93, x + 151, y + 125))
    keys(x + 174, y + 37, right - x - 184, ['Pan L-R', 'Pan R-L', 'Pan Short', 'Mono Short', 'Mono Long'])
    for cx, name, value in ((36, 'Time', .7), (106, 'Feedback', .45), (184, 'Delay', .4)):
        knob(x + cx, y + 147, name, value)


def verify():
    """Captions keep off one another, off controls and off their card's edge."""
    problems = []
    for i, (where, what, a) in enumerate(boxes):
        x0, y0, x1, y1 = cards[where]
        if a[0] < x0 + 4.9 or a[1] < y0 + 4.9 or a[2] > x1 - 4.9 or a[3] > y1 - 4.9:
            problems.append(f'{where}: {what} is inside the 5 px padding')
        for other, whom, b in boxes[i + 1:]:
            if other != where:
                continue
            dx = max(b[0] - a[2], a[0] - b[2])
            dy = max(b[1] - a[3], a[1] - b[3])
            owned = what.startswith('knob ') and what[5:] == whom
            if max(dx, dy) < (2 if owned else 4.5):
                problems.append(f'{where}: {what} and {whom} are {max(dx, dy):.1f} px apart')
    return problems


def main():
    header()
    height = (H - HEADER - 4 * GAP) / 3
    for i, row in enumerate((oscillators, filter_and_amp, play_and_effects)):
        row(HEADER + GAP + i * (height + GAP), height)
    problems = verify()
    OUT.mkdir(parents=True, exist_ok=True)
    for factor in (1, 2):
        image.resize((W * factor, H * factor), Image.LANCZOS).save(OUT / f'front-panel-draft-{factor}x.png')
    print(f'{W} x {H} ({RU}U), {len(boxes)} placed items, {len(problems)} clearance problems')
    for problem in problems:
        print(' ', problem)
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
