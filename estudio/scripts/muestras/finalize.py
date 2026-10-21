#!/usr/bin/env python3
# Biblioteca Racoon: arma catalogo.json, ondas.json, kits, CREDITOS.txt y LICENCIAS a partir de lo procesado
import os, sys, json, re, shutil, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from model import CATS
import src_extra  # registra las categorías extra

ROOT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.getcwd(), 'Muestras Racoon')
EXCLUDE_SRC = {'jrhodes'}   # muestras con licencia no comercial: fuera

CC0, CCBY4, CCBY3, PD, UNL, GPL = 'CC0 1.0', 'CC BY 4.0', 'CC BY 3.0', 'Dominio público', 'The Unlicense', 'GPL 2.0 o posterior'
SF = 'https://github.com/sfzinstruments/'
FUENTES = {
    'virtuosity': ('Virtuosity Drums', 'Versilian Studios (Sam Gossner)', CC0, SF + 'virtuosity_drums'),
    'frankensnare': ('Frankensnare', 'Karoryfer Lecolds', CC0, SF + 'karoryfer.frankensnare'),
    'unruly': ('Unruly Drums', 'Karoryfer Lecolds', CC0, SF + 'karoryfer.unruly-drums'),
    'bigrusty': ('Big Rusty Drums', 'Karoryfer Lecolds', CC0, SF + 'karoryfer.big-rusty-drums'),
    'swirly': ('Swirly Drums', 'Karoryfer Lecolds', CC0, SF + 'karoryfer.swirly-drums'),
    'hatphat': ('The Hat With The Phat', 'Karoryfer Lecolds', CC0, SF + 'Karoryfer.TheHatWithThePhat'),
    'body': ('Body Percussion', 'sfzinstruments', CC0, SF + 'body_percussion'),
    'gogodze1': ('Gogodze Phu Vol I', 'Karoryfer Lecolds', CC0, SF + 'karoryfer.gogodze-phu-vol-i'),
    'gogodze2': ('Gogodze Phu Vol II', 'Karoryfer Lecolds', CC0, SF + 'karoryfer.gogodze-phu-vol-ii'),
    'dimcabasa': ('Dim Cabasa', 'kinwie', CCBY4, SF + 'kinwie.dim-cabasa'),
    'muldjord': ('MuldjordKit', 'Lars Muldjord (DrumGizmo), mapeo de kinwie', CCBY4, SF + 'DrumGizmo.MuldjordKit'),
    'drs': ('DRSKit 1.0', 'Lars Muldjord, Bent Bisballe Nyeng y Jes Eiler (DrumGizmo / DRSDrums)', CCBY4, SF + 'DrumGizmo.DRSKit'),
    'tr808': ('Roland TR-808, juego de 1994', 'Michael Fischer (Technopolis)', CC0, 'https://github.com/tidalcycles/sounds-tr808-fischer'),
    'tr707': ('Roland TR-707', 'Francois Dion', PD, 'https://github.com/fluid-music/open-drums'),
    'cr78': ('Roland CR-78', 'boxedear.com, vía oramics/sampled', PD, 'https://github.com/oramics/sampled'),
    'linndrum': ('LinnDrum LM-2', 'machines.hyperreal.org, vía oramics/sampled', PD, 'https://github.com/oramics/sampled'),
    'mrk2': ('Maestro Rhythm King MRK-2', 'oramics/sampled', PD, 'https://github.com/oramics/sampled'),
    'tr505': ('Roland TR-505', 'oramics/sampled', PD, 'https://github.com/oramics/sampled'),
    'pearl': ('Pearl Master Studio Pack 1', 'enoe, vía oramics/sampled', CCBY3, 'https://github.com/oramics/sampled'),
    'smpldsnds': ('Drum Machines (RZ-1, SK-1, MFB-512, CR-8000, Drumtraks, MR-10)', 'smpldsnds', PD, 'https://github.com/smpldsnds/drum-machines'),
    'hydrogen_gmrock': ('GMRockKit de Hydrogen', 'Glen MacArthur y Sebastian Moors', GPL, 'https://github.com/hydrogen-music/hydrogen'),
    'hydrogen_808': ('TR808EmulationKit de Hydrogen', 'ArtemioLabs', GPL, 'https://github.com/hydrogen-music/hydrogen'),
    'sonicpi': ('Muestras de Sonic Pi', 'Sam Aaron y colaboradores de freesound.org, Arovane, The Black Dog', CC0, 'https://github.com/sonic-pi-net/sonic-pi'),
    'vcsl': ('Versilian Community Sample Library', 'Versilian Studios (Sam Gossner)', CC0, 'https://github.com/sgossner/VCSL'),
    'vsco': ('VSCO 2 Community Edition', 'Versilian Studios (Sam Gossner)', CC0, 'https://github.com/sgossner/VSCO-2-CE'),
    'salamander': ('Salamander Grand Piano', 'Alexander Holm, mapeo de kinwie', CCBY3, SF + 'SalamanderGrandPiano'),
    'splendid': ('Splendid Grand Piano', 'Akai (liberado en 2000), mapeo de kinwie', PD, 'https://github.com/smpldsnds/sfzinstruments-splendid-grand-piano'),
    'freepats_oldpiano': ('Old Piano (Francis Bacon)', 'Piotr Barcz, armado por FreePats', CC0, 'https://github.com/smpldsnds/freepats-old-piano-fb'),
    'sneakybass': ('Sneaky Bass', 'Karoryfer Lecolds', CC0, SF + 'karoryfer.sneakybass'),
    'biglittlebass': ('Big Little Bass', 'Karoryfer Lecolds', CC0, SF + 'karoryfer.big-little-bass'),
    'blackblue': ('Black and Blue Basses', 'Karoryfer Lecolds', CC0, SF + 'karoryfer.black-and-blue-basses'),
    'pastabass': ('Pasta Bass', 'Karoryfer Lecolds', CC0, SF + 'karoryfer.pastabass'),
    'swagbass': ('Swag Bass', 'Karoryfer Lecolds', CC0, SF + 'karoryfer.swagbass'),
    'fashionbass': ('Fashion Bass', 'Karoryfer Lecolds', CC0, SF + 'karoryfer.fashionbass'),
    'horsepulse': ('Horse Pulse', 'Karoryfer Lecolds', CC0, SF + 'Karoryfer.HorsePulse'),
    'growlybass': ('Growly Bass', 'Karoryfer Lecolds', CC0, SF + 'karoryfer.growlybass'),
    'dsmolken': ('Double Bass', 'D. Smolken', CC0, SF + 'dsmolken.double-bass'),
    'meatbass': ('Meat Bass', 'Karoryfer Lecolds', CC0, SF + 'karoryfer.meatbass'),
    'emilyguitar': ('Emily Guitar', 'Karoryfer Lecolds', CC0, SF + 'karoryfer.emilyguitar'),
    'shinyguitar': ('Shiny Guitar', 'Karoryfer Lecolds', CC0, SF + 'karoryfer.shinyguitar'),
    'blackgreen': ('Black and Green Guitars', 'Karoryfer Lecolds', CC0, SF + 'karoryfer.black-and-green-guitars'),
    'zither': ('Hungarian Zither', 'sfzinstruments', CC0, SF + 'hungarian_zither'),
    'bigcatcello': ('Big Cat Cello', 'Karoryfer Lecolds', CC0, SF + 'karoryfer-bigcat.cello'),
    'wartuba': ('War Tuba', 'Karoryfer Lecolds', CC0, SF + 'karoryfer.war-tuba'),
    'squidpipes': ('Squid Pipes', 'Karoryfer Lecolds', CC0, SF + 'karoryfer.squidpipes'),
    'weresax': ('Weresax', 'Karoryfer Lecolds', CC0, SF + 'karoryfer.weresax'),
    'bearsax': ('Bear Sax', 'Karoryfer Lecolds', CC0, SF + 'karoryfer.bear-sax'),
    'steeldrum': ('jSteelDrum', 'Jeff Learman', UNL, SF + 'jlearman.SteelDrum'),
    'stringcyborgs': ('String Cyborgs', 'Karoryfer Lecolds', CC0, SF + 'karoryfer.string-cyborgs'),
    'caveman': ('Caveman Cosmonaut', 'Karoryfer Lecolds', CC0, SF + 'karoryfer.caveman-cosmonaut'),
    'scarypiano': ('Scarypiano', 'Karoryfer Lecolds, sobre el piano de dominio público de la Universidad de Iowa', CC0, SF + 'karoryfer.scarypiano'),
    'merryorks': ('272 Merry Orks', 'Karoryfer Lecolds', CC0, SF + 'karoryfer.272-merry-orks'),
    'boochi': ('Free Drum Samples (Trap Duro, Bounce, Soul Vintage)', 'Boochi44, en parte a partir de la 808 grabada por Edward Loveall', CC0, 'https://github.com/Boochi44/free-drum-samples'),
    'akwf': ('Adventure Kid Waveforms (AKWF)', 'Kristoffer Ekstrand (Adventure Kid), vía Sonic Pi', CC0, 'https://github.com/sonic-pi-net/sonic-pi'),
    'waveedit': ('WaveEdit Online', 'usuarios de WaveEdit (Synthesis Technology)', CC0, 'https://github.com/smpldsnds/wavedit-online'),
}

def load_entries():
    E = json.load(open(os.path.join(ROOT, '.entradas.json')))
    keep = []
    for e in E:
        if e['fuente'] in EXCLUDE_SRC:
            p = os.path.join(ROOT, e['archivo'])
            if e.get('tipo') == 'multimuestra':
                shutil.rmtree(p[:-4], ignore_errors=True)
            if os.path.exists(p):
                os.remove(p)
            continue
        if not os.path.exists(os.path.join(ROOT, e['archivo'])):
            continue
        keep.append(e)
    return keep

# ------------------------------------------------------------------ kits para Hamster (16 pistas, mismo orden que las fábricas)
SLOTS = [('Bombo', ['bombo']), ('Bombo 2', ['bombo', 'bajo_808', 'tom_grave']), ('Caja', ['caja']), ('Palmas', ['palmas', 'chasquido', 'aro', 'caja']),
         ('Aro', ['aro', 'clave', 'bloque']), ('Hi-hat cerrado', ['hihat_cerrado', 'hihat_pedal']), ('Hi-hat abierto', ['hihat_abierto']),
         ('Ride', ['ride', 'crash']), ('Crash', ['crash', 'platillo_fx', 'ride']), ('Shaker', ['shaker', 'pandereta', 'cabasa', 'cascabeles', 'guiro']),
         ('Perc 1', ['conga', 'tom_grave', 'djembe', 'tambor']), ('Perc 2', ['bongo', 'tom_medio', 'darbuka', 'tabla']),
         ('Perc 3', ['tom_agudo', 'timbal', 'bongo', 'tom_medio']), ('Campana', ['cencerro', 'agogo', 'campana']),
         ('Metal', ['triangulo', 'campana', 'metal', 'clave']), ('Efecto', ['platillo_fx', 'efecto', 'voz', 'percusion'])]
HAMSTER_ROLE = {'bombo': 'Kick', 'bajo_808': 'Kick', 'caja': 'Snare', 'palmas': 'Clap', 'chasquido': 'Clap', 'aro': 'Rim', 'clave': 'Rim', 'bloque': 'Rim',
                'hihat_cerrado': 'Closed Hat', 'hihat_pedal': 'Closed Hat', 'hihat_abierto': 'Open Hat', 'ride': 'Ride', 'crash': 'Crash',
                'platillo_fx': 'Crash', 'shaker': 'Shaker', 'pandereta': 'Shaker', 'cabasa': 'Shaker', 'cascabeles': 'Shaker', 'guiro': 'Shaker',
                'conga': 'Conga', 'djembe': 'Conga', 'tambor': 'Conga', 'cajon': 'Conga', 'bongo': 'Bongo', 'darbuka': 'Bongo', 'tabla': 'Bongo',
                'timbal': 'Bongo', 'tom_grave': 'Tom Low', 'tom_medio': 'Tom Mid', 'tom_agudo': 'Tom High', 'cencerro': 'Cowbell', 'agogo': 'Cowbell',
                'campana': 'Metal Perc', 'triangulo': 'Metal Perc', 'metal': 'Metal Perc', 'efecto': 'Custom', 'voz': 'Custom', 'percusion': 'Custom',
                'cuerpo': 'Custom', 'orquesta': 'Custom'}
AVOID = ('escobilla', 'maza', 'preparad', 'parche inferior', 'apagad', 'sin bordonero', 'hundid', 'aro canto', 'aro punta', 'flam', 'golpe de baqueta',
         'sizzle', 'roto', 'cadena', 'muy sucio', 'campana', 'semiabierto', 'zumbido', 'contacto', 'roll', 'redoble', 'nosnare', 'antigua')

def score(g, hints):
    n = g['nombre'].lower()
    s = g['capas'] * 2 + min(g['rr'], 4)
    for a in AVOID:
        if a in n and not any(a in h for h in hints):
            s -= 6
    for h in hints:
        if h in n:
            s += 10
    if re.search(r'\b(centro|cerrado|abierto|punta)\b', n):
        s += 2
    return s

def build_groups(E):
    G = collections.OrderedDict()
    for e in E:
        if e.get('tipo') == 'multimuestra' or e['cat'] in ('ciclo', 'tabla_ondas'):
            continue
        g = G.setdefault(e['grupo'], dict(id=e['grupo'], nombre=e['nombre'], cat=e['cat'], pack=e['pack'], fuente=e['fuente'], capas=0, rr=0,
                                          generos=e.get('generos', []), n=0))
        g['capas'] = max(g['capas'], e['capa'])
        g['rr'] = max(g['rr'], e['rr'])
        g['n'] += 1
    return G

def make_kit(G, name, packs, genres, bpm, hints=None, fallback=('VCSL', 'Virtuosity Perc', 'Sonic Pi'), override=None):
    hints = hints or {}
    used, tracks = set(), []
    for i, (slot, cats) in enumerate(SLOTS):
        cand = None
        ov = (override or {}).get(slot)
        if ov:
            pk, cats2, h2 = ov
            pool = [g for g in G.values() if g['pack'] in pk and g['cat'] in cats2 and g['id'] not in used]
            if pool:
                cand = max(pool, key=lambda g: score(g, h2))
        for c in ([] if cand else cats):
            pool = [g for g in G.values() if g['pack'] in packs and g['cat'] == c and g['id'] not in used]
            if not pool:
                continue
            cand = max(pool, key=lambda g: score(g, hints.get(slot, [])))
            break
        if cand is None:
            for c in cats:
                pool = [g for g in G.values() if g['pack'] in fallback and g['cat'] == c and g['id'] not in used]
                if pool:
                    cand = max(pool, key=lambda g: score(g, hints.get(slot, [])))
                    break
        if cand is None:
            continue
        used.add(cand['id'])
        tracks.append(dict(pista=i, nombre=slot, rol=HAMSTER_ROLE.get(cand['cat'], 'Custom'), grupo=cand['id']))
    return dict(id=re.sub(r'[^a-z0-9]+', '-', name.lower()).strip('-'), nombre=name, generos=genres, bpm=bpm, pistas=tracks)

def kits(G):
    K = []
    acoustic = [('Virtuosity', ['Virtuosity', 'Virtuosity Perc'], ['pop', 'rock', 'funk'], 100),
                ('Muldjord Rock', ['Muldjord'], ['rock', 'metal', 'punk'], 140), ('DRS Metal', ['DRS'], ['metal', 'rock'], 150),
                ('Big Rusty', ['Big Rusty'], ['rock', 'indie', 'blues'], 96), ('Unruly', ['Unruly'], ['indie', 'rock', 'funk'], 105),
                ('Swirly', ['Swirly'], ['world', 'indie', 'pop'], 110), ('Gogodze 73', ['Gogodze 73'], ['soul', 'funk', 'psicodelia'], 92),
                ('GM Rock', ['GM Rock'], ['rock', 'pop'], 120), ('Pearl Master', ['Pearl Master'], ['pop', 'rock'], 110)]
    for nm, packs, gen, bpm in acoustic:
        K.append(make_kit(G, nm, packs, gen, bpm))
    machines = [('TR-808', ['TR-808'], ['hiphop', 'trap', 'miami bass', 'electro'], 90), ('TR-707', ['TR-707'], ['italo', 'house', 'freestyle'], 120),
                ('CR-78', ['CR-78'], ['disco', 'synthpop', 'lofi'], 112), ('LinnDrum', ['LinnDrum'], ['synthpop', 'pop', 'funk'], 110),
                ('TR-505', ['TR-505'], ['house', 'pop'], 122), ('Rhythm King', ['Rhythm King'], ['soul', 'funk', 'lofi'], 96),
                ('Casio RZ-1', ['Casio RZ-1'], ['hiphop', 'boombap'], 92), ('CR-8000', ['CR-8000'], ['disco', 'italo'], 118),
                ('Drumtraks', ['Drumtraks'], ['synthpop', 'electro'], 115), ('Yamaha MR-10', ['Yamaha MR-10'], ['lofi', 'chiptune'], 100),
                ('MFB-512', ['MFB-512'], ['techno', 'ebm'], 128), ('808 Emulado', ['808 Emulado'], ['hiphop', 'electro'], 100),
                ('Sonic Pi Electrónico', ['Sonic Pi'], ['techno', 'idm', 'electro'], 126)]
    for nm, packs, gen, bpm in machines:
        K.append(make_kit(G, nm, packs, gen, bpm, fallback=('Sonic Pi', 'VCSL', 'TR-808')))
    # por género: mezclas a mano de varias bibliotecas
    K.append(make_kit(G, 'Trap 808', ['TR-808', '808 Emulado'], ['trap', 'drill', 'hiphop'], 140,
                      hints={'Bombo': ['tono 2.5 caída 5'], 'Bombo 2': ['caída 10'], 'Hi-hat cerrado': ['cerrado']},
                      override={'Bombo 2': (['TR-808'], ['bombo'], ['caída 10', 'tono 0']), 'Palmas': (['TR-808', 'Body'], ['palmas'], []),
                                'Efecto': (['Merry Orks'], ['voz'], ['hey', 'ey'])}))
    K.append(make_kit(G, 'Reggaetón y dembow', ['TR-808', 'Virtuosity Perc', 'VCSL'], ['reggaeton', 'dembow', 'latino'], 92,
                      hints={'Bombo': ['caída 5'], 'Caja': ['brillo 5']},
                      override={'Perc 3': (['Virtuosity Perc'], ['timbal'], ['14']), 'Aro': (['TR-808'], ['aro'], []),
                                'Caja': (['TR-808'], ['caja'], ['tono 5 brillo 5']), 'Hi-hat cerrado': (['TR-808'], ['hihat_cerrado'], []),
                                'Palmas': (['TR-808'], ['palmas'], []),
                                'Shaker': (['Virtuosity Perc', 'VCSL'], ['shaker'], ['chico', 'grande'])}))
    K.append(make_kit(G, 'Trap duro', ['Trap Duro', 'TR-808'], ['trap', 'drill'], 145))
    K.append(make_kit(G, 'Bounce', ['Bounce', 'TR-808'], ['trap', 'jersey club', 'reggaeton'], 140))
    K.append(make_kit(G, 'Soul vintage', ['Soul Vintage', 'Gogodze Retro'], ['lofi', 'soul', 'boombap'], 88))
    K.append(make_kit(G, 'Boom bap', ['Gogodze Retro', 'Virtuosity Lo-Fi', 'Casio RZ-1'], ['boombap', 'hiphop', 'lofi'], 90))
    K.append(make_kit(G, 'Lo-fi', ['Virtuosity Lo-Fi', 'Gogodze Retro', 'Rhythm King', 'Yamaha MR-10'], ['lofi', 'chillhop'], 82,
                      override={'Efecto': (['Sonic Pi'], ['efecto'], ['vinilo'])}))
    K.append(make_kit(G, 'House orgánico', ['Sonic Pi', 'TR-707', 'Virtuosity Perc', 'Gogodze Bobobo', 'Dim Cabasa'], ['house', 'organic house', 'deep house'], 122,
                      override={'Bombo': (['Sonic Pi'], ['bombo'], ['haus', 'klub']), 'Caja': (['Sonic Pi'], ['caja'], ['dolf', 'generic']), 'Shaker': (['Dim Cabasa', 'Virtuosity Perc'], ['cabasa', 'shaker'], []),
                                'Perc 1': (['Gogodze Bobobo'], ['tambor'], ['bajo']), 'Perc 2': (['Gogodze Bobobo'], ['tambor'], ['tenor']),
                                'Perc 3': (['Virtuosity Perc'], ['bongo'], ['agudo'])}))
    K.append(make_kit(G, 'Deep house', ['TR-707', 'Sonic Pi', 'TR-505', 'Body'], ['deep house', 'house'], 120,
                      override={'Palmas': (['Body', 'TR-707'], ['palmas', 'chasquido'], ['palmada']), 'Shaker': (['VCSL'], ['shaker'], ['chico'])}))
    K.append(make_kit(G, 'Techno', ['MFB-512', 'Sonic Pi', 'TR-808'], ['techno', 'industrial'], 130,
                      override={'Bombo': (['Sonic Pi'], ['bombo'], ['tek', 'klub']), 'Efecto': (['Sonic Pi'], ['efecto'], ['industrial', 'glitch'])}))
    K.append(make_kit(G, 'Cumbia', ['Virtuosity Perc', 'VCSL', 'Virtuosity'], ['cumbia', 'latino', 'tropical'], 95,
                      override={'Caja': (['Virtuosity'], ['caja'], ['centro']), 'Shaker': (['Virtuosity Perc', 'VCSL'], ['guiro'], ['rápido']), 'Perc 1': (['Virtuosity Perc', 'VCSL'], ['conga'], ['conga']),
                                'Perc 3': (['Virtuosity Perc'], ['timbal'], [])}))
    K.append(make_kit(G, 'Salsa', ['Virtuosity Perc', 'VCSL'], ['salsa', 'latino', 'son'], 100,
                      override={'Bombo': (['Virtuosity Perc', 'VCSL'], ['conga'], ['tumba']), 'Caja': (['Virtuosity Perc'], ['timbal'], ['13']),
                                'Campana': (['Virtuosity Perc', 'VCSL'], ['cencerro'], [])}))
    K.append(make_kit(G, 'Afro', ['Gogodze Bobobo', 'Swirly', 'VCSL', 'Virtuosity Perc'], ['afro', 'afrobeat', 'amapiano', 'world'], 112))
    K.append(make_kit(G, 'Cajón flamenco', ['Gogodze Cajon', 'Body', 'VCSL'], ['flamenco', 'rumba', 'acustico'], 104,
                      override={'Bombo': (['Gogodze Cajon'], ['cajon'], ['golpe 1']), 'Caja': (['Gogodze Cajon'], ['cajon'], ['lateral']),
                                'Palmas': (['Body'], ['palmas'], ['palmada'])}))
    K.append(make_kit(G, 'Jazz con escobillas', ['Big Rusty', 'Unruly'], ['jazz', 'bossa', 'swing'], 128,
                      hints={'Caja': ['escobilla'], 'Ride': ['escobilla', 'borde'], 'Hi-hat cerrado': ['escobilla'], 'Hi-hat abierto': ['escobilla']}))
    K.append(make_kit(G, 'Synthpop 80', ['LinnDrum', 'Drumtraks', 'TR-707'], ['synthpop', 'new wave', 'synthwave'], 118))
    K.append(make_kit(G, 'Disco', ['CR-78', 'CR-8000', 'Virtuosity'], ['disco', 'nu disco', 'funk'], 120))
    K.append(make_kit(G, 'Orquestal', ['VCSL', 'VSCO 2'], ['cine', 'orquestal', 'epico'], 90,
                      override={'Bombo': (['VCSL', 'VSCO 2'], ['orquesta'], ['bombo']), 'Crash': (['VCSL'], ['crash'], ['choque'])}))
    K.append(make_kit(G, 'Percusión corporal', ['Body', 'Merry Orks'], ['pop', 'folk', 'acapella'], 100))
    K.append(make_kit(G, 'Mundo: tablas y darbuka', ['Sonic Pi', 'Swirly', 'VCSL'], ['world', 'india', 'trip-hop'], 96,
                      override={'Bombo': (['Sonic Pi'], ['tabla'], ['ghe']), 'Caja': (['Sonic Pi'], ['tabla'], ['na', 'te'])}))
    return [k for k in K if len(k['pistas']) >= 8]

def main():
    E = load_entries()
    G = build_groups(E)
    used_src = sorted(set(e['fuente'] for e in E))
    fuentes = {s: dict(nombre=FUENTES[s][0], autor=FUENTES[s][1], licencia=FUENTES[s][2], url=FUENTES[s][3]) for s in used_src}
    cats = {c: dict(nombre=v[0], carpeta=v[1], rol=v[3]) for c, v in CATS.items()}
    from melodic import INST_CATS
    for c, n in INST_CATS.items():
        cats.setdefault(c, dict(nombre=n, carpeta='instrumentos', rol=''))
    strip = lambda e: {k: v for k, v in e.items() if k not in ('sr',)}
    main_entries = [strip(e) for e in E if e.get('tipo') != 'multimuestra' and e['cat'] not in ('ciclo', 'tabla_ondas')]
    insts = [e for e in E if e.get('tipo') == 'multimuestra']
    ondas = [dict(id=e['id'], archivo=e['archivo'], nombre=e['nombre'], grupo=e['etiquetas'][-1] if e['cat'] == 'ciclo' else 'waveedit',
                  ciclo=e.get('ciclo', 600), cuadros=e.get('cuadros', 1), fuente=e['fuente']) for e in E if e['cat'] in ('ciclo', 'tabla_ondas')]
    K = kits(G)
    cat = dict(version=1, nombre='Muestras Racoon', perfil=os.environ.get('RACOON_PERFIL', 'esencial'), fuentes=fuentes, categorias=cats, muestras=main_entries, instrumentos=insts, kits=K)
    json.dump(cat, open(os.path.join(ROOT, 'catalogo.json'), 'w'), ensure_ascii=False, separators=(',', ':'))
    json.dump(dict(version=1, ondas=ondas), open(os.path.join(ROOT, 'ondas.json'), 'w'), ensure_ascii=False, separators=(',', ':'))
    # créditos
    count = collections.Counter(e['fuente'] for e in E)
    lines = ['MUESTRAS RACOON, CRÉDITOS Y LICENCIAS', '',
             'Todas las muestras vienen de bibliotecas gratuitas que permiten redistribuirlas. Se mezclaron micrófonos, se recortaron colas',
             'y se igualaron niveles, pero el sonido es de sus autores. Las licencias CC BY piden dar crédito: este archivo lo da y debe',
             'acompañar a la biblioteca. La GPL de los dos kits de Hydrogen es compatible con la GPL3 de Racoon.', '',
             'Se dejaron fuera, a propósito: jRhodes3d (CC BY-NC), la 909 de Rob Roy (prohíbe modificar), Drum Abuse y SMDrums (sin licencia clara).', '']
    for s in used_src:
        f = fuentes[s]
        lines += ['%s' % f['nombre'], '  autor: %s' % f['autor'], '  licencia: %s' % f['licencia'], '  origen: %s' % f['url'],
                  '  archivos en Racoon: %d' % count[s], '']
    open(os.path.join(ROOT, 'CREDITOS.txt'), 'w').write('\n'.join(lines))
    # textos de licencia
    lic = os.path.join(ROOT, 'LICENCIAS'); os.makedirs(lic, exist_ok=True)
    from core import SRC as srcdir
    for name, repo, path in [('CC0-1.0.txt', 'VCSL', 'LICENSE'), ('CC-BY-4.0.txt', 'DrumGizmo.MuldjordKit', 'LICENSE'),
                             ('CC-BY-3.0.txt', 'SalamanderGrandPiano', 'LICENSE'), ('Unlicense.txt', 'jlearman.SteelDrum', 'LICENSE'),
                             ('GPL-2.0.txt', 'hydrogen', 'COPYING')]:
        dst = os.path.join(lic, name)
        if not os.path.exists(dst):
            import subprocess
            r = subprocess.run(['git', '-C', os.path.join(srcdir, repo), 'show', 'HEAD:' + path], capture_output=True)
            open(dst, 'wb').write(r.stdout)
    n_files = sum(1 for e in main_entries) + sum(e['muestras'] for e in insts) + len(ondas)
    print('catálogo: %d muestras sueltas, %d grupos, %d instrumentos (%d notas), %d ondas, %d kits, %d fuentes; %d archivos de audio' % (
        len(main_entries), len(G), len(insts), sum(e['muestras'] for e in insts), len(ondas), len(K), len(used_src), n_files))
    for k in K:
        print('  kit %-26s %2d pistas' % (k['nombre'], len(k['pistas'])))

if __name__ == '__main__':
    main()
