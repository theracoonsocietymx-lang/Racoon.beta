# Kits Racoon: el taller de golpes. Lee catalogo.json, mide cada golpe de batería y escribe kits-racoon.json con las recetas
# de 40 kits por grupo de géneros (360 en total). Una receta no trae audio: dice qué muestras suenan en cada pad, cuánto se
# recortan, cuántos semitonos se afinan, su nivel, su saturación y su EQ. Hamster arma el golpe al abrir el kit, así que la
# biblioteca no crece, y cada golpe procesado sabe de qué muestras salió (el certificado de rola limpia lo usa).
#
#   python3 kits_racoon.py --root "/ruta/Muestras Racoon"
#
# El taller, por pad:
# - Bombo de tres capas: el golpe, un sub afinado a la misma nota que el golpe (solo lo de abajo de 120 Hz) y un clic corto
#   (lo de arriba de 3 kHz de un aro o un charles). Las capas se alinean a su primer transitorio y se suman en fase.
# - Caja con palmas, o dos cajas: la segunda capa rellena el cuerpo o el chasquido de la primera.
# - Todo: recorte del silencio del principio, cola cortada con fundido, nivel parejo por papel (medido en los primeros
#   150 ms), un poco de saturación y un EQ de tres bandas según el carácter del kit.
import argparse, collections, hashlib, json, math, os, random
import numpy as np
import soundfile as sf

SR = 48000

# los 16 pads, en el orden de los kits de Muestras Racoon y de las fábricas de Hamster
PADS = [
    (0, 'Bombo', 'Kick'), (1, 'Bombo 2', 'Kick'), (2, 'Caja', 'Snare'), (3, 'Palmas', 'Clap'), (4, 'Aro', 'Rim'),
    (5, 'Hi-hat cerrado', 'Closed Hat'), (6, 'Hi-hat abierto', 'Open Hat'), (7, 'Ride', 'Ride'), (8, 'Crash', 'Crash'),
    (9, 'Shaker', 'Shaker'), (10, 'Perc 1', 'Conga'), (11, 'Perc 2', 'Bongo'), (12, 'Perc 3', 'Tom High'),
    (13, 'Campana', 'Cowbell'), (14, 'Metal', 'Metal Perc'), (15, 'Efecto', 'Crash')]

# nivel de cada papel: RMS de los primeros 150 ms, en dBFS. El kit sale balanceado antes de tocar un fader
NIVEL = {0: -9.0, 1: -10.0, 2: -12.0, 3: -13.0, 4: -17.0, 5: -21.0, 6: -20.0, 7: -21.0, 8: -19.0, 9: -22.0,
         10: -16.0, 11: -16.0, 12: -14.0, 13: -18.0, 14: -20.0, 15: -17.0}

CATS = {
    'kick': ['bombo'], 'sub': ['bombo', 'bajo_808'], 'snare': ['caja'], 'clap': ['palmas', 'chasquido'], 'rim': ['aro', 'clave', 'bloque'],
    'ch': ['hihat_cerrado', 'hihat_pedal'], 'oh': ['hihat_abierto'], 'ride': ['ride'], 'crash': ['crash'],
    'shaker': ['shaker', 'pandereta', 'cabasa', 'guiro', 'cascabeles'], 'conga': ['conga', 'tambor', 'cajon', 'djembe'],
    'bongo': ['bongo', 'tabla', 'darbuka', 'timbal'], 'tom': ['tom_agudo', 'tom_medio'], 'cowbell': ['cencerro', 'agogo'],
    'metal': ['triangulo', 'campana', 'metal'], 'fx': ['platillo_fx', 'crash'], 'click': ['aro', 'hihat_cerrado', 'clave']}

# los nueve grupos de Racoon (variedad::Grupo) con las etiquetas de la biblioteca que les quedan y su manera de procesar
GRUPOS = {
    'house':  dict(nombre='House', tags=['house', 'deep house', 'disco', 'techno', 'funk', 'electro', 'italo'],
                   sat=0.18, cola=0.45, sub=0.55, clic=0.35, caja='palmas', hp_hats=7000, lp=0, oscuro=False),
    'afro':   dict(nombre='Afro', tags=['afro', 'afrobeat', 'world', 'house', 'latino', 'salsa', 'cumbia'],
                   sat=0.10, cola=0.38, sub=0.40, clic=0.15, caja='aro', hp_hats=6000, lp=0, oscuro=False),
    'disco':  dict(nombre='Disco', tags=['disco', 'funk', 'synthpop', 'italo', 'pop', 'synthwave', 'soul'],
                   sat=0.12, cola=0.32, sub=0.30, clic=0.20, caja='palmas', hp_hats=6000, lp=0, oscuro=False),
    'techno': dict(nombre='Techno', tags=['techno', 'electro', 'edm', 'ebm', 'industrial', 'idm', 'darkwave'],
                   sat=0.38, cola=0.50, sub=0.60, clic=0.45, caja='palmas', hp_hats=8000, lp=0, oscuro=False),
    'breaks': dict(nombre='Breaks', tags=['breakbeat', 'jungle', 'funk', 'soul', 'hiphop', 'rock', 'jazz'],
                   sat=0.25, cola=0.28, sub=0.30, clic=0.30, caja='caja', hp_hats=5000, lp=0, oscuro=False),
    'trap':   dict(nombre='Trap', tags=['trap', 'drill', 'miami bass', 'hiphop', 'electro', 'jersey club'],
                   sat=0.20, cola=0.35, sub=0.50, clic=0.30, caja='palmas', hp_hats=7000, lp=0, oscuro=False, ochocientos=True),
    'latino': dict(nombre='Latino', tags=['reggaeton', 'dembow', 'latino', 'cumbia', 'salsa', 'trap'],
                   sat=0.16, cola=0.30, sub=0.45, clic=0.25, caja='palmas', hp_hats=6500, lp=0, oscuro=False, ochocientos=True),
    'hiphop': dict(nombre='Hip-hop', tags=['hiphop', 'boombap', 'lofi', 'soul', 'jazz', 'rnb', 'funk'],
                   sat=0.32, cola=0.30, sub=0.35, clic=0.10, caja='caja', hp_hats=4000, lp=10000, oscuro=True),
    'chill':  dict(nombre='Chill', tags=['lofi', 'ambient', 'downtempo', 'trip-hop', 'jazz', 'acustico', 'world'],
                   sat=0.12, cola=0.40, sub=0.35, clic=0.0, caja='aro', hp_hats=4000, lp=9000, oscuro=True),
}

# el carácter de cada kit: cambia la saturación y el EQ del kit completo
CARACTER = [
    ('Seco',      dict(sat=0.8, ls=0.0, pk=1.0, hs=0.5, cola=0.75)),
    ('Cálido',    dict(sat=1.2, ls=1.5, pk=-1.0, hs=-1.5, cola=1.0)),
    ('Brillante', dict(sat=0.9, ls=0.0, pk=1.5, hs=2.5, cola=0.9)),
    ('Profundo',  dict(sat=1.0, ls=2.5, pk=-0.5, hs=0.0, cola=1.25)),
    ('Sucio',     dict(sat=2.0, ls=1.0, pk=1.0, hs=-0.5, cola=0.9)),
    ('Limpio',    dict(sat=0.4, ls=0.5, pk=0.5, hs=1.0, cola=1.0)),
]

KITS_POR_GRUPO = 40


def load_mono(path, max_s=4.0):
    try:
        x, sr = sf.read(path, frames=int(max_s * 96000), dtype='float32', always_2d=True)
    except Exception:
        return None, 0
    x = x.mean(axis=1)
    return x, sr


def analyse(root, m):
    x, sr = load_mono(os.path.join(root, m['archivo']))
    if x is None or len(x) < 32:
        return None
    peak = float(np.max(np.abs(x))) + 1e-12
    onset = int(np.argmax(np.abs(x) > 0.1 * peak))
    seg = x[onset:onset + int(0.15 * sr)]
    rms = float(np.sqrt(np.mean(seg ** 2)) + 1e-12)
    env = np.abs(x)
    above = np.where(env > peak * 10 ** (-50 / 20))[0]
    dur = (above[-1] - onset) / sr if len(above) else 0.0
    # fundamental (bombos y 808): el pico del espectro entre 30 y 160 Hz, de 30 a 300 ms después del golpe
    f0 = 0.0
    body = x[onset + int(0.03 * sr): onset + int(0.30 * sr)]
    if len(body) > 2048:
        n = 1 << 15
        sp = np.abs(np.fft.rfft(body * np.hanning(len(body)), n))
        fr = np.fft.rfftfreq(n, 1 / sr)
        band = (fr >= 30) & (fr <= 160)
        if band.any():
            f0 = float(fr[band][np.argmax(sp[band])])
    # dónde vive su energía: abajo de 120 Hz, arriba de 3 kHz (el clic) en los primeros 20 ms
    n = 1 << 13
    head = x[onset:onset + n]
    sp = np.abs(np.fft.rfft(head * np.hanning(len(head)), n)) ** 2 if len(head) > 64 else np.zeros(n // 2 + 1)
    fr = np.fft.rfftfreq(n, 1 / sr)
    tot = sp.sum() + 1e-12
    low = float(sp[fr < 120].sum() / tot)
    clic = x[onset:onset + int(0.02 * sr)]
    if len(clic) > 64:
        s2 = np.abs(np.fft.rfft(clic * np.hanning(len(clic)), 2048)) ** 2
        f2 = np.fft.rfftfreq(2048, 1 / sr)
        high = float(s2[f2 > 3000].sum() / (s2.sum() + 1e-12))
    else:
        high = 0.0
    centroid = float((fr * sp).sum() / tot)
    return dict(peak_db=20 * math.log10(peak), rms_db=20 * math.log10(rms), dur=dur, f0=f0, low=low, high=high,
                centroid=centroid, onset=onset / sr)


def semitones(f_from, f_to):
    if f_from <= 0 or f_to <= 0:
        return 0.0
    st = 12 * math.log2(f_to / f_from)
    while st > 6: st -= 12
    while st < -6: st += 12
    return round(st, 2)


def nearest_note(f):
    if f <= 0:
        return 0.0
    n = round(12 * math.log2(f / 440.0))
    return 440.0 * 2 ** (n / 12)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', required=True)
    ap.add_argument('--kits', type=int, default=KITS_POR_GRUPO)
    a = ap.parse_args()
    root = a.root
    cat = json.load(open(os.path.join(root, 'catalogo.json'), encoding='utf-8'))
    muestras = cat['muestras']
    # de cada grupo, la capa más fuerte y la primera alternancia: el golpe con más cuerpo
    best = {}
    for m in muestras:
        if m.get('rr', 1) != 1:
            continue
        g = m['grupo']
        if g not in best or m.get('capa', 1) > best[g].get('capa', 1):
            best[g] = m
    wanted = set(c for cs in CATS.values() for c in cs)
    cands = collections.defaultdict(list)
    n = 0
    for g, m in sorted(best.items()):
        if m['cat'] not in wanted:
            continue
        info = analyse(root, m)
        if info is None or info['rms_db'] < -45:
            continue
        n += 1
        e = dict(id=m['id'], grupo=g, cat=m['cat'], fuente=m['fuente'], generos=m.get('generos', []), brillo=m.get('brillo', 0), **info)
        for role, cs in CATS.items():
            if m['cat'] in cs:
                cands[role].append(e)
    print('taller: %d golpes medidos' % n)

    def pool(role, tags, filt=None):
        allc = [e for e in cands[role] if filt is None or filt(e)]
        fit = [e for e in allc if any(t in e['generos'] for t in tags)]
        return fit if len(fit) >= 6 else allc

    kits = []
    for gid, G in GRUPOS.items():
        rng = random.Random(int(hashlib.md5(gid.encode()).hexdigest()[:8], 16))
        tags = G['tags']
        # golpes: bombos con pegada (poca energía abajo de 120 Hz en el ataque o con clic); subs: largos y graves
        golpes = pool('kick', tags, lambda e: e['dur'] > 0.08 and e['f0'] > 0)
        subs = pool('sub', tags, lambda e: e['low'] > 0.35 and e['dur'] > 0.25 and e['f0'] > 0)
        ochos = [e for e in cands['sub'] if e['cat'] == 'bajo_808'] if G.get('ochocientos') else []
        cajas = pool('snare', tags, lambda e: e['dur'] > 0.06)
        palmas = pool('clap', tags)
        clics = [e for e in cands['click'] if e['high'] > 0.4 and e['dur'] < 0.5]
        pools = {r: pool(r, tags) for r in ('rim', 'ch', 'oh', 'ride', 'crash', 'shaker', 'conga', 'bongo', 'tom', 'cowbell', 'metal', 'fx')}
        used_pairs = set()
        for i in range(a.kits):
            car_name, car = CARACTER[i % len(CARACTER)]
            # el par bombo y caja no se repite dentro del grupo
            for _ in range(200):
                k = rng.choice(golpes); s = rng.choice(cajas)
                if (k['id'], s['id']) not in used_pairs:
                    break
            used_pairs.add((k['id'], s['id']))
            sat = round(G['sat'] * car['sat'], 3)
            eq = dict(ls=round(car['ls'], 1), pk=round(car['pk'], 1), hs=round(car['hs'] - (2.0 if G['oscuro'] else 0.0), 1),
                      lp=G['lp'])
            cola = G['cola'] * car['cola']
            pads = []

            def pad(p, capas, **kw):
                d = dict(pad=p, nombre=PADS[p][1], rol=PADS[p][2], nivel=NIVEL[p], capas=capas)
                d.update(kw)
                pads.append(d)

            # bombo: golpe + sub en la misma nota + clic
            nota = nearest_note(k['f0'])
            capas = [dict(m=k['id'], db=0.0, st=semitones(k['f0'], nota))]
            # el sub que menos hay que mover para caer en la nota del golpe (afinar más de 3 semitonos cambia su cuerpo)
            sub = None
            if subs and G['sub'] > 0:
                opciones = rng.sample(subs, min(10, len(subs)))
                sub = min(opciones, key=lambda e: abs(semitones(e['f0'], nota)))
            if sub is not None and sub['id'] != k['id']:
                capas.append(dict(m=sub['id'], db=round(-6.0 + 6.0 * G['sub'], 1), st=semitones(sub['f0'], nota), lp=120, largo=round(cola * 1.2, 2)))
            if clics and G['clic'] > 0 and rng.random() < 0.85:
                c = rng.choice(clics)
                capas.append(dict(m=c['id'], db=round(-20.0 + 10.0 * G['clic'], 1), st=0.0, hp=3000, largo=0.025))
            pad(0, capas, cola=round(cola, 2), sat=sat, eq=eq, nota=round(nota, 2))
            # bombo 2: el 808 largo en trap y latino, otro bombo en los demás
            if ochos:
                o = rng.choice(ochos)
                pad(1, [dict(m=o['id'], db=0.0, st=semitones(o['f0'], nearest_note(o['f0'])))], cola=1.6, sat=round(sat * 1.5, 3), eq=dict(eq, hs=eq['hs'] - 2), nota=round(nearest_note(o['f0']), 2))
            else:
                k2 = rng.choice([e for e in golpes if e['id'] != k['id']] or golpes)
                pad(1, [dict(m=k2['id'], db=0.0, st=semitones(k2['f0'], nearest_note(k2['f0'])))], cola=round(cola, 2), sat=sat, eq=eq)
            # caja: con palmas, con aro o con otra caja según el grupo
            capas = [dict(m=s['id'], db=0.0, st=0.0)]
            if G['caja'] == 'palmas' and palmas:
                c = rng.choice(palmas); capas.append(dict(m=c['id'], db=-4.0, st=0.0, hp=400))
            elif G['caja'] == 'aro' and pools['rim']:
                c = rng.choice(pools['rim']); capas.append(dict(m=c['id'], db=-8.0, st=0.0, hp=600))
            else:
                s2 = rng.choice([e for e in cajas if e['id'] != s['id']] or cajas)
                capas.append(dict(m=s2['id'], db=-5.0, st=0.0, hp=250))
            pad(2, capas, cola=round(0.35 * car['cola'] + 0.1, 2), sat=sat, eq=eq)
            # palmas: palma con un poco de caja corta debajo
            if palmas:
                c = rng.choice(palmas)
                capas = [dict(m=c['id'], db=0.0, st=0.0)]
                if rng.random() < 0.5:
                    capas.append(dict(m=rng.choice(cajas)['id'], db=-10.0, st=0.0, hp=300, largo=0.08))
                pad(3, capas, cola=0.4, sat=sat, eq=eq)
            else:
                pad(3, [dict(m=rng.choice(cajas)['id'], db=0.0, st=0.0)], cola=0.3, sat=sat, eq=eq)
            hats_eq = dict(eq, ls=0.0)
            simples = [(4, 'rim', 0.3, 0), (5, 'ch', 0.12 if G['nombre'] != 'Chill' else 0.18, G['hp_hats'] * 0.05),
                       (6, 'oh', 0.6, G['hp_hats'] * 0.05), (7, 'ride', 1.6, 300), (8, 'crash', 2.2, 300), (9, 'shaker', 0.25, 400),
                       (10, 'conga', 0.5, 0), (11, 'bongo', 0.45, 0), (12, 'tom', 0.6, 0), (13, 'cowbell', 0.5, 200),
                       (14, 'metal', 0.9, 400), (15, 'fx', 2.5, 200)]
            for p, role, c_s, hp in simples:
                if not pools[role]:
                    continue
                e = rng.choice(pools[role])
                capa = dict(m=e['id'], db=0.0, st=0.0)
                if hp:
                    capa['hp'] = int(hp)
                pad(p, [capa], cola=round(c_s * car['cola'], 2), sat=round(sat * 0.6, 3), eq=hats_eq if p in (5, 6, 7, 8, 9) else eq)
            nombre = '%s %s %02d' % (G['nombre'], car_name, i + 1)
            kits.append(dict(id='%s-%02d' % (gid, i + 1), nombre=nombre, grupo=gid, generos=tags[:4], pads=pads))
    out = dict(version=1, nombre='Kits Racoon', kits=kits)
    with open(os.path.join(root, 'kits-racoon.json'), 'w', encoding='utf-8') as f:
        json.dump(out, f, ensure_ascii=False, separators=(',', ':'))
    used = collections.Counter(c['m'] for k in kits for p in k['pads'] for c in p['capas'])
    print('Kits Racoon: %d kits en %d grupos, %d golpes distintos, %d KB' % (
        len(kits), len(GRUPOS), len(used), os.path.getsize(os.path.join(root, 'kits-racoon.json')) // 1024))


if __name__ == '__main__':
    main()
