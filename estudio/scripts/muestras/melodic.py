# Biblioteca Racoon: instrumentos multimuestreados (para Squirrel), con mapa SFZ y bucles automáticos
import os, re, math, json
import numpy as np
from core import tree, fetch, mix, trim, analyse, write, slug, yin, note_number, NOTE_NAMES

DYN = {'pppp': 1, 'ppp': 2, 'pp': 3, 'p': 4, 'mp': 5, 'mf': 6, 'f': 7, 'ff': 8, 'fff': 9, 'soft': 3, 'quiet': 3, 'medium': 5, 'med': 5,
       'loud': 8, 'hard': 8, 'normal': 6}

INST_CATS = {
    'piano': 'Pianos', 'piano_electrico': 'Pianos eléctricos', 'teclado': 'Clavecín y teclados', 'organo': 'Órganos',
    'bajo': 'Bajos', 'contrabajo': 'Contrabajos', 'guitarra': 'Guitarras', 'cuerda_pulsada': 'Cuerdas pulsadas del mundo',
    'cuerdas': 'Cuerdas frotadas', 'metales': 'Metales', 'maderas': 'Maderas', 'saxofon': 'Saxofones', 'mallets': 'Láminas y mallets',
    'campanas': 'Campanas afinadas', 'mundo': 'Instrumentos del mundo', 'sintetizador': 'Sintetizadores', 'arpa': 'Arpas',
    'timbales_orq': 'Timbales de orquesta', 'armonica': 'Armónicas y lengüetas', 'voz': 'Voces',
}

def nn_name(n):
    return '%s%d' % (NOTE_NAMES[n % 12], n // 12 - 1)

def parse_tokens(stem, midi_numbers=False, trail_vel=False):
    toks = [t for t in re.split(r'[_\s]+', stem) if t]
    note, vl, rr, rest = None, None, None, []
    for t in toks:
        tl = t.lower()
        m = re.fullmatch(r'([a-g])(#|b|s)?(-?\d)', tl)
        if m and note is None:
            n = note_number(m.group(1).upper() + {'s': '#', None: ''}.get(m.group(2), m.group(2) or '') + m.group(3))
            if n is not None:
                note = n
                continue
        m = re.fullmatch(r'([a-g](?:#|b)?\d)v(\d+)', tl)        # Salamander: A0v1
        if m and note is None:
            note = note_number(m.group(1)[0].upper() + m.group(1)[1:]); vl = int(m.group(2)); continue
        if midi_numbers and re.fullmatch(r'\d{2,3}', tl) and note is None and 12 <= int(tl) <= 120:
            note = int(tl); continue
        m = re.fullmatch(r'(pppp|ppp|pp|p|mp|mf|f|ff|fff)(\d+)', tl)
        if m:
            vl = DYN[m.group(1)]; rr = int(m.group(2)); continue
        if trail_vel and note is not None and re.fullmatch(r'\d', tl):
            vl = int(tl); continue
        if re.fullmatch(r'(vl?|dyn|v)\d+', tl):
            vl = int(re.findall(r'\d+', tl)[0]); continue
        if re.fullmatch(r'rr?\d+', tl):
            rr = int(re.findall(r'\d+', tl)[0]); continue
        if tl in DYN:
            vl = DYN[tl]; continue
        rest.append(t)
    return note, vl, rr, rest

class MelInst:
    """Un instrumento cromático: {(nota, capa, rr): [partes]}."""
    def __init__(self, pack, src, key, name, cat, members, genres=(), tags=(), sustain=False, maxlen=4.0, maxnotes=16,
                 nvel=2, release=0.3, mics=None, octave_check=True, gain_db=0.0):
        self.pack, self.src, self.key, self.name, self.cat = pack, src, key, name, cat
        self.members, self.genres, self.tags = members, list(genres), list(tags)
        self.sustain, self.maxlen, self.maxnotes, self.nvel, self.release = sustain, maxlen, maxnotes, nvel, release
        self.octave_check, self.gain_db = octave_check, gain_db
        if os.environ.get('RACOON_PERFIL', 'esencial') != 'completo':
            piano = cat in ('piano', 'piano_electrico')
            self.maxnotes = min(maxnotes, 22 if piano else 12)
            self.nvel = min(nvel, 2 if piano else 1)
            self.maxlen = min(maxlen, 4.0 if piano else 2.6)
        else:
            # la biblioteca grande: más notas, más capas y colas largas (un piano se apaga solo, no a los 4 s)
            piano = cat in ('piano', 'piano_electrico')
            self.maxnotes = max(maxnotes, 30)
            self.nvel = max(nvel, 6 if piano else 4)
            self.maxlen = max(maxlen, 10.0 if cat == 'piano' else 7.0 if piano else min(2.0 * maxlen, 6.0))

    def gid(self):
        return '%s/%s' % (slug(self.pack), slug(self.key))

    def choose(self):
        notes = sorted(set(n for n, v, r in self.members))
        if len(notes) > self.maxnotes:
            # reparto parejo en semitonos
            lo, hi = notes[0], notes[-1]
            targets = np.linspace(lo, hi, self.maxnotes)
            pick = []
            for t in targets:
                best = min(notes, key=lambda n: abs(n - t))
                if best not in pick:
                    pick.append(best)
            notes = sorted(pick)
        vls = sorted(set(v for n, v, r in self.members))
        if len(vls) > self.nvel:
            if self.nvel == 1:
                vls = [vls[int(len(vls) * 0.7)]]
            else:
                idx = sorted(set(int(round(i)) for i in np.linspace(len(vls) * 0.25, len(vls) - 1, self.nvel)))
                vls = [vls[i] for i in idx]
        out = []
        for n in notes:
            for li, v in enumerate(vls):
                rrs = sorted(r for nn, vv, r in self.members if nn == n and vv == v)
                if not rrs:
                    # capa ausente en esta nota: la más cercana
                    cand = sorted(set(vv for nn, vv, r in self.members if nn == n), key=lambda vv: abs(vv - v))
                    if not cand:
                        continue
                    rrs = sorted(r for nn, vv, r in self.members if nn == n and vv == cand[0])
                    out.append((n, li, len(vls), self.members[(n, cand[0], rrs[0])]))
                else:
                    out.append((n, li, len(vls), self.members[(n, v, rrs[0])]))
        return out

    def render(self, local, root):
        chosen = self.choose()
        files = fetch(local, [p for c in chosen for (p, g, pan) in c[3]])
        rendered = []
        for n, li, nl, parts in chosen:
            if any(p not in files for p, g, pan in parts):
                continue
            x, sr = mix([(files[p], g, pan) for p, g, pan in parts])
            if sr > 44100:
                from core import resample
                x = resample(x, sr, 44100); sr = 44100
            x = trim(x, sr, self.maxlen + (0.0 if not self.sustain else 0.5), floor_db=-58.0, fade_ms=80.0)
            if x.shape[0] < int(0.03 * sr):
                continue
            rendered.append([n, li, nl, x, sr])
        if not rendered:
            return []
        # comprobación de octava: medir la afinación de las notas centrales
        offset = 0
        if self.octave_check:
            diffs = []
            mids = sorted(rendered, key=lambda r: abs(r[0] - 60))[:5]
            for n, li, nl, x, sr in mids:
                m = x.mean(axis=1)
                s = int(0.08 * sr)
                f = yin(m[s:s + int(0.25 * sr)], sr, 25.0, 2500.0)
                if f > 0:
                    est = 69 + 12 * math.log2(f / 440.0)
                    diffs.append(est - n)
            if diffs:
                d = float(np.median(diffs))
                k = int(round(d / 12.0))
                if abs(d - 12 * k) < 1.2 and k != 0:
                    offset = 12 * k
        pk = max(float(np.abs(r[3]).max()) for r in rendered)
        g = (10 ** (-1.0 / 20.0) / pk) * (10 ** (self.gain_db / 20.0))
        mono = all(r[3].shape[1] == 1 or (np.corrcoef(r[3][:, 0], r[3][:, 1])[0, 1] > 0.995 if r[3].shape[0] > 10 else True) for r in rendered)
        folder = 'instrumentos/%s/%s' % (slug(self.pack), slug(self.key))
        regions, entries = [], []
        for n, li, nl, x, sr in rendered:
            y = x * g
            if mono and y.shape[1] == 2:
                y = (0.5 * (y[:, 0] + y[:, 1]))[:, None]
            note = n + offset
            fname = '%s-v%d.flac' % (nn_name(note).replace('#', 's'), li + 1)
            rel = '%s/%s' % (folder, fname)
            loop = find_loop(y, sr, 440.0 * 2 ** ((note - 69) / 12.0)) if self.sustain else None
            write(os.path.join(root, rel), y, sr)
            regions.append(dict(note=note, layer=li, layers=nl, file=fname, loop=loop, len=y.shape[0]))
        # SFZ
        notes = sorted(set(r['note'] for r in regions))
        ranges = {}
        for i, n in enumerate(notes):
            lo = 0 if i == 0 else (notes[i - 1] + n) // 2 + 1
            hi = 127 if i == len(notes) - 1 else (n + notes[i + 1]) // 2
            ranges[n] = (lo, hi)
        nlayers = max(r['layers'] for r in regions)
        lines = ['// %s (Biblioteca Racoon)' % self.name, '// fuente: %s' % self.src, '<control>', 'default_path=%s/' % slug(self.key), '',
                 '<global>', 'ampeg_release=%.2f' % self.release, 'amp_veltrack=85', '']
        for r in sorted(regions, key=lambda r: (r['note'], r['layer'])):
            lo, hi = ranges[r['note']]
            vlo = 1 + (r['layer'] * 127) // nlayers
            vhi = ((r['layer'] + 1) * 127) // nlayers
            l = '<region> sample=%s pitch_keycenter=%d lokey=%d hikey=%d lovel=%d hivel=%d' % (r['file'], r['note'], lo, hi, vlo, vhi)
            if r['loop']:
                l += ' loop_mode=loop_continuous loop_start=%d loop_end=%d' % r['loop']
            else:
                l += ' loop_mode=no_loop'
            lines.append(l)
        sfz_rel = 'instrumentos/%s/%s.sfz' % (slug(self.pack), slug(self.key))
        with open(os.path.join(root, sfz_rel), 'w') as f:
            f.write('\n'.join(lines) + '\n')
        e = dict(id=sfz_rel[:-4], archivo=sfz_rel, nombre=self.name, cat=self.cat, tipo='multimuestra', grupo=self.gid(), pack=self.pack,
                 fuente=self.src, notas=len(notes), capas=nlayers, rango=[nn_name(notes[0]), nn_name(notes[-1])], bucle=bool(self.sustain),
                 muestras=len(regions))
        if self.genres:
            e['generos'] = self.genres
        if self.tags:
            e['etiquetas'] = self.tags
        if offset:
            e['octava_corregida'] = offset
        return [e]

def find_loop(y, sr, f0):
    """Bucle por periodos enteros en la parte estable, ajustado por correlación en los extremos."""
    m = y.mean(axis=1).astype(np.float64)
    n = len(m)
    if n < int(1.2 * sr) or f0 <= 0:
        return None
    period = sr / f0
    a, b = int(0.35 * n), int(0.88 * n)
    want = max(int(0.4 * sr), int(round(int(0.6 * sr / period) * period)))
    if b - a < want + int(0.05 * sr):
        want = int(round(int((b - a - 0.05 * sr) / period) * period))
        if want < int(0.15 * sr):
            return None
    w = max(32, int(min(0.004 * sr, 2 * period)))
    best, bs, be = -2.0, None, None
    for s in np.linspace(a, b - want - w, 12).astype(int):
        seg = m[s:s + w]
        e0 = s + want
        lo, hi = max(a, e0 - int(period)), min(n - w - 1, e0 + int(period))
        for e in range(lo, hi):
            seg2 = m[e:e + w]
            den = math.sqrt(float(np.dot(seg, seg)) * float(np.dot(seg2, seg2)))
            if den <= 0:
                continue
            c = float(np.dot(seg, seg2)) / den
            if c > best:
                best, bs, be = c, s, e
    if bs is None or best < 0.8:
        return None
    return (int(bs), int(be) - 1)

def note_scan(local, base, folder_rx, pack, src, cat, name, key=None, midi_numbers=False, filt=None, mics=None, trail_vel=False, **kw):
    """Agrupa los archivos de una carpeta en un instrumento (o varios, por articulación si key es None)."""
    groups = {}
    rx = re.compile(folder_rx)
    for p in tree(local):
        if not p.startswith(base) or not p.lower().endswith(('.wav', '.flac', '.ogg')):
            continue
        folder, fname = os.path.split(p[len(base):])
        if not rx.fullmatch(folder):
            continue
        stem = os.path.splitext(fname)[0]
        if filt and not filt(stem, folder):
            continue
        note, vl, rr, rest = parse_tokens(stem, midi_numbers, trail_vel)
        if note is None:
            continue
        mic = None
        g, pan = 1.0, 0.0
        if mics:
            for t in rest:
                if t in mics:
                    mic = t
            if mic is None:
                continue
            g, pan = mics[mic]
            rest = [t for t in rest if t != mic]
        k = key or (folder + '|' + '_'.join(rest))
        groups.setdefault(k, {}).setdefault((note, vl or 5, rr or 1), {}).setdefault(mic, (p, g, pan))
    out = []
    for k, members in sorted(groups.items()):
        full = max(len(v) for v in members.values())
        members = {kk: list(v.values()) for kk, v in members.items() if len(v) == full}
        nm = name(k) if callable(name) else name
        out.append(MelInst(pack, src, k if key else slug(k), nm, cat, members, **kw))
    return out
