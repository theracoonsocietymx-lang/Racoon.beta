# Biblioteca Racoon: modelo de instrumentos, clasificación y procesamiento de grupos
import os, re, json, math
import numpy as np
from core import tree, fetch, mix, trim, maybe_mono, analyse, write, slug, db, read, resample

# categoría -> (nombre visible, carpeta, duración máxima en s, rol de Hamster, tonal)
CATS = {
    'bombo':          ('Bombo', 'bateria', 1.6, 'Kick', True),
    'bajo_808':       ('Bajo 808', 'bateria', 4.0, 'Kick', True),
    'caja':           ('Caja', 'bateria', 1.5, 'Snare', False),
    'aro':            ('Aro y baqueta cruzada', 'bateria', 0.9, 'Rim', False),
    'palmas':         ('Palmas', 'bateria', 1.2, 'Clap', False),
    'chasquido':      ('Chasquidos', 'bateria', 0.8, 'Clap', False),
    'hihat_cerrado':  ('Hi-hat cerrado', 'bateria', 0.9, 'Closed Hat', False),
    'hihat_pedal':    ('Hi-hat de pedal', 'bateria', 0.9, 'Closed Hat', False),
    'hihat_abierto':  ('Hi-hat abierto', 'bateria', 2.2, 'Open Hat', False),
    'ride':           ('Ride', 'bateria', 3.5, 'Ride', False),
    'crash':          ('Crash', 'bateria', 3.8, 'Crash', False),
    'platillo_fx':    ('China y splash', 'bateria', 3.0, 'Crash', False),
    'tom_grave':      ('Tom grave', 'bateria', 2.5, 'Tom Low', True),
    'tom_medio':      ('Tom medio', 'bateria', 2.2, 'Tom Mid', True),
    'tom_agudo':      ('Tom agudo', 'bateria', 2.0, 'Tom High', True),
    'shaker':         ('Shaker y maracas', 'percusion', 1.2, 'Shaker', False),
    'pandereta':      ('Pandereta', 'percusion', 1.5, 'Shaker', False),
    'cabasa':         ('Cabasa', 'percusion', 1.2, 'Shaker', False),
    'guiro':          ('Güiro', 'percusion', 1.6, 'Shaker', False),
    'cascabeles':     ('Cascabeles', 'percusion', 2.0, 'Shaker', False),
    'conga':          ('Conga y tumba', 'percusion', 1.8, 'Conga', True),
    'bongo':          ('Bongó', 'percusion', 1.4, 'Bongo', True),
    'timbal':         ('Timbal', 'percusion', 1.8, 'Bongo', True),
    'djembe':         ('Djembé', 'percusion', 1.8, 'Conga', True),
    'cajon':          ('Cajón', 'percusion', 1.5, 'Conga', False),
    'darbuka':        ('Darbuka', 'percusion', 1.5, 'Bongo', True),
    'tabla':          ('Tabla', 'percusion', 2.0, 'Bongo', True),
    'tambor':         ('Tambores del mundo', 'percusion', 2.5, 'Conga', True),
    'cencerro':       ('Cencerro', 'percusion', 1.5, 'Cowbell', True),
    'agogo':          ('Agogó', 'percusion', 1.5, 'Cowbell', True),
    'clave':          ('Clave', 'percusion', 0.8, 'Rim', True),
    'bloque':         ('Bloque de madera', 'percusion', 0.8, 'Rim', True),
    'triangulo':      ('Triángulo', 'percusion', 4.0, 'Metal Perc', True),
    'campana':        ('Campanas', 'percusion', 6.0, 'Metal Perc', True),
    'metal':          ('Metales', 'percusion', 3.0, 'Metal Perc', False),
    'cuerpo':         ('Percusión corporal', 'percusion', 1.2, 'Custom', False),
    'percusion':      ('Otras percusiones', 'percusion', 2.5, 'Custom', False),
    'orquesta':       ('Percusión de orquesta', 'percusion', 6.0, 'Custom', True),
    'efecto':         ('Efectos', 'efectos', 6.0, 'Custom', False),
    'voz':            ('Voces sueltas', 'efectos', 3.0, 'Custom', False),
}

CYMBALS = ('ride', 'crash', 'platillo_fx', 'hihat_abierto')
FULL = os.environ.get('RACOON_PERFIL', 'esencial') == 'completo'
TARGET_SR = 44100

# palabras en nombres de archivo -> español
WORDS = {
    'center': 'centro', 'offcenter': 'fuera de centro', 'edge': 'borde', 'rimshot': 'rimshot', 'rims': 'rimshot',
    'crossstick': 'baqueta cruzada', 'sidestick': 'baqueta cruzada', 'sstick': 'baqueta cruzada', 'ss': 'baqueta cruzada',
    'brush': 'escobilla', 'mallet': 'maza', 'muted': 'apagado', 'halfopen': 'semiabierto', 'half': 'medio abierto',
    'open': 'abierto', 'closed': 'cerrado', 'tip': 'punta', 'shank': 'canto', 'bow': 'arco', 'bell': 'campana',
    'loose': 'suelto', 'tight': 'apretado', 'quarter': 'un cuarto', 'pedal': 'pedal', 'foot': 'pie', 'footchik': 'pie',
    'chik': 'pie', 'footspl': 'pie salpicado', 'footsplash': 'pie salpicado', 'splash': 'splash', 'sizzle': 'sizzle',
    'sizz': 'sizzle', 'sizzling': 'sizzle', 'buzz': 'buzz', 'flam': 'flam', 'roll': 'redoble', 'stickshot1': 'golpe de baqueta',
    'stickshot2': 'golpe de baqueta 2', 'snon': 'con bordonero', 'snoff': 'sin bordonero', 'hit': 'golpe', 'normal': '',
    'clean': 'limpio', 'dirty': 'sucio', 'dirtiest': 'muy sucio', 'rctip': 'aro punta', 'rcshank': 'aro canto', 'dig': 'hundido',
    'prep': 'preparado', 'tall': 'alto', 'basic': '', 'high': 'agudo', 'hi': 'agudo', 'low': 'grave', 'lo': 'grave',
    'med': 'medio', 'mid': 'medio', 'long': 'largo', 'short': 'corto', 'shake': 'agitado', 'rub': 'frotado', 'fast': 'rápido',
    'squash': 'aplastado', 'xtight': 'muy apretado', 'drop': 'caída', 'down': 'abajo', 'up': 'arriba', 'stroke': 'pasada',
    'crash': 'crash', 'ride': 'ride', 'china': 'china', 'accent': 'acento', 'alt': 'alterno',
}

def human(*tokens):
    out = []
    for t in tokens:
        if t is None:
            continue
        for w in re.split(r'[_\s]+', str(t)):
            if not w:
                continue
            v = WORDS.get(w.lower(), w)
            if v and v not in out:
                out.append(v)
    return ' '.join(out)

class Inst:
    def __init__(self, pack, src, key, name, cat, members, tags=(), genres=(), maxlen=None, tonal=None, mono=None, nvel=4, nrr=4, gain_db=0.0, floor_db=-60.0):
        self.pack, self.src, self.key, self.name, self.cat = pack, src, key, name, cat
        self.members = members        # {(vl, rr): [(repo_path, gain, pan)]}
        self.tags, self.genres = list(tags), list(genres)
        self.maxlen = maxlen if maxlen is not None else CATS[cat][2]
        self.tonal = CATS[cat][4] if tonal is None else tonal
        self.mono, self.nvel, self.nrr, self.gain_db, self.floor_db = mono, nvel, nrr, gain_db, floor_db
        if FULL:
            self.nvel = max(self.nvel, 6)
        else:
            self.nvel, self.nrr = min(self.nvel, 3), min(self.nrr, 3)
            if cat in CYMBALS:
                self.nvel, self.nrr = min(self.nvel, 2), min(self.nrr, 2)
                self.floor_db = max(floor_db, -54.0)
                self.maxlen = min(self.maxlen, 3.0)

    def gid(self):
        return '%s/%s' % (slug(self.pack), slug(self.key))

def vel_value(v):
    if v is None:
        return 1
    if isinstance(v, (int, float)):
        return v
    s = str(v).lower()
    named = {'softest': 1, 'pp': 1, 'soft': 2, 'p': 2, 'mp': 3, 'med': 3, 'medium': 3, 'mf': 4, 'hard': 5, 'loud': 5, 'f': 5, 'hardest': 6, 'ff': 6, 'accent': 7}
    if s in named:
        return named[s]
    nums = re.findall(r'\d+', s)
    return int(nums[-1]) if nums else 1

def scan(local, regex, cat_fn, mics=None, name_fn=None, pack='', src='', tags=(), genres=(), skip=None, **kw):
    """Agrupa archivos de un repositorio en instrumentos.
    regex: grupos con nombre; 'mic', 'vl' y 'rr' son especiales; el resto forma la clave del instrumento.
    mics: dict mic -> (ganancia, pan) o función(dict de grupos) -> ese dict."""
    rx = re.compile(regex)
    groups = {}
    for p in tree(local):
        m = rx.fullmatch(p)
        if not m:
            continue
        gd = {k: v for k, v in m.groupdict().items()}
        if skip and skip(gd):
            continue
        mic = gd.get('mic')
        g, pan = 1.0, 0.0
        if mics is not None:
            mm = mics(gd) if callable(mics) else mics
            if mic not in mm:
                continue
            g, pan = mm[mic]
        vl = vel_value(gd.get('vl')); rr = int(gd.get('rr') or 1)
        key = tuple((k, v) for k, v in sorted(gd.items()) if k not in ('mic', 'vl', 'rr') and v is not None)
        groups.setdefault(key, {}).setdefault((vl, rr), []).append((p, g, pan))
    insts = []
    for key, members in sorted(groups.items()):
        d = dict(key)
        cat = cat_fn(d)
        if cat is None:
            continue
        # sólo miembros con todos los micrófonos que tiene el más completo
        full = max(len(v) for v in members.values())
        members = {k: v for k, v in members.items() if len(v) == full}
        name = name_fn(d) if name_fn else human(*[v for k, v in key])
        ikey = '-'.join(str(v) for k, v in key)
        insts.append(Inst(pack, src, ikey, name, cat, members, tags, genres, **kw))
    return insts

def select(inst):
    vls = sorted(set(v for v, r in inst.members))
    if len(vls) > inst.nvel:
        idx = sorted(set(int(round(i)) for i in np.linspace(0, len(vls) - 1, inst.nvel)))
        vls = [vls[i] for i in idx]
    out = []
    for i, v in enumerate(vls):
        rrs = sorted(r for vv, r in inst.members if vv == v)
        take = rrs[:inst.nrr] if v == vls[-1] else rrs[:1]
        for j, r in enumerate(take):
            out.append((i, len(vls), j, inst.members[(v, r)]))
    return out

def measure_layers(inst, local):
    """Para bibliotecas sin capas de velocidad en el nombre (DrumGizmo): mide cada golpe y lo reparte en capas."""
    hits = sorted(inst.members)
    paths = {}
    for k in hits:
        for p, g, pan in inst.members[k]:
            if inst.measure in p.rsplit('-', 1)[-1]:
                paths[k] = p
    files = fetch(local, list(paths.values()))
    levels = []
    for k, p in paths.items():
        if p in files:
            x, sr = read(files[p])
            levels.append((float(np.abs(x[:int(0.12 * sr)]).max()), k))
    levels.sort()
    if not levels:
        return
    nb = min(inst.nvel, len(levels))
    new = {}
    for i, (lv, k) in enumerate(levels):
        b = min(nb - 1, int(i * nb / len(levels)))
        rr = sum(1 for kk in new if kk[0] == b) + 1
        new[(b + 1, rr)] = inst.members[k]
    # en la capa más fuerte, los golpes más fuertes primero
    top = nb
    tops = [(lv, k) for i, (lv, k) in enumerate(levels) if min(nb - 1, int(i * nb / len(levels))) == nb - 1]
    tops.sort(reverse=True)
    for j, (lv, k) in enumerate(tops):
        new[(top, j + 1)] = inst.members[k]
    inst.members = new

def render(inst, local, root, rel_dir=None):
    """Procesa un instrumento y devuelve las entradas del catálogo."""
    if getattr(inst, 'measure', None):
        measure_layers(inst, local)
    chosen = select(inst)
    files = fetch(local, [p for c in chosen for (p, g, pan) in c[3]])
    rendered = []
    for layer, nl, rr, parts in chosen:
        if any(p not in files for p, g, pan in parts):
            continue
        x, sr = mix([(files[p], g, pan) for p, g, pan in parts])
        if sr > TARGET_SR and not FULL:
            x = resample(x, sr, TARGET_SR); sr = TARGET_SR
        x = trim(x, sr, inst.maxlen, floor_db=inst.floor_db)
        if x.shape[0] < int(0.005 * sr):
            continue
        rendered.append([layer, nl, rr, x, sr])
    if not rendered:
        return []
    pk = max(float(np.abs(r[3]).max()) for r in rendered)
    g = (10 ** (-1.0 / 20.0) / pk) * (10 ** (inst.gain_db / 20.0)) if pk > 0 else 1.0
    mono_ok = all(maybe_mono(r[3]).shape[1] == 1 for r in rendered) if inst.mono is None else inst.mono
    nl_real = len(sorted(set(r[0] for r in rendered)))
    layers = {l: i for i, l in enumerate(sorted(set(r[0] for r in rendered)))}
    entries = []
    folder = rel_dir or '%s/%s' % (CATS[inst.cat][1], inst.cat)
    base = '%s-%s' % (slug(inst.pack), slug(inst.key))
    for layer, nl, rr, x, sr in rendered:
        y = x * g
        if mono_ok:
            y = (0.5 * (y[:, 0] + y[:, 1]))[:, None] if y.shape[1] == 2 else y
        li = layers[layer]
        fname = '%s-v%d%s.flac' % (base, li + 1, '-rr%d' % (rr + 1) if rr > 0 else '')
        rel = '%s/%s' % (folder, fname)
        write(os.path.join(root, rel), y, sr)
        a = analyse(y, sr, tonal=inst.tonal)
        e = dict(id=rel[:-5], archivo=rel, nombre=inst.name, cat=inst.cat, grupo=inst.gid(), pack=inst.pack, fuente=inst.src,
                 capa=li + 1, capas=nl_real, rr=rr + 1, sr=sr, canales=y.shape[1], **a)
        if inst.tags:
            e['etiquetas'] = inst.tags
        if inst.genres:
            e['generos'] = inst.genres
        entries.append(e)
    return entries
