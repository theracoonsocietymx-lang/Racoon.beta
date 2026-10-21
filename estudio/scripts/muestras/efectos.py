#!/usr/bin/env python3
# Efectos por género, la parte grabada: vinilos, silbatos, sirenas, golpes de metal, láseres, voces de anunciador,
# multitudes, whooshes y ambientes de bibliotecas libres (CC0 y GPL), repartidos en las carpetas de cada género
# (Música/Racoon/Efectos por género/<Género>/<Tipo>/). Racoon sintetiza el resto (risers, rollers, impactos...) en
# las mismas carpetas la primera vez que abre.
#   python3 efectos.py --root "~/Music/Racoon/Efectos por género"
# Lo corre completa.sh dentro del instalador. Necesita lo mismo que build.py (numpy, scipy, soundfile, git, curl).
import os, sys, re, json, argparse
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import core

VERSION = 1
core.REPOS.setdefault('CC0-Public-Domain-Sounds', 'lavenderdotpet/CC0-Public-Domain-Sounds')
core.REPOS.setdefault('lmms', 'LMMS/lmms')

# los géneros de Racoon (Source/engine/Genres.cpp): id -> nombre de la carpeta
GENEROS = {
    'house': 'House', 'deephouse': 'Deep House', 'techhouse': 'Tech House', 'afrohouse': 'Afro House', 'organichouse': 'Organic House',
    'progressive': 'Progressive House', 'nudisco': 'Nu Disco', 'indiedance': 'Indie Dance', 'melodictechno': 'Melodic Techno',
    'techno': 'Techno', 'hardtechno': 'Hard Techno', 'minimal': 'Minimal', 'acid': 'Acid House', 'trance': 'Trance',
    'psytrance': 'Psytrance', 'dnb': 'Drum & Bass', 'liquid': 'Liquid DnB', 'jungle': 'Jungle', 'dubstep': 'Dubstep',
    'futurebass': 'Future Bass', 'trap': 'Trap', 'drill': 'Drill', 'phonk': 'Phonk', 'reggaeton': 'Reggaetón', 'dembow': 'Dembow',
    'moombahton': 'Moombahton', 'cumbia': 'Cumbia', 'afrobeats': 'Afrobeats', 'amapiano': 'Amapiano', 'dancehall': 'Dancehall',
    'hiphop': 'Hip Hop (boom bap)', 'lofi': 'Lo-fi Hip Hop', 'rnb': 'R&B', 'jerseyclub': 'Jersey Club', 'ukgarage': 'UK Garage',
    'breakbeat': 'Breakbeat', 'hardstyle': 'Hardstyle', 'electro': 'Electro', 'synthwave': 'Synthwave', 'disco': 'Disco',
    'funk': 'Funk', 'pop': 'Pop', 'downtempo': 'Downtempo', 'ambient': 'Ambient',
}

LAV = 'CC0-Public-Domain-Sounds'
AUD = r'\.(wav|ogg|flac)$'

# cada grupo: de dónde (repositorio y expresión sobre la ruta), a qué carpeta, con qué nombre, para qué géneros,
# cuántos archivos por género como máximo y cuánto dura cada uno como máximo
GRUPOS = [
    dict(nombre='Vinilo', repo=LAV, patron=r'^Micro Pack - Record Fuzzies/.*\.wav$', tipo='Texturas', max=5, dur=12.0, autor='Ben Burnes (Abstraction)',
         generos='deephouse nudisco minimal liquid jungle phonk hiphop lofi rnb funk downtempo'),
    dict(nombre='Vinilo', repo='sonic-pi', patron=r'^etc/samples/vinyl_hiss\.flac$', tipo='Texturas', max=1, dur=12.0, autor='Sonic Pi (Freesound CC0)',
         generos='lofi hiphop downtempo'),
    dict(nombre='Scratch', repo='sonic-pi', patron=r'^etc/samples/vinyl_(scratch|backspin|rewind)\.flac$', tipo='Incidentales', max=3, dur=6.0, autor='Sonic Pi (Freesound CC0)',
         generos='hiphop lofi breakbeat electro jungle dancehall ukgarage dnb funk phonk'),
    dict(nombre='Scratch', repo='lmms', patron=r'^data/samples/effects/scratch01\.ogg$', tipo='Incidentales', max=1, dur=6.0, autor='LMMS (GPL 2.0 o posterior)',
         generos='hiphop breakbeat electro'),
    dict(nombre='Silbato', repo='VCSL', patron=r'^Aerophones/Edge-blown Aerophones/(Ball Whistle|Train Whistle, Toy)/.*\.wav$', tipo='Incidentales', max=4, dur=6.0, autor='VCSL (Versilian Studios, CC0)',
         generos='afrohouse cumbia afrobeats amapiano disco funk dancehall'),
    dict(nombre='Slide whistle', repo=LAV, patron=r'^bb - Slide Whistle \(Aug 2021\)/.*\.wav$', tipo='Incidentales', max=4, dur=6.0, autor='Ben Burnes (Abstraction)',
         generos='funk disco cumbia afrobeats'),
    dict(nombre='Sirena', repo='VCSL', patron=r'^Aerophones/Free Aerophones/Siren/.*\.wav$', tipo='Incidentales', max=3, dur=8.0, autor='VCSL (Versilian Studios, CC0)',
         generos='dnb jungle reggaeton dembow moombahton dancehall hardtechno ukgarage breakbeat'),
    dict(nombre='Sirena', repo=LAV, patron=r'^MMRetroArcadeSoundsPack1_0_5/Misc/wav/Siren1Loop\.wav$', tipo='Incidentales', max=1, dur=8.0, autor='The Motion Monkey (CC0)',
         generos='dembow reggaeton hardtechno jerseyclub'),
    dict(nombre='Golpe de yunque', repo='VCSL', patron=r'^Idiophones/Struck Idiophones/Anvil/.*_v3_.*\.wav$', tipo='Impactos', max=3, dur=5.0, autor='VCSL (Versilian Studios, CC0)',
         generos='techno hardtechno trap drill dembow dancehall jerseyclub'),
    dict(nombre='Brake drum', repo='VCSL', patron=r'^Idiophones/Struck Idiophones/Brake Drum/.*(Hammer|Susp).*\.wav$', tipo='Impactos', max=3, dur=5.0, autor='VCSL (Versilian Studios, CC0)',
         generos='techno hardtechno trap drill dembow dancehall jerseyclub minimal'),
    dict(nombre='Golpe de metal', repo=LAV, patron=r'^kenney_impactsounds/Audio/impact(Metal|Bell|Plate)_heavy.*\.ogg$', tipo='Impactos', max=3, dur=4.0, autor='Kenney (CC0)',
         generos='techno hardtechno trap drill jerseyclub dembow'),
    dict(nombre='Laser', repo=LAV, patron=r'^kenney_digitalaudio/Audio/(laser|phaser|zap).*\.ogg$', tipo='Incidentales', max=5, dur=3.0, autor='Kenney (CC0)',
         generos='psytrance dubstep dnb electro synthwave acid dembow moombahton cumbia nudisco disco'),
    dict(nombre='Laser', repo=LAV, patron=r'^sci-fi-sounds/Audio/laser.*\.ogg$', tipo='Incidentales', max=4, dur=3.0, autor='Kenney (CC0)',
         generos='psytrance dubstep electro synthwave dnb'),
    dict(nombre='Espacio', repo=LAV, patron=r'^sci-fi-sounds/Audio/(spaceEngine|forceField|computerNoise|thrusterFire).*\.ogg$', tipo='Texturas', max=4, dur=10.0, autor='Kenney (CC0)',
         generos='psytrance melodictechno techno minimal ambient electro synthwave dubstep'),
    dict(nombre='Electromagnetico', repo=LAV, patron=r'^Micro Pack - NazdyNate - Electromagnetic Sounds/.*\.wav$', tipo='Texturas', max=4, dur=10.0, autor='NazdyNate (CC0)',
         generos='techno minimal hardtechno electro ambient'),
    dict(nombre='Whoosh', repo=LAV, patron=r'^Micro Pack - Organic Wooshes/.*(Swish|Twirl|Woosh|Combo).*\.wav$', tipo='Sweeps', max=5, dur=4.0, autor='Ben Burnes (Abstraction)',
         generos='pop futurebass trap rnb organichouse afrohouse progressive drill amapiano'),
    dict(nombre='Whoosh', repo='sonic-pi', patron=r'^etc/samples/(perc_swoosh|ambi_swoosh|ambi_dark_woosh)\.flac$', tipo='Sweeps', max=2, dur=6.0, autor='Sonic Pi (Freesound CC0)',
         generos='trance progressive melodictechno dubstep futurebass'),
    dict(nombre='Ambiente', repo=LAV, patron=r'^bb - Fans and Drones \(Jul 2021\)/.*\.wav$', tipo='Texturas', max=4, dur=12.0, autor='Ben Burnes (Abstraction)',
         generos='ambient downtempo techno minimal melodictechno lofi'),
    dict(nombre='Drone', repo='sonic-pi', patron=r'^etc/samples/ambi_(drone|haunted_hum|glass_rub|glass_hum|soft_buzz|choir)\.flac$', tipo='Texturas', max=3, dur=12.0, autor='Sonic Pi (Freesound CC0)',
         generos='ambient downtempo melodictechno organichouse deephouse'),
    dict(nombre='Glitch', repo='sonic-pi', patron=r'^etc/samples/glitch_(perc\d|robot\d)\.flac$', tipo='Incidentales', max=4, dur=4.0, autor='Sonic Pi (Freesound CC0)',
         generos='minimal techhouse electro futurebass dubstep indiedance'),
    dict(nombre='Impacto', repo='sonic-pi', patron=r'^etc/samples/(misc_cineboom|perc_impact\d|ambi_lunar_land)\.flac$', tipo='Impactos', max=3, dur=8.0, autor='Sonic Pi (Freesound CC0)',
         generos='trance progressive melodictechno dubstep pop futurebass'),
    dict(nombre='Arcade', repo=LAV, patron=r'^MMRetroArcadeSoundsPack1_0_5/(Misc/wav/OldSchool|Speech/wav/(GetReady|Ready|PowerUp)).*\.wav$', tipo='Incidentales', max=5, dur=3.0, autor='The Motion Monkey (CC0)',
         generos='synthwave electro futurebass jerseyclub'),
    dict(nombre='Rarezas', repo=LAV, patron=r'^30-cc0-weird-sfx/.*\.(wav|ogg)$', tipo='Incidentales', max=4, dur=4.0, autor='rubberduck (OpenGameArt, CC0)',
         generos='psytrance minimal electro'),
    dict(nombre='Voz', repo=LAV, patron=r'^kenney_voiceoverfighter/Audio/(ready|fight|begin|combo|final_round|winner|round_1|time|prepare_yourself|flawless_victory)\.ogg$', tipo='Voces', max=6, dur=3.0, autor='Kenney (CC0)',
         generos='hardstyle hardtechno dubstep dnb jungle trap reggaeton dembow jerseyclub breakbeat electro'),
    dict(nombre='Voz', repo=LAV, patron=r'^kenney_voiceoverpack/(Male|Female)/(go|ready|set|hold|hurry_up|level_up|power_up|correct)\.ogg$', tipo='Voces', max=6, dur=3.0, autor='Kenney (CC0)',
         generos='house techhouse hardstyle trap reggaeton dembow moombahton jerseyclub synthwave pop'),
    dict(nombre='Voz', repo=LAV, patron=r'^warfork-cc0/sounds/announcer/.*(go|fight|start|get_ready_to_fight|ready)[^/]*\.(wav|ogg)$', tipo='Voces', max=4, dur=3.0, autor='Warfork (CC0)',
         generos='hardstyle hardtechno dubstep dnb'),
    dict(nombre='Multitud', repo='lmms', patron=r'^data/samples/misc/(raving_crowd01|applause01)\.ogg$', tipo='Ambientes', max=2, dur=12.0, autor='LMMS (GPL 2.0 o posterior)',
         generos='house techhouse hardstyle disco funk hiphop afrobeats cumbia acid nudisco reggaeton'),
]

def limpio(s):
    s = os.path.splitext(os.path.basename(s))[0]
    s = re.sub(r'^Main_', '', s)
    s = re.sub(r'[_]+', ' ', s)
    s = re.sub(r'\s+', ' ', s).strip()
    return s[:60]

def nombre_archivo(g, ruta):
    # "Vinilo - Classic Record Fuzz (Ben Burnes).flac"
    autor = g['autor'].split(' (')[0]
    quien = {'Male': ' (hombre)', 'Female': ' (mujer)'}.get(os.path.basename(os.path.dirname(ruta)), '')
    n = '%s - %s%s (%s)' % (g['nombre'], limpio(ruta), quien, autor)
    return re.sub(r'[\\/:*?"<>|]', '-', n) + '.flac'

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', default=os.path.join(os.path.expanduser('~'), 'Music', 'Racoon', 'Efectos por género'))
    ap.add_argument('--dry', action='store_true')
    a = ap.parse_args()
    root = a.root
    marca = os.path.join(root, '.racoon-efectos-grabados')
    if os.path.exists(marca) and open(marca).read().strip() == str(VERSION):
        print('Los efectos grabados ya estaban en', root)
        return 0
    # qué archivo va a qué carpetas
    plan = {}       # (repo, ruta) -> [(destino relativo, grupo)]
    for g in GRUPOS:
        rutas = sorted(p for p in core.tree(g['repo']) if re.search(g['patron'], p, re.I) and re.search(AUD, p, re.I) and '__MACOSX' not in p)
        if not rutas:
            print('  !! nada para', g['nombre'], g['patron']); continue
        for gi, gen in enumerate(g['generos'].split()):
            n = min(g['max'], len(rutas))
            # cada género se lleva otros archivos del grupo (rotando), así no todos tienen los mismos
            elegidos = [rutas[(gi * n + k) % len(rutas)] for k in range(n)]
            for r in dict.fromkeys(elegidos):
                rel = os.path.join(GENEROS[gen], g['tipo'], nombre_archivo(g, r))
                plan.setdefault((g['repo'], r), []).append((rel, g))
    copias = sum(len(v) for v in plan.values())
    print('efectos grabados: %d archivos de origen, %d copias en %d géneros' % (len(plan), copias, len(GENEROS)))
    if a.dry:
        return 0
    por_repo = {}
    for (repo, r) in plan: por_repo.setdefault(repo, []).append(r)
    locales = {}
    for repo, rutas in por_repo.items():
        locales.update({(repo, p): f for p, f in core.fetch(repo, rutas).items()})
    hechos, n = 0, 0
    for (repo, r), destinos in sorted(plan.items()):
        n += 1
        if n % 25 == 0 or n == len(plan):
            print('[%3d%%] %d/%d' % (int(100 * n / len(plan)), n, len(plan)), flush=True)
        src = locales.get((repo, r))
        if not src: continue
        try:
            x, sr = core.read(src)
        except Exception:
            continue
        g = destinos[0][1]
        x = core.trim(x, sr, g['dur'], floor_db=-60.0, lead_db=-50.0)
        if x.shape[0] < int(0.02 * sr): continue
        pk = float(np.abs(x).max())
        if pk < 1e-5: continue
        x = (x * (0.89 / pk)).astype(np.float32)
        x = core.maybe_mono(x)
        for rel, _ in destinos:
            core.write(os.path.join(root, rel), x, sr)
            hechos += 1
    with open(os.path.join(root, 'CREDITOS de los efectos grabados.txt'), 'w') as f:
        f.write('EFECTOS POR GÉNERO, LOS GRABADOS: CRÉDITOS Y LICENCIAS\n\n'
                'Los efectos sintetizados (risers, rollers, impactos, sirenas sintéticas...) los hace Racoon y son CC0.\n'
                'Los grabados vienen de estas bibliotecas libres. El nombre de cada archivo dice de quién es.\n\n')
        vistos = set()
        for g in GRUPOS:
            if g['autor'] in vistos: continue
            vistos.add(g['autor'])
            f.write('- %s, origen https://github.com/%s\n' % (g['autor'], core.REPOS[g['repo']]))
        f.write('\nCC0 y dominio público no piden crédito; se da igual. LMMS es GPL 2.0 o posterior, compatible con la GPL3 de Racoon\n'
                '(el texto está en Muestras Racoon/LICENCIAS/GPL-2.0.txt).\n')
    open(marca, 'w').write(str(VERSION))
    print('listo: %d efectos grabados en %s' % (hechos, root))
    return 0

if __name__ == '__main__':
    sys.exit(main())
