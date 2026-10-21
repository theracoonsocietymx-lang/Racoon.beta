# Biblioteca Racoon: baterías y percusiones acústicas (sfzinstruments / Karoryfer / DrumGizmo)
import re
from model import scan, human, Inst
from core import tree

ACOUSTIC = ['rock', 'pop', 'funk', 'jazz', 'soul', 'indie', 'blues', 'reggae', 'lofi', 'hiphop', 'rnb', 'latino']

def first(gd, *names):
    for n in names:
        if gd.get(n):
            return gd[n]
    return ''

PNAMES = {'BongoH_Hit1': 'Bongó agudo', 'BongoL_Hit1': 'Bongó grave', 'Cabasa1_Rub': 'Cabasa frotada', 'Claves1_Hit': 'Claves',
          'Cowbell1_Normal': 'Cencerro', 'LShaker_Shake1D': 'Shaker grande abajo', 'LShaker_Shake1U': 'Shaker grande arriba',
          'Sleighbells_Hit': 'Cascabeles', 'Tamb1_Shake': 'Pandereta agitada', 'Triangle1_Hit': 'Triángulo', 'Triangle1_HitM': 'Triángulo apagado',
          'BellTree_Stroke': 'Árbol de campanas', 'Agogo_High': 'Agogó agudo', 'Agogo_Low': 'Agogó grave', 'Guiro_Fast': 'Güiro rápido', 'Guiro_Med': 'Güiro medio'}

# ------------------------------------------------------------------ Virtuosity Drums (CC0)
def virtuosity():
    L = 'virtuosity_drums'
    def mics(gd):
        p = gd['piece']
        if p == 'kick':
            return {'kickmic': (1.0, 0.0), 'oh': (0.45, 0.0), 'room': (0.3, 0.0)}
        if p in ('snare', 'snareoff'):
            return {'snaremic': (1.0, 0.0), 'oh': (0.5, 0.0), 'room': (0.3, 0.0)}
        if p in ('htom', 'ltom'):
            return {'mid': (1.0, 0.0), 'oh': (0.55, 0.0), 'room': (0.3, 0.0)}
        return {'oh': (1.0, 0.0), 'room': (0.35, 0.0)}
    def cat(gd):
        p, a = gd['piece'], gd['art']
        if a in ('roll', 'flam', 'buzz') and p.startswith('snare'):
            return 'caja' if a == 'flam' else None
        return {'kick': 'bombo', 'htom': 'tom_agudo', 'ltom': 'tom_grave', 'crash': 'crash', 'flatride': 'ride', 'ride': 'ride'}.get(p) or (
            'aro' if a == 'crossstick' else 'caja' if p.startswith('snare') else
            'hihat_pedal' if a == 'pedal' else 'hihat_abierto' if a in ('open', 'half', '34', 'splash') else 'hihat_cerrado')
    def name(gd):
        piece = {'kick': 'Bombo', 'snare': 'Caja', 'snareoff': 'Caja sin bordonero', 'htom': 'Tom agudo', 'ltom': 'Tom grave', 'hh': 'Hi-hat',
                 'crash': 'Crash', 'flatride': 'Ride plano', 'ride': 'Ride'}[gd['piece']]
        art = {'34': 'tres cuartos'}.get(gd['art'], human(gd['art']))
        return ('Virtuosity %s %s' % (piece, art)).replace('Crash crash', 'Crash').replace('Ride ride', 'Ride').strip()
    out = scan(L, r'Samples/(?P<mic>kickmic|snaremic|mid|oh|room)/(?P<piece>[a-z]+)/(?P=mic)_(?P=piece)_(?P<art>[a-z0-9]+)(?:_vl(?P<vl>\d+))?(?:_rr(?P<rr>\d+))?\.flac',
               cat, mics, name, pack='Virtuosity', src='virtuosity', genres=ACOUSTIC)
    # el micrófono lo-fi como paquete aparte (para lo-fi y boom bap)
    def catlofi(gd):
        if gd['piece'] not in ('kick', 'snare', 'hh', 'ride', 'htom', 'ltom') or gd['art'] in ('roll', 'buzz', 'flam', 'stickshot1', 'stickshot2', 'muted', 'halfopen', 'offcenter', '34', 'splash'):
            return None
        return cat(gd)
    out += scan(L, r'Samples/(?P<mic>lofi)/(?P<piece>[a-z]+)/(?P=mic)_(?P=piece)_(?P<art>[a-z0-9]+)(?:_vl(?P<vl>\d+))?(?:_rr(?P<rr>\d+))?\.flac',
                catlofi, {'lofi': (1.0, 0.0)}, lambda gd: name(gd).replace('Virtuosity', 'Virtuosity Lo-Fi'), pack='Virtuosity Lo-Fi', src='virtuosity',
                genres=['lofi', 'hiphop', 'boombap', 'trip-hop'], tags=['lo-fi'], nvel=3, nrr=3)
    # percusiones (mezcla de cercano y ambiente)
    def pcat(gd):
        i = gd['inst']
        return {'agogo': 'agogo', 'belltree': 'campana', 'bongoh': 'bongo', 'bongol': 'bongo', 'cabasa': 'cabasa', 'claves': 'clave',
                'conga': 'conga', 'tumba': 'conga', 'cowbell': 'cencerro', 'guiro': 'guiro', 'shaker': 'shaker', 'sleighbells': 'cascabeles',
                'tambourine': 'pandereta', 'triangle': 'triangulo'}.get(i)
    pm = {'close': (1.0, 0.0), 'oh': (0.5, 0.0)}
    out += scan(L, r'Samples/perc/(?P<mic>close|oh)/(?P<inst>[a-z]+)/(?P<art>[A-Za-z0-9]+_[A-Za-z0-9]+?)(?:_v(?P<vl>\d+)|_(?P<rng>\d+_\d+))?_rr(?P<rr>\d+)_(?:Close|Overhead)\.wav',
                lambda gd: pcat(gd), pm, lambda gd: 'Virtuosity ' + PNAMES.get(gd['art'], human(gd['art'])),
                pack='Virtuosity Perc', src='virtuosity', genres=['latino', 'pop', 'funk', 'house', 'afro', 'reggaeton'])
    out += scan(L, r'Samples/perc/oh/(?P<inst>timbales|woodblock)/(?P<art>[a-z0-9_]+?)_vl(?P<vl>\d+)_rr(?P<rr>\d+)\.wav',
                lambda gd: 'timbal' if gd['inst'] == 'timbales' else 'bloque', None,
                lambda gd: 'Virtuosity %s' % ('Timbal ' + gd['art'][-2:] if gd['inst'] == 'timbales' else 'Bloque ' + human(gd['art'].split('_')[-1])),
                pack='Virtuosity Perc', src='virtuosity', genres=['latino', 'salsa', 'cumbia', 'funk'])
    out += scan(L, r'Samples/perc/oh/cuica/(?P<art>Long_p\d)_Main_vl(?P<vl>\d+)_rr(?P<rr>\d+)\.wav', lambda gd: 'percusion', None,
                lambda gd: 'Virtuosity Cuíca ' + gd['art'][-2:], pack='Virtuosity Perc', src='virtuosity', genres=['samba', 'latino'])
    return L, out

def virtuosity_perc_ranges():
    """Congas/tumbas/vibraslap usan rangos 1_70 en el nombre en vez de _vN: se leen como capas."""
    L = 'virtuosity_drums'
    def cat(gd):
        return {'conga': 'conga', 'tumba': 'conga', 'vibraslap': 'percusion'}[gd['inst']]
    return L, scan(L, r'Samples/perc/(?P<mic>close|oh)/(?P<inst>conga|tumba|vibraslap)/(?P<art>[A-Za-z0-9]+_[A-Za-z0-9]+_[A-Za-z0-9]+?)_(?P<vl>\d+_\d+)_rr(?P<rr>\d+)_(?:Close|Overhead)\.wav',
                   cat, {'close': (1.0, 0.0), 'oh': (0.5, 0.0)},
                   lambda gd: 'Virtuosity ' + {'conga': 'Conga', 'tumba': 'Tumba', 'vibraslap': 'Vibraslap'}[gd['inst']] + ' ' + gd['art'].split('_')[1],
                   pack='Virtuosity Perc', src='virtuosity', genres=['latino', 'salsa', 'house', 'afro'])

# ------------------------------------------------------------------ Frankensnare (CC0): 30 cajas
SNARE_NAMES = {'10': '10x6 fresno', '12': '12x5 acero', '13b': '13x9 abedul', '14a': '14x8 aluminio', '14d': '14x5 arce',
               '14p': '14x6.5 arce', '14s': '14x5 misteriosa', '14t': '14x8 abedul', '16': '16x7 sapele', '18': '18x16 abedul',
               '20': '20x6 álamo', '20m': '20x12 aliso', '22': '22x5 caoba', 'ziggurat': 'zigurat'}
def frankensnare():
    L = 'karoryfer.frankensnare'
    mics = {'top': (1.0, 0.0), 'btm': (0.55, 0.0), 'oh': (0.6, 0.0)}
    def cat(gd):
        a = gd['art']
        return {'sn': 'caja', 'sr': 'caja', 'sb': 'caja', 'sm': 'caja', 'ss': 'aro'}.get(a)
    def name(gd):
        drum, var = gd['drum'], gd['var']
        a = {'sn': '', 'sr': 'rimshot', 'sb': 'escobilla', 'sm': 'apagada', 'ss': 'baqueta cruzada'}[gd['art']]
        v = {'basic': '', 'brush': 'escobilla', 'rimshot': 'rimshot', 'tamb': 'con pandereta', 'muted': 'apagada', 'prep': 'preparada',
             'tight': 'tensa', 'spring': 'con muelle', 'lev1': 'nivel 1', 'lev2': 'nivel 2'}.get(var, var)
        return ' '.join(w for w in ['Frankensnare', SNARE_NAMES.get(drum, drum), v, a] if w).replace('escobilla escobilla', 'escobilla')
    out = scan(L, r'Samples/(?P<drum>[0-9a-z]+?)_(?P<var>[a-z0-9]+)/(?P<mic>top|btm|oh)/(?P<art>s[nsrbm])_vl(?P<vl>\d+)_rr(?P<rr>\d+)\.flac',
               cat, mics, name, pack='Frankensnare', src='frankensnare', genres=ACOUSTIC)
    return L, out

# ------------------------------------------------------------------ Unruly Drums (CC0)
def unruly():
    L = 'karoryfer.unruly-drums'
    def mics(gd):
        d = gd['drum']
        if d.startswith('s'):
            return {'top': (1.0, 0.0), 'btm': (0.5, 0.0), 'ohl': (0.55, -1.0), 'ohr': (0.55, 1.0)}
        if d.startswith('k'):
            return {'in': (1.0, 0.0), 'out': (0.6, 0.0), 'oh': (0.4, 0.0)} if gd['art'].startswith(('clean', 'dirty', 'dirtiest')) else {'cl': (1.0, 0.0), 'oh': (0.5, 0.0)}
        if d == 'h14':
            return {'cl': (1.0, 0.0), 'ohl': (0.6, -1.0), 'ohr': (0.6, 1.0)}
        if d.startswith('t'):
            return {'cl': (1.0, 0.0), 'oh': (0.55, 0.0), 'ohl': (0.5, -1.0), 'ohr': (0.5, 1.0)}
        return {'cl': (0.7, 0.0), 'oh': (1.0, 0.0)}
    def cat(gd):
        d, a = gd['drum'], gd['art']
        if any(w in a for w in ('flutter', 'stir', 'choke', 'pedal', 'stool', 'return')):
            return None
        if d.startswith('s'):
            return 'aro' if ('sstick' in a or 'rc' in a) else 'caja'
        if d.startswith('k'):
            return 'bombo' if not a.startswith('rc') else 'aro'
        if d == 'h14':
            return 'hihat_pedal' if 'foot' in a else ('hihat_abierto' if any(w in a for w in ('open', 'half', 'loose', 'quarter')) else 'hihat_cerrado')
        if d.startswith('c'):
            return 'crash'
        if d.startswith('r'):
            return 'ride'
        if d == 'wb':
            return 'bloque'
        if d.startswith('t'):
            return 'tom'
        return None
    def name(gd):
        d = gd['drum']
        piece = {'s13': 'Caja 13', 's14': 'Caja 14', 's22': 'Caja 22', 'k20': 'Bombo 20', 'h14': 'Hi-hat 14', 'c16': 'Crash 16', 'r20': 'Ride 20', 'wb': 'Bloque'}.get(d, 'Tom ' + d[1:])
        a = re.sub(r'^(s1[34]|s22|kick|hh|cr|ride|woodblock|t\d+)_', '', gd['art'])
        a = a.replace('b_', 'parche inferior ').replace('tom_', 'sin bordonero ')
        return 'Unruly %s %s' % (piece, human(a))
    out = scan(L, r'Samples/(?P<drum>[a-z]+\d*)/(?P<mic>cl|oh|ohl|ohr|in|out|top|btm)/rr\d/(?P<art>[a-z0-9_]+?)(?:_vl(?P<vl>\d+))?_rr(?P<rr>\d+)\.flac',
               cat, mics, name, pack='Unruly', src='unruly', genres=ACOUSTIC)
    toms = [i for i in out if i.cat == 'tom']
    out = [i for i in out if i.cat != 'tom']
    toms_sorted = sorted(set(re.search(r'drum-(\w+)', 'drum-' + i.key.split('-')[1]).group(1) if '-' in i.key else i.key for i in toms))
    for i in toms:
        size = int(re.findall(r'\d+', i.key)[0]) if re.findall(r'\d+', i.key) else 12
        i.cat = 'tom_grave' if size >= 15 else ('tom_medio' if size >= 12 else 'tom_agudo')
        i.maxlen = 2.5
        out.append(i)
    return L, out

# ------------------------------------------------------------------ Big Rusty Drums (CC0)
def big_rusty():
    L = 'karoryfer.big-rusty-drums'
    def mics(gd):
        p = gd['piece']
        if p.startswith('kick'):
            return {'kick': (1.0, 0.0), 'oh': (0.45, 0.0), 'sn': (0.15, 0.0), 'cl': (1.0, 0.0)}
        if p.startswith('snare'):
            return {'top': (1.0, 0.0), 'btm': (0.5, 0.0), 'oh': (0.55, 0.0)}
        if p.startswith('tom'):
            return {'cl': (1.0, 0.0), 'oh': (0.55, 0.0), 'sn': (0.1, 0.0)}
        return {'cl': (0.7, 0.0), 'oh': (1.0, 0.0)}
    skip_art = ('phit', 'preturn', 'choke', 'bflutter', 'brelease', 'bstir', 'release', 'flutter', 'stir', 'pret')
    cym_skip = ('mallet', 'bl')
    def cat(gd):
        p, a = gd['piece'], gd['art']
        if a in skip_art:
            return None
        if p.startswith('kick'):
            return 'aro' if a == 'rc' else 'bombo'
        if p.startswith('snare'):
            return 'aro' if a in ('sidestick', 'tsidestick', 'rc') else 'caja'
        if p.startswith('tom'):
            n = int(re.findall(r'\d+', p)[0])
            return 'tom_grave' if n >= 15 else ('tom_medio' if n >= 12 else 'tom_agudo')
        if p.startswith('hihat'):
            return 'hihat_pedal' if a in ('chik', 'footspl') else ('hihat_abierto' if a in ('ho', 'open', 'qo', 'ho_brush', 'open_brush') else 'hihat_cerrado')
        if p.startswith(('crash', 'stack', 'ride', 'china')) and a in cym_skip:
            return None
        if p.startswith(('crash', 'stack')):
            return 'crash' if a != 'rd' else 'ride'
        if p.startswith('ride'):
            return 'ride'
        if p.startswith('china'):
            return 'platillo_fx'
        return None
    def name(gd):
        p, a = gd['piece'], gd['art']
        n = re.findall(r'\d+', p)
        base = {'kick': 'Bombo', 'snare': 'Caja', 'tom': 'Tom', 'hihat': 'Hi-hat', 'crash': 'Crash', 'ride': 'Ride', 'china': 'China', 'stack': 'Stack'}
        k = re.match(r'[a-z]+', p).group(0)
        extra = ' sizzle' if 'sizzle' in p else (' sin apagar' if 'nodamp' in p else '')
        arts = {'kick': '', 'cn': '', 'cr': '', 'rd': '', 'ed': 'borde', 'bl': 'campana', 'center': 'centro', 'tcenter': 'alta centro', 'pcenter': 'preparada',
                'primshot': 'preparada rimshot', 'trimshot': 'alta rimshot', 'tsidestick': 'alta baqueta cruzada', 'bdig': 'parche inferior hundido',
                'sc': 'aro canto', 'tc': 'aro punta', 'rc': 'aro', 'cl': 'cerrado', 'lc': 'semicerrado', 'ho': 'medio abierto', 'qo': 'un cuarto',
                'cl_s': 'cerrado canto', 'lc_s': 'semicerrado canto', 'cl_brush': 'cerrado escobilla', 'lc_brush': 'semicerrado escobilla',
                'ho_brush': 'medio abierto escobilla', 'open_brush': 'abierto escobilla', 'perc': 'golpe', 'mid': 'medio'}
        return ('Big Rusty %s %s%s %s' % (base.get(k, k), n[0] if n else '', extra, arts.get(a, human(a)))).replace('  ', ' ').strip()
    out = scan(L, r'Samples/(?P<piece>[a-z]+(?:_sizzle)?_\d+(?:_nodamp|_layer)?)/(?P<art>[a-z_]+)/(?P<mic>[a-z]+)/[a-z0-9_]+?(?:_vl(?P<vl>\d+))?_rr(?P<rr>\d+)\.flac',
               cat, mics, name, pack='Big Rusty', src='bigrusty', genres=ACOUSTIC + ['metal', 'punk'])
    return L, out

# ------------------------------------------------------------------ Swirly Drums (CC0)
def swirly():
    L = 'karoryfer.swirly-drums'
    def cat(gd):
        i = gd['inst']
        if i in ('mech_noises', 'snare_flutter', 'snare_stir'):
            return None
        m = {'cajon_kick': 'bombo', 'marching_kick': 'bombo', 'kick_buzz': 'bombo', 'china': 'platillo_fx', 'splash': 'platillo_fx',
             'splash_broken': 'platillo_fx', 'cowbell': 'cencerro', 'crash': 'crash', 'crash_broken': 'crash', 'darbouka': 'darbuka',
             'djembe': 'djembe', 'hat_closed': 'hihat_cerrado', 'hat_tight': 'hihat_cerrado', 'hat_foot': 'hihat_pedal',
             'hat_footsplash': 'hihat_pedal', 'hat_half': 'hihat_abierto', 'hat_open': 'hihat_abierto', 'hat_quarter': 'hihat_abierto',
             'hat_sizzling': 'hihat_abierto', 'hbongo': 'bongo', 'lbongo': 'bongo', 'ride': 'ride', 'snare_dig': 'caja', 'snare_edge': 'caja',
             'snare_main': 'caja', 'tom_floor': 'tom_grave', 'tom_mlow': 'tom_medio', 'tom_xmed': 'tom_medio', 'tom_mhi': 'tom_agudo', 'tom_vhi': 'tom_agudo'}
        return m.get(i)
    def mics(gd):
        i = gd['inst']
        if i == 'cajon_kick':
            return {'front': (1.0, 0.0), 'rear': (0.7, 0.0)}
        if i == 'marching_kick':
            return {'beater': (1.0, 0.0), 'reso': (0.8, 0.0)}
        if i.startswith('snare'):
            return {'top': (1.0, 0.0), 'btm': (0.5, 0.0)}
        return {None: (1.0, 0.0)}
    names = {'cajon_kick': 'Bombo de cajón', 'marching_kick': 'Bombo de marcha', 'kick_buzz': 'Bombo con zumbido de caja', 'china': 'China',
             'splash': 'Splash', 'splash_broken': 'Splash roto', 'cowbell': 'Cencerro', 'crash': 'Crash', 'crash_broken': 'Crash roto',
             'darbouka': 'Darbuka', 'djembe': 'Djembé', 'hat_closed': 'Hi-hat cerrado', 'hat_tight': 'Hi-hat apretado', 'hat_foot': 'Hi-hat de pie',
             'hat_footsplash': 'Hi-hat de pie salpicado', 'hat_half': 'Hi-hat medio abierto', 'hat_open': 'Hi-hat abierto',
             'hat_quarter': 'Hi-hat un cuarto', 'hat_sizzling': 'Hi-hat sizzle', 'hbongo': 'Bongó agudo', 'lbongo': 'Bongó grave', 'ride': 'Ride',
             'snare_dig': 'Caja hundida', 'snare_edge': 'Caja borde', 'snare_main': 'Caja', 'tom_floor': 'Tom de piso', 'tom_mlow': 'Tom medio grave',
             'tom_xmed': 'Tom medio', 'tom_mhi': 'Tom medio agudo', 'tom_vhi': 'Tom muy agudo'}
    out = scan(L, r'Samples/(?P<inst>[a-z_]+)/[a-z_]+?_vl(?P<vl>\d+)_rr(?P<rr>\d+)(?:_(?P<mic>front|rear|beater|reso|top|btm))?\.wav',
               cat, mics, lambda gd: 'Swirly ' + names[gd['inst']], pack='Swirly', src='swirly', genres=ACOUSTIC + ['world'])
    return L, out

# ------------------------------------------------------------------ The Hat With The Phat (CC0): hi-hat de 20"
def hat_phat():
    L = 'Karoryfer.TheHatWithThePhat'
    def cat(gd):
        a = gd['art']
        if a.startswith(('raise', 'return')):
            return None
        if a.startswith(('chik', 'splash', 'drop')):
            return 'hihat_pedal'
        if a.startswith(('closed', 'xtight', 'squash')):
            return 'hihat_cerrado'
        return 'hihat_abierto'
    out = scan(L, r'Samples/[a-z]+/hat_20_(?P<art>[a-z_]+?)(?:_vl(?P<vl>\d+))?_rr(?P<rr>\d+)(?:_(?P<mic>down))?\.wav',
               cat, lambda gd: {None: (1.0, 0.0), 'down': (0.6, 0.0)}, lambda gd: 'Hat With The Phat ' + human(gd['art']),
               pack='Hat Phat', src='hatphat', genres=ACOUSTIC)
    return L, out

# ------------------------------------------------------------------ Body Percussion (CC0)
def body():
    L = 'body_percussion'
    names = {'bellycenter': 'Panza al centro', 'bellyside': 'Panza de lado', 'cheek': 'Mejilla', 'chestside': 'Pecho de lado', 'chestthump': 'Golpe de pecho',
             'fingerclap': 'Palmada de dedos', 'halfstomp': 'Medio pisotón', 'handclap': 'Palmada', 'head': 'Cabeza', 'heel': 'Talón',
             'pinkysnap': 'Chasquido meñique', 'shoulderout': 'Hombro', 'snap': 'Chasquido', 'stomp': 'Pisotón'}
    def cat(gd):
        a = gd['art']
        if a in ('handclap', 'fingerclap'):
            return 'palmas'
        if a in ('snap', 'pinkysnap'):
            return 'chasquido'
        if a in ('stomp', 'halfstomp', 'chestthump'):
            return 'bombo'
        return 'cuerpo'
    out = scan(L, r'Samples/body/(?P<art>[a-z]+)(?:_(?P<side>l|r|lin|lout|rin|rout))?(?:_vl(?P<vl>\d+))?_rr(?P<rr>\d+)\.wav',
               cat, None, lambda gd: 'Cuerpo ' + names[gd['art']] + {'l': ' izq.', 'r': ' der.', None: ''}.get(gd.get('side'), ' ' + (gd.get('side') or '')),
               pack='Body', src='body', genres=['pop', 'folk', 'hiphop', 'rnb', 'indie'], tags=['orgánico'],
               skip=lambda gd: gd.get('side') in ('r', 'rin', 'rout'))
    return L, out

# ------------------------------------------------------------------ Gogodze Phu (CC0)
def gogodze():
    L1 = 'karoryfer.gogodze-phu-vol-i'
    out1 = scan(L1, r'Samples/bobobo/bobobo_(?P<art>bass|tenor_[ls]|treble_[ls])(?P<n>\d?)_vl(?P<vl>\d+)_rr(?P<rr>\d+)\.wav',
                lambda gd: 'tambor', None,
                lambda gd: 'Bobobo ' + {'bass': 'bajo', 'tenor_l': 'tenor abierto', 'tenor_s': 'tenor seco', 'treble_l': 'agudo abierto', 'treble_s': 'agudo seco'}[gd['art']] + (' ' + gd['n'] if gd.get('n') else ''),
                pack='Gogodze Bobobo', src='gogodze1', genres=['afro', 'world', 'house', 'afrobeat'])
    out1 += scan(L1, r'Samples/cajon/(?P<mic>cnd|dyn)/cajon_(?P<art>s[lr]\d)_(?:vl(?P<vl>\d+)_)?rr(?P<rr>\d+)_(?P=mic)\.wav',
                 lambda gd: 'cajon', {'cnd': (1.0, 0.0), 'dyn': (0.8, 0.0)},
                 lambda gd: 'Cajón ' + {'sl': 'golpe', 'sr': 'lateral'}[gd['art'][:2]] + ' ' + gd['art'][2:], pack='Gogodze Cajon', src='gogodze1',
                 genres=['flamenco', 'acustico', 'pop', 'latino', 'folk'])
    L2 = 'karoryfer.gogodze-phu-vol-ii'
    codes = {'hc': ('hihat_cerrado', 'Hi-hat cerrado'), 'ht': ('hihat_cerrado', 'Hi-hat apretado'), 'hf': ('hihat_pedal', 'Hi-hat de pie'),
             'hh': ('hihat_abierto', 'Hi-hat medio'), 'hl': ('hihat_abierto', 'Hi-hat suelto'), 'ho': ('hihat_abierto', 'Hi-hat abierto'),
             'ks': ('bombo', 'Bombo'), 'sc': ('caja', 'Caja centro'), 'se': ('caja', 'Caja borde'), 'ss': ('aro', 'Caja baqueta cruzada'),
             'th': ('tom_agudo', 'Tom agudo'), 'tm': ('tom_medio', 'Tom medio'), 'tl': ('tom_grave', 'Tom grave')}
    def mics(gd):
        c = gd['art']
        if c == 'ks':
            return {'kick_mic': (1.0, 0.0), 'front_mic': (0.5, 0.0), 'oh_mic': (0.4, 0.0)}
        if c[0] in 'st':
            return {'side_mic': (1.0, 0.0), 'front_mic': (0.6, 0.0), 'oh_mic': (0.45, 0.0)}
        return {'oh_mic': (1.0, 0.0), 'front_mic': (0.5, 0.0)}
    out2 = scan(L2, r'Samples/(?P<mic>[a-z]+_mic)/(?P<art>[a-z]{2})_vl(?P<vl>\d+)_rr(?P<rr>\d+)\.wav', lambda gd: codes[gd['art']][0], mics,
                lambda gd: 'Gogodze 73 ' + codes[gd['art']][1], pack='Gogodze 73', src='gogodze2', genres=['soul', 'funk', 'psicodelia', 'rock', 'hiphop', 'boombap'], tags=['vintage'])
    out2 += scan(L2, r'Samples/(?P<mic>retro_mic)/(?P<art>[a-z]{2})_vl(?P<vl>\d+)_rr(?P<rr>\d+)\.wav', lambda gd: codes[gd['art']][0],
                 {'retro_mic': (1.0, 0.0)}, lambda gd: 'Gogodze Retro ' + codes[gd['art']][1], pack='Gogodze Retro', src='gogodze2',
                 genres=['lofi', 'boombap', 'hiphop', 'soul'], tags=['vintage', 'lo-fi'], nvel=3, nrr=3)
    return [(L1, out1), (L2, out2)]

# ------------------------------------------------------------------ Dim Cabasa (CC-BY 4.0)
def dim_cabasa():
    L = 'kinwie.dim-cabasa'
    return L, scan(L, r'Dim Cabasa Samples/(?P<art>bw|fw)_rr(?P<rr>\d+)_(?P<vl>\d+)\.flac', lambda gd: 'cabasa', None,
                   lambda gd: 'Dim Cabasa ' + {'bw': 'hacia atrás', 'fw': 'hacia adelante'}[gd['art']], pack='Dim Cabasa', src='dimcabasa',
                   genres=['latino', 'bossa', 'pop', 'house', 'afro'])

# ------------------------------------------------------------------ DrumGizmo (CC-BY 4.0): Muldjord y DRS
def drumgizmo(L, kitdir, pack, src, spec, genres):
    """spec: instrumento -> (cat, nombre, {mic: (ganancia, pan)}). Las capas se ordenan por nivel medido."""
    rx = re.compile(r'%s/Samples/(?P<inst>[^/]+)/(?P<hit>\d+)-(?P=inst)-(?P<mic>[^/]+)\.(?:flac|wav)' % re.escape(kitdir))
    hits = {}
    for p in tree(L):
        m = rx.fullmatch(p)
        if m and m.group('inst') in spec:
            hits.setdefault(m.group('inst'), {}).setdefault(int(m.group('hit')), {})[m.group('mic')] = p
    out = []
    for inst, hs in sorted(hits.items()):
        cat, name, mics = spec[inst]
        members = {}
        for h, mm in hs.items():
            parts = [(mm[m], g, pan) for m, (g, pan) in mics.items() if m in mm]
            if parts:
                members[(h, 1)] = parts
        i = Inst(pack, src, inst, name, cat, members, genres=genres)
        i.measure = sorted(mics, key=lambda m: -mics[m][0])[0]    # micrófono para medir el nivel de cada golpe
        out.append(i)
    return out

# el parche de abajo de la caja va con la polaridad invertida, como en el SFZ original (phase=invert)
MULDJORD = {
    'KdrumL': ('bombo', 'Muldjord Bombo', {'KdrumL': (1.0, 0.0), 'KdrumR': (0.6, 0.0), 'OHL': (0.4, -1.0), 'OHR': (0.4, 1.0), 'AmbL': (0.3, -1.0), 'AmbR': (0.3, 1.0)}),
    'Snare': ('caja', 'Muldjord Caja', {'Snare_top': (1.0, 0.0), 'Snare_bottom': (-0.45, 0.0), 'OHL': (0.5, -1.0), 'OHR': (0.5, 1.0), 'AmbL': (0.3, -1.0), 'AmbR': (0.3, 1.0)}),
    'SnareRest': ('caja', 'Muldjord Caja apagada', {'Snare_top': (1.0, 0.0), 'Snare_bottom': (-0.4, 0.0), 'OHL': (0.45, -1.0), 'OHR': (0.45, 1.0)}),
    'Tom1': ('tom_agudo', 'Muldjord Tom 1', {'Tom1': (1.0, 0.25), 'OHL': (0.5, -1.0), 'OHR': (0.5, 1.0), 'AmbL': (0.3, -1.0), 'AmbR': (0.3, 1.0)}),
    'Tom2': ('tom_medio', 'Muldjord Tom 2', {'Tom2': (1.0, 0.0), 'OHL': (0.5, -1.0), 'OHR': (0.5, 1.0), 'AmbL': (0.3, -1.0), 'AmbR': (0.3, 1.0)}),
    'Tom3': ('tom_medio', 'Muldjord Tom 3', {'Tom3': (1.0, -0.15), 'OHL': (0.5, -1.0), 'OHR': (0.5, 1.0), 'AmbL': (0.3, -1.0), 'AmbR': (0.3, 1.0)}),
    'Tom4': ('tom_grave', 'Muldjord Tom de piso', {'Tom4': (1.0, -0.3), 'OHL': (0.5, -1.0), 'OHR': (0.5, 1.0), 'AmbL': (0.3, -1.0), 'AmbR': (0.3, 1.0)}),
    'HihatClosed': ('hihat_cerrado', 'Muldjord Hi-hat cerrado', {'Hihat': (1.0, 0.3), 'OHL': (0.6, -1.0), 'OHR': (0.6, 1.0)}),
    'HihatOpen': ('hihat_abierto', 'Muldjord Hi-hat abierto', {'Hihat': (1.0, 0.3), 'OHL': (0.6, -1.0), 'OHR': (0.6, 1.0)}),
    'RideL': ('ride', 'Muldjord Ride', {'OHL': (1.0, -1.0), 'OHR': (1.0, 1.0), 'RideL': (0.5, -0.4), 'RideR': (0.5, 0.4)}),
    'RideLBell': ('ride', 'Muldjord Ride campana', {'OHL': (1.0, -1.0), 'OHR': (1.0, 1.0), 'RideL': (0.5, -0.4), 'RideR': (0.5, 0.4)}),
    'CrashL': ('crash', 'Muldjord Crash izquierdo', {'OHL': (1.0, -1.0), 'OHR': (1.0, 1.0), 'AmbL': (0.35, -1.0), 'AmbR': (0.35, 1.0)}),
    'CrashR': ('crash', 'Muldjord Crash derecho', {'OHL': (1.0, -1.0), 'OHR': (1.0, 1.0), 'AmbL': (0.35, -1.0), 'AmbR': (0.35, 1.0)}),
    'China': ('platillo_fx', 'Muldjord China', {'OHL': (1.0, -1.0), 'OHR': (1.0, 1.0), 'AmbL': (0.35, -1.0), 'AmbR': (0.35, 1.0)}),
}

def muldjord():
    L = 'DrumGizmo.MuldjordKit'
    return L, drumgizmo(L, 'DrumGizmo/MuldjordKit', 'Muldjord', 'muldjord', MULDJORD, ['rock', 'metal', 'pop', 'punk', 'indie'])
