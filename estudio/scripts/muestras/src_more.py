# Biblioteca Racoon: cajas de ritmo clásicas, Sonic Pi, VCSL, VSCO 2 CE, Hydrogen, DRS Kit
import re, os
from model import scan, human, Inst, vel_value
from core import tree
import src_drums as D

ELECTRO = ['house', 'techno', 'electro', 'hiphop', 'trap', 'synthwave', 'disco', 'funk', 'pop', 'reggaeton', 'dembow', 'miami bass', 'boombap']
DYN = {'pppp': 1, 'ppp': 2, 'pp': 3, 'p': 4, 'mp': 5, 'mf': 6, 'f': 7, 'ff': 8, 'fff': 9, 'soft': 3, 'quiet': 3, 'medium': 5, 'med': 5,
       'loud': 8, 'hard': 8, 'softest': 1, 'hardest': 9}

def one(local, path, cat, name, pack, src, genres=(), tags=(), **kw):
    return Inst(pack, src, os.path.splitext(os.path.basename(path))[0], name, cat, {(1, 1): [(path, 1.0, 0.0)]}, tags, genres, **kw)

# ------------------------------------------------------------------ TR-808 de Michael Fischer (CC0, tidalcycles)
def tr808():
    L = 'sounds-tr808-fischer'
    names = {'bd8': ('bombo', 'Bombo'), 'sd8': ('caja', 'Caja'), 'cy8': ('crash', 'Platillo'), 'oh8': ('hihat_abierto', 'Hi-hat abierto'),
             'ch8': ('hihat_cerrado', 'Hi-hat cerrado'), 'lt8': ('tom_grave', 'Tom grave'), 'mt8': ('tom_medio', 'Tom medio'),
             'ht8': ('tom_agudo', 'Tom agudo'), 'lc8': ('conga', 'Conga grave'), 'mc8': ('conga', 'Conga media'), 'hc8': ('conga', 'Conga aguda'),
             'cb8': ('cencerro', 'Cencerro'), 'cl8': ('clave', 'Clave'), 'cp8': ('palmas', 'Palmas'), 'ma8': ('shaker', 'Maracas'), 'rs8': ('aro', 'Rimshot')}
    out = []
    for p in tree(L):
        m = re.fullmatch(r'(\w\w8)/([A-Z]+)(\d*)\.WAV', p)
        if not m or m.group(1) not in names:
            continue
        cat, nm = names[m.group(1)]
        knobs = m.group(3)
        desc = ''
        if len(knobs) == 4:
            a, b = knobs[:2], knobs[2:]
            w1 = {'bd8': 'tono', 'sd8': 'tono', 'cy8': 'tono'}[m.group(1)]
            w2 = {'bd8': 'caída', 'sd8': 'brillo', 'cy8': 'caída'}[m.group(1)]
            pos = lambda s: {'00': '0', '25': '2.5', '50': '5', '75': '7.5', '10': '10'}[s]
            desc = ' %s %s %s %s' % (w1, pos(a), w2, pos(b))
        elif len(knobs) == 2:
            desc = ' %s %s' % ('afinación' if m.group(1) != 'oh8' else 'caída', {'00': '0', '25': '2.5', '50': '5', '75': '7.5', '10': '10'}[knobs])
        tags = ['808', 'analógico']
        maxlen = None
        if m.group(1) == 'bd8' and knobs[2:] in ('75', '10'):
            tags.append('bajo 808'); maxlen = 4.0
        out.append(one(L, p, cat, 'TR-808 ' + nm + desc, 'TR-808', 'tr808', ELECTRO + ['trap', 'drill', 'miami bass', 'reggaeton'], tags, maxlen=maxlen,
                       floor_db=-70.0 if cat == 'bombo' else -60.0))
    return L, out

# ------------------------------------------------------------------ TR-707 (dominio público, fluid-music/open-drums)
def tr707():
    L = 'open-drums'
    names = {'BassDrum1': ('bombo', 'Bombo 1'), 'BassDrum2': ('bombo', 'Bombo 2'), 'Snare1': ('caja', 'Caja 1'), 'Snare2': ('caja', 'Caja 2'),
             'HiTom': ('tom_agudo', 'Tom agudo'), 'MedTom': ('tom_medio', 'Tom medio'), 'LowTom': ('tom_grave', 'Tom grave'),
             'HhC': ('hihat_cerrado', 'Hi-hat cerrado'), 'HhO': ('hihat_abierto', 'Hi-hat abierto'), 'Crash': ('crash', 'Crash'), 'Ride': ('ride', 'Ride'),
             'Cowbell': ('cencerro', 'Cencerro'), 'HandClap': ('palmas', 'Palmas'), 'RimShot': ('aro', 'Rimshot'), 'Tamb': ('pandereta', 'Pandereta')}
    out = []
    for p in tree(L):
        m = re.fullmatch(r'tr-707/TR707WAV/(\w+)\.wav', p, re.I)
        if m and m.group(1) in names:
            c, n = names[m.group(1)]
            out.append(one(L, p, c, 'TR-707 ' + n, 'TR-707', 'tr707', ELECTRO + ['italo', 'freestyle', 'electro'], ['707', 'digital']))
    return L, out

# ------------------------------------------------------------------ oramics/sampled: CR-78, LinnDrum, MRK-2, TR-505 (dominio público), Pearl (CC-BY 3.0)
CATWORDS = [
    (r'kick|bd|bass.?drum|bombo', 'bombo'), (r'snare|sd\b|sn\b', 'caja'), (r'rim|stick|rs\b', 'aro'), (r'clap|cp\b', 'palmas'),
    (r'hihat-?open|hh-?open|hhopen|open', 'hihat_abierto'), (r'hihat|hithat|hh|hat|ch\b', 'hihat_cerrado'), (r'ride', 'ride'),
    (r'crash|cymbal|cymball|cy\b', 'crash'), (r'splash|china', 'platillo_fx'), (r'tom-?(ll|l|low|lo|1)\b|tom-low|floor', 'tom_grave'),
    (r'tom-?(hh|h|hi|high|3)\b|tom-high|(high|hi)-?tom', 'tom_agudo'), (r'low-?tom|lt\d', 'tom_grave'), (r'ht\d', 'tom_agudo'), (r'tom', 'tom_medio'), (r'conga', 'conga'), (r'bongo', 'bongo'),
    (r'timbal', 'timbal'), (r'cowb|cowbell', 'cencerro'), (r'clave', 'clave'), (r'block|wood', 'bloque'), (r'tamb', 'pandereta'),
    (r'cabasa', 'cabasa'), (r'shaker|maraca', 'shaker'), (r'guiro', 'guiro'), (r'agogo', 'agogo'), (r'triangle', 'triangulo'),
]
def guess(name):
    s = name.lower()
    for rx, c in CATWORDS:
        if re.search(rx, s):
            return c
    return 'percusion'

NAMES_ES = {'kick': 'Bombo', 'snare': 'Caja', 'hihat': 'Hi-hat', 'open': 'abierto', 'closed': 'cerrado', 'clap': 'Palmas', 'crash': 'Crash',
            'ride': 'Ride', 'tom': 'Tom', 'conga': 'Conga', 'bongo': 'Bongó', 'cowbell': 'Cencerro', 'cowb': 'Cencerro', 'clave': 'Clave',
            'block': 'Bloque', 'tamb': 'Pandereta', 'cabasa': 'Cabasa', 'rim': 'Aro', 'rimshot': 'Rimshot', 'stick': 'Baqueta', 'cymbal': 'Platillo',
            'cymball': 'Platillo', 'guiro': 'Güiro', 'timbal': 'Timbal', 'accent': 'acento', 'long': 'largo', 'short': 'corto', 'metal': 'metálico',
            'alt': 'alterno', 'high': 'agudo', 'low': 'grave', 'mid': 'medio', 'h': 'agudo', 'hh': 'muy agudo', 'l': 'grave', 'll': 'muy grave',
            'lll': 'más grave', 'm': 'medio', 'hithat': 'Hi-hat', 'splash': 'Splash', 'clap': 'Palmas', 'shaker': 'Shaker', 'clav': 'Clave'}
def es(stem):
    w = [NAMES_ES.get(t.lower(), t) for t in re.split(r'[-_ ]+', stem) if t and not t.isdigit() and t.lower() not in ('tr505',)]
    nums = [t for t in re.split(r'[-_ ]+', stem) if t.isdigit()]
    return ' '.join(w + nums)

def oramics():
    L = 'sampled'
    out = []
    machines = {'CR-78': ('CR-78', 'cr78', ['disco', 'synthpop', 'new wave', 'lofi', 'soul', 'indie']),
                'LM-2': ('LinnDrum', 'linndrum', ['pop', 'synthpop', 'funk', 'rnb', 'freestyle', 'synthwave', 'new wave']),
                'MRK-2': ('Rhythm King', 'mrk2', ['soul', 'funk', 'lofi', 'psicodelia']),
                'TR-505': ('TR-505', 'tr505', ['house', 'pop', 'electro', 'freestyle'])}
    for d, (pack, src, gen) in machines.items():
        for p in tree(L):
            m = re.fullmatch(r'DM/%s/samples/(.+)\.wav' % re.escape(d), p)
            if m:
                stem = m.group(1)
                out.append(one(L, p, guess(stem.replace('tr505-', '')), pack + ' ' + es(stem), pack, src, gen, ['caja de ritmo']))
    for p in tree(L):
        m = re.fullmatch(r'DRUMS/pearl-master-studio/samples/(.+)\.wav', p)
        if m:
            out.append(one(L, p, guess(m.group(1)), 'Pearl Master ' + es(m.group(1)), 'Pearl Master', 'pearl', D.ACOUSTIC))
    return L, out

def smpld_machines():
    L = 'drum-machines'
    machines = {'Casio-RZ1': ('Casio RZ-1', ['hiphop', 'boombap', 'electro', 'lofi']), 'Casio-SK1': ('Casio SK-1', ['lofi', 'hiphop', 'chiptune']),
                'MFB-512': ('MFB-512', ['techno', 'electro', 'ebm']), 'Micro-Rhythmer-12': ('Micro Rhythmer', ['lofi', 'synthpop']),
                'Roland-CR-8000': ('CR-8000', ['disco', 'synthpop', 'house', 'italo']),
                'Sequential-Circuits-Drumtraks': ('Drumtraks', ['pop', 'synthpop', 'freestyle', 'electro']),
                'Yamaha-MR10': ('Yamaha MR-10', ['lofi', 'synthpop', 'chiptune'])}
    out = []
    for p in tree(L):
        m = re.fullmatch(r'([\w-]+)/([\w-]+)\.ogg', p)
        if m and m.group(1) in machines:
            pack, gen = machines[m.group(1)]
            out.append(one(L, p, guess(m.group(2)), pack + ' ' + es(m.group(2)), pack, 'smpldsnds', gen, ['caja de ritmo']))
    return L, out

# ------------------------------------------------------------------ Hydrogen (GPL): GMRockKit y TR808EmulationKit
def hydrogen():
    L = 'hydrogen'
    out = scan(L, r'data/drumkits/GMRockKit/(?P<art>[A-Za-z0-9]+)-(?P<vl>Softest|Soft|Med|Hardest|Hard)\.wav',
               lambda gd: {'Kick': 'bombo', 'Snare': 'caja', 'SnareRimshot': 'caja', 'SideStick': 'aro', 'HatClosed': 'hihat_cerrado',
                           'HatPedal': 'hihat_pedal', 'HatOpen': 'hihat_abierto', 'HatSemiOpen': 'hihat_abierto', 'Ride': 'ride', 'Bell': 'ride',
                           'Crash': 'crash', 'Splash': 'platillo_fx', 'Tom1': 'tom_agudo', 'Tom2': 'tom_medio', 'TomFloor': 'tom_grave',
                           'Cowbell': 'cencerro'}.get(gd['art']), None,
               lambda gd: 'GM Rock ' + {'Kick': 'Bombo', 'Snare': 'Caja', 'SnareRimshot': 'Caja rimshot', 'SideStick': 'Baqueta cruzada',
                                        'HatClosed': 'Hi-hat cerrado', 'HatPedal': 'Hi-hat de pedal', 'HatOpen': 'Hi-hat abierto',
                                        'HatSemiOpen': 'Hi-hat semiabierto', 'Ride': 'Ride', 'Bell': 'Ride campana', 'Crash': 'Crash',
                                        'Splash': 'Splash', 'Tom1': 'Tom 1', 'Tom2': 'Tom 2', 'TomFloor': 'Tom de piso', 'Cowbell': 'Cencerro'}[gd['art']],
               pack='GM Rock', src='hydrogen_gmrock', genres=['rock', 'pop', 'punk', 'metal'])
    for p in tree(L):
        m = re.fullmatch(r'data/drumkits/TR808EmulationKit/808_(\w+)\.flac', p)
        if m:
            s = m.group(1)
            c = {'Kick_Long': 'bombo', 'Kick_Short': 'bombo', 'Snare_1': 'caja', 'Snare_2': 'caja', 'Hat_Closed': 'hihat_cerrado', 'Hat_Pedal': 'hihat_pedal',
                 'Hat_Open': 'hihat_abierto', 'Cymbal': 'crash', 'Clap': 'palmas', 'Clave': 'clave', 'Conga': 'conga', 'Cowbell': 'cencerro',
                 'Shaker': 'shaker', 'Tom_Hi': 'tom_agudo', 'Tom_Mid': 'tom_medio', 'Tom_Low': 'tom_grave'}.get(s, 'percusion')
            n = {'Kick_Long': 'Bombo largo', 'Kick_Short': 'Bombo corto', 'Snare_1': 'Caja 1', 'Snare_2': 'Caja 2', 'Hat_Closed': 'Hi-hat cerrado',
                 'Hat_Pedal': 'Hi-hat de pedal', 'Hat_Open': 'Hi-hat abierto', 'Cymbal': 'Platillo', 'Clap': 'Palmas', 'Clave': 'Clave', 'Conga': 'Conga',
                 'Cowbell': 'Cencerro', 'Shaker': 'Shaker', 'Tom_Hi': 'Tom agudo', 'Tom_Mid': 'Tom medio', 'Tom_Low': 'Tom grave'}.get(s, s)
            out.append(one(L, p, c, '808 Emulado ' + n, '808 Emulado', 'hydrogen_808', ELECTRO, ['808']))
    return L, out

# ------------------------------------------------------------------ Sonic Pi (CC0): golpes electrónicos, tablas, vinilo, glitch
def sonicpi():
    L = 'sonic-pi'
    out = []
    pref = {'bd': 'bombo', 'sn': 'caja', 'hat': 'hihat_cerrado', 'ride': 'ride', 'tabla': 'tabla', 'glitch': 'efecto', 'elec': None, 'drum': None,
            'perc': None, 'vinyl': 'efecto', 'misc': 'efecto', 'mehackit': 'efecto'}
    nm = {'bd': 'Bombo', 'sn': 'Caja', 'hat': 'Hi-hat', 'ride': 'Ride', 'tabla': 'Tabla', 'glitch': 'Glitch', 'vinyl': 'Vinilo', 'misc': 'Efecto',
          'mehackit': 'Mehackit', 'elec': 'Electrónico', 'drum': 'Batería', 'perc': 'Percusión'}
    for p in tree(L):
        m = re.fullmatch(r'etc/samples/(\w+?)_(\w+)\.flac', p)
        if not m or m.group(1) not in pref:
            continue
        k, rest = m.group(1), m.group(2)
        c = pref[k]
        if k == 'drum':
            c = guess(rest.replace('cymbal_closed', 'hihat').replace('cymbal_open', 'hihat-open').replace('cymbal_pedal', 'hh pedal')
                      .replace('bass', 'kick').replace('heavy_kick', 'kick').replace('roll', 'snare roll'))
            if 'pedal' in rest:
                c = 'hihat_pedal'
            if rest == 'roll':
                c = 'caja'
        if k == 'elec':
            c = guess(rest.replace('kick', 'kick')) if any(w in rest for w in ('kick', 'snare', 'cymbal', 'tom', 'wood', 'triangle')) else 'efecto'
            if 'tom' in rest:
                c = 'tom_medio'
            if 'cymbal' in rest:
                c = 'crash'
        if k == 'perc':
            c = {'bell': 'campana', 'bell2': 'campana', 'snap': 'chasquido', 'snap2': 'chasquido', 'door': 'efecto', 'impact1': 'efecto',
                 'impact2': 'efecto', 'swash': 'platillo_fx', 'swoosh': 'efecto', 'till': 'efecto'}.get(rest, 'percusion')
        if k == 'hat' and rest in ('cab', 'raw', 'noiz', 'psych', 'sci', 'zap', 'mess'):
            c = 'hihat_abierto' if rest in ('psych', 'sci', 'mess') else 'hihat_cerrado'
        name = 'Sonic Pi %s %s' % (nm[k], rest.replace('_', ' '))
        gen = ELECTRO if k in ('bd', 'sn', 'hat', 'elec', 'drum', 'ride') else (['world', 'india', 'trip-hop', 'downtempo'] if k == 'tabla' else ['lofi', 'idm', 'experimental', 'hiphop'])
        out.append(one(L, p, c, name, 'Sonic Pi', 'sonicpi', gen, maxlen=6.0 if c == 'efecto' else None))
    return L, out

# ------------------------------------------------------------------ escáner genérico por "tallo" del nombre (VCSL, VSCO)
DROP = set('agogo anvil brake drum cabasa cajon clap cymbal claves cowbell fing cymb flexatone gong guiro hb hat hi ratchet l shaker slapstick sleighbells sleighbell log sus tamb triangle vibraslap b new bassdrum bongo conga darbuka h snare tenor tom rope wood hdrum s m'.split())
SKIP_WORDS = ('roll', 'cresc', 'rel', 'nloop', 'loop', 'scrape', 'rub', 'bowed', 'bow', 'gliss', 'click', 'sfx', 'special', 'release', 'decay', 'swell')
def stem_scan(local, base, dirs, pack, src, genres, keep_long=False, tags=()):
    """dirs: lista de (regex de carpeta relativa, cat o función(tallo)->cat, prefijo del nombre)."""
    out = []
    groups = {}
    for p in tree(local):
        if not p.startswith(base) or not p.lower().endswith(('.wav', '.flac')):
            continue
        rel = p[len(base):]
        folder, fname = os.path.split(rel)
        for drx, cat, prefix in dirs:
            if re.fullmatch(drx, folder):
                break
        else:
            continue
        stem = os.path.splitext(fname)[0]
        toks = re.split(r'[_\-\s]+', stem)
        vl, rr, rest = None, None, []
        for t in toks:
            tl = t.lower()
            if re.fullmatch(r'vl?\d+', tl):
                vl = int(re.findall(r'\d+', tl)[0])
            elif re.fullmatch(r'rr?\d+', tl):
                rr = int(re.findall(r'\d+', tl)[0])
            elif tl in DYN:
                vl = DYN[tl]
            elif re.fullmatch(r'(pppp|ppp|pp|p|mp|mf|f|ff|fff)\d+', tl):
                d = re.match(r'[a-z]+', tl).group(0); vl = DYN[d]; rr = int(tl[len(d):]) + 1
            elif tl in ('mid', 'sum', 'main', 'close', 'far', 'mic'):
                continue
            elif re.fullmatch(r'\d+', tl) and rest:
                rr = int(tl) + 1 if rr is None else rr
            else:
                rest.append(t)
        key = '_'.join(rest)
        if not keep_long and any(w in key.lower() for w in SKIP_WORDS):
            continue
        c = cat(key) if callable(cat) else cat
        if c is None:
            continue
        gk = (folder, key)
        groups.setdefault(gk, (c, prefix, {}))[2].setdefault((vl or 5, rr or 1), [(p, 1.0, 0.0)])
    for (folder, key), (c, prefix, members) in sorted(groups.items()):
        parts = []
        for t in re.split(r'_+', re.sub(r'(?<=[a-z])(?=[A-Z])', '_', key)):
            base = re.sub(r'\d+', '', t).lower()
            if base in DROP:
                d = re.findall(r'\d+', t)
                if d:
                    parts.append(d[0])
                continue
            parts.append(t)
        nice = human(*parts)
        name = ('%s %s' % (prefix, nice)).strip() if prefix else nice
        out.append(Inst(pack, src, folder.replace('/', '-') + '-' + key, name, c, members, tags, genres))
    return out

def vcsl_perc():
    L = 'VCSL'
    I, M = 'Idiophones/Struck Idiophones/', 'Membranophones/'
    dirs = [
        (r'Agogo Bells', 'agogo', 'Agogó'), (r'Anvil', 'metal', 'Yunque'), (r'Brake Drum', 'metal', 'Tambor de freno'),
        (r'Cabasa', 'cabasa', 'Cabasa'), (r'Cajon', 'cajon', 'Cajón'), (r'Claps', 'palmas', 'Palmas'),
        (r'Clash Cymbals \d', 'crash', 'Platillos de choque'), (r'Claves', 'clave', 'Claves'),
        (r'Cowbells', 'cencerro', 'Cencerro'), (r'Finger Cymbals', 'campana', 'Crótalos'), (r'Flexatone', 'efecto', 'Flexatón'),
        (r'Gong \d', 'orquesta', 'Gong'), (r'Guiro', 'guiro', 'Güiro'), (r'Hand Bells, Nepalese', 'campana', 'Campanas nepalesas'),
        (r'Hi-Hat Cymbal', lambda k: 'hihat_abierto' if 'O' in k.split('_')[-1] or 'Loose' in k else 'hihat_cerrado', 'Hi-hat de orquesta'),
        (r'Ratchet', 'efecto', 'Matraca'), (r'Shaker, Large', 'shaker', 'Shaker grande'), (r'Shaker, Small', 'shaker', 'Shaker chico'),
        (r'Slapstick', 'palmas', 'Látigo'), (r'Sleigh Bells', 'cascabeles', 'Cascabeles'), (r'Slit Drum', 'bloque', 'Tambor de tronco'),
        (r'Suspended Cymbal \d', lambda k: 'ride' if 'bell' in k.lower() else 'crash', 'Platillo suspendido'),
        (r'Tambourine \d', 'pandereta', 'Pandereta'), (r'Triangles', 'triangulo', 'Triángulo'), (r'Vibraslap/Legacy', 'percusion', 'Vibraslap'),
        (r'Woodblock', 'bloque', 'Bloque'), (r'Bell Tree/Stroke', 'campana', 'Árbol de campanas'),
    ]
    out = stem_scan(L, I, dirs, 'VCSL', 'vcsl', ['orquestal', 'latino', 'pop', 'cine', 'folk', 'house'])
    mdirs = [
        (r'Struck Membranophones/Bass Drum \d', 'orquesta', 'Bombo de concierto'), (r'Struck Membranophones/Bongos', 'bongo', 'Bongó'),
        (r'Struck Membranophones/Conga', 'conga', 'Conga'), (r'Struck Membranophones/Darbuka', 'darbuka', 'Darbuka'),
        (r'Struck Membranophones/Frame Drum', 'tambor', 'Tambor de marco'),
        (r'Struck Membranophones/Snare Drum, Modern \d', 'caja', 'Caja de concierto'),
        (r'Struck Membranophones/Snare Drum, Rope Tension(/\w+)?', 'caja', 'Caja de cuerdas'),
        (r'Struck Membranophones/Legacy Snares/(OldSnare|drum\d|drum3_marching)', lambda k: 'aro' if 'rim' in k.lower() and 'shot' not in k.lower() else 'caja', 'Caja antigua'),
        (r'Struck Membranophones/Legacy Toms/tenor_(higher|lower)', 'tom_medio', 'Tom tenor'),
        (r'Struck Membranophones/Tom \d/(Mallet|Stick)', 'tom_grave', 'Tom de concierto'),
    ]
    out += stem_scan(L, M, mdirs, 'VCSL', 'vcsl', ['orquestal', 'latino', 'cine', 'world', 'marcha'])
    for i in out:
        if i.cat == 'tom_medio' and 'lower' in i.key:
            i.cat = 'tom_grave'
        if 'Muted' in i.key or 'muted' in i.key:
            i.name += ' apagado' if 'apagado' not in i.name else ''
    return L, out

def vsco_perc():
    L = 'VSCO-2-CE'
    dirs = [(r'Percussion', lambda k: None if any(w in k.lower() for w in ('glock', 'marimba', 'xylo', 'timpani', 'tb_hit')) else
             {'anvil': 'metal', 'bdrumnewhit': 'orquesta', 'belltree': 'campana', 'brakedrum': 'metal', 'claves': 'clave', 'conga': 'conga',
              'quinto': 'conga', 'tumba': 'conga', 'cowbell': 'cencerro', 'guiro': 'guiro', 'logdrumhi': 'bloque', 'logdrumlo': 'bloque',
              'ratchet': 'efecto', 'sleighbells': 'cascabeles', 'snare': 'caja', 'tamb': 'pandereta', 'triangle': 'triangulo', 'alien': 'efecto',
              'cymbal': 'crash', 'gonghit': 'orquesta'}.get(re.sub(r'\d', '', k.split('_')[0]).lower()), 'VSCO')]
    out = stem_scan(L, '', dirs, 'VSCO 2', 'vsco', ['orquestal', 'cine', 'latino', 'pop'])
    return L, out

# ------------------------------------------------------------------ DRS Kit (DrumGizmo, CC-BY 4.0)
def drs():
    L = 'DrumGizmo.DRSKit'
    OH = {'OHL': (0.5, -1.0), 'OHR': (0.5, 1.0), 'AmbL': (0.3, -1.0), 'AmbR': (0.3, 1.0)}
    def k(close, extra=None, oh=OH):
        d = dict(oh); d.update(close)
        return d
    spec = {
        'Kdrum_without_contact': ('bombo', 'DRS Bombo', k({'Kdrum_front': (1.0, 0.0), 'Kdrum_back': (0.7, 0.0)})),
        'Kdrum_with_contact': ('bombo', 'DRS Bombo con contacto', k({'Kdrum_front': (1.0, 0.0), 'Kdrum_back': (0.7, 0.0)})),
        'Snare': ('caja', 'DRS Caja', k({'Snare_top': (1.0, 0.0), 'Snare_bottom': (0.45, 0.0)})),
        'Snare_rim': ('caja', 'DRS Caja rimshot', k({'Snare_top': (1.0, 0.0), 'Snare_bottom': (0.4, 0.0)})),
        'Snare_whisker': ('caja', 'DRS Caja escobilla', k({'Snare_top': (1.0, 0.0), 'Snare_bottom': (0.4, 0.0)})),
        'Snare_rest': ('caja', 'DRS Caja apagada', k({'Snare_top': (1.0, 0.0), 'Snare_bottom': (0.4, 0.0)})),
        'Tom1': ('tom_agudo', 'DRS Tom 1', k({'Tom1': (1.0, 0.2)})), 'Tom2': ('tom_medio', 'DRS Tom 2', k({'Tom2': (1.0, -0.1)})),
        'Tom3': ('tom_grave', 'DRS Tom de piso', k({'Tom3': (1.0, -0.3)})),
        'Hihat_closed': ('hihat_cerrado', 'DRS Hi-hat cerrado', {'Hihat': (1.0, 0.3), 'OHL': (0.6, -1.0), 'OHR': (0.6, 1.0)}),
        'Hihat_closed_shank': ('hihat_cerrado', 'DRS Hi-hat cerrado canto', {'Hihat': (1.0, 0.3), 'OHL': (0.6, -1.0), 'OHR': (0.6, 1.0)}),
        'Hihat_foot': ('hihat_pedal', 'DRS Hi-hat de pie', {'Hihat': (1.0, 0.3), 'OHL': (0.6, -1.0), 'OHR': (0.6, 1.0)}),
        'Hihat_semi_open': ('hihat_abierto', 'DRS Hi-hat semiabierto', {'Hihat': (1.0, 0.3), 'OHL': (0.6, -1.0), 'OHR': (0.6, 1.0)}),
        'Hihat_open': ('hihat_abierto', 'DRS Hi-hat abierto', {'Hihat': (1.0, 0.3), 'OHL': (0.6, -1.0), 'OHR': (0.6, 1.0)}),
        'Ride_tip': ('ride', 'DRS Ride punta', {'Ride': (0.7, -0.4), 'OHL': (1.0, -1.0), 'OHR': (1.0, 1.0)}),
        'Ride_tip_bell': ('ride', 'DRS Ride campana', {'Ride': (0.7, -0.4), 'OHL': (1.0, -1.0), 'OHR': (1.0, 1.0)}),
        'Ride_tip_chain': ('ride', 'DRS Ride con cadena', {'Ride': (0.7, -0.4), 'OHL': (1.0, -1.0), 'OHR': (1.0, 1.0)}),
        'Crash_left_tip': ('crash', 'DRS Crash izquierdo', {'OHL': (1.0, -1.0), 'OHR': (1.0, 1.0), 'AmbL': (0.35, -1.0), 'AmbR': (0.35, 1.0)}),
        'Crash_right_tip': ('crash', 'DRS Crash derecho', {'OHL': (1.0, -1.0), 'OHR': (1.0, 1.0), 'AmbL': (0.35, -1.0), 'AmbR': (0.35, 1.0)}),
        'Crash_left_whisker': ('crash', 'DRS Crash escobilla', {'OHL': (1.0, -1.0), 'OHR': (1.0, 1.0)}),
    }
    return L, D.drumgizmo(L, 'DrumGizmo/DRSKit', 'DRS', 'drs', spec, ['rock', 'metal', 'pop', 'indie', 'jazz'])

# ------------------------------------------------------------------ Free Drum Samples de Boochi44 (CC0): tres kits de trap y soul
def boochi():
    L = 'free-drum-samples'
    kits = {'01-hard-trap': ('Trap Duro', ['trap', 'drill', 'hiphop']), '02-bounce': ('Bounce', ['trap', 'reggaeton', 'jersey club', 'hiphop']),
            '03-soulful-vintage': ('Soul Vintage', ['lofi', 'boombap', 'soul', 'hiphop'])}
    cats = {'kicks': 'bombo', '808s': 'bajo_808', 'snares': 'caja', 'claps': 'palmas', 'hi-hats': 'hihat_cerrado', 'open-hats': 'hihat_abierto', 'fx': 'platillo_fx'}
    out = []
    for p in tree(L):
        m = re.fullmatch(r'drum-samples/([\w-]+)/([\w-]+)/([\w-]+)\.wav', p)
        if not m or m.group(1) not in kits:
            continue
        pack, gen = kits[m.group(1)]
        c = cats.get(m.group(2)) or guess(m.group(3).replace('perc-', ''))
        if m.group(2) == 'percs' and 'maraca' in m.group(3):
            c = 'shaker'
        tags = ['trap'] + (['bajo 808'] if c == 'bajo_808' else [])
        out.append(one(L, p, c, pack + ' ' + es(m.group(3).replace('perc-', '')), pack, 'boochi', gen, tags))
    return L, out

ALL = [tr808, tr707, oramics, smpld_machines, hydrogen, sonicpi, vcsl_perc, vsco_perc, drs, boochi]
