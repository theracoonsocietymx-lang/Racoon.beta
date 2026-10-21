# Biblioteca Racoon: definiciones de instrumentos cromáticos (Squirrel)
import re
from melodic import note_scan

def has(*words):
    return lambda stem, folder: all(w in stem.split('_') or w in folder for w in words)
def notin(*words):
    return lambda stem, folder: not any(w in stem.lower() for w in words)
def both(a, b):
    return lambda s, f: a(s, f) and b(s, f)

SPECS = []
def add(local, base, folder, pack, src, cat, name, key, **kw):
    SPECS.append((local, base, folder, pack, src, cat, name, key, kw))

POP = ['pop', 'rnb', 'soul', 'jazz', 'lofi', 'baladas', 'indie']
# ---------------------------------------------------------------- pianos
add('SalamanderGrandPiano', 'Samples/', r'', 'Salamander', 'salamander', 'piano', 'Piano de cola Salamander', 'piano-de-cola',
    maxlen=5.0, maxnotes=30, nvel=3, release=0.45, genres=POP + ['clasica', 'cine', 'house'])
add('sfzinstruments-splendid-grand-piano', 'samples/', r'', 'Splendid Grand', 'splendid', 'piano', 'Piano de cola Splendid (Akai)', 'piano-splendid',
    maxlen=5.0, maxnotes=24, nvel=2, release=0.45, genres=POP + ['clasica', 'house', 'gospel'])
add('freepats-old-piano-fb', 'samples/', r'', 'Freepats', 'freepats_oldpiano', 'piano', 'Piano viejo de cantina', 'piano-viejo',
    maxlen=4.0, maxnotes=22, nvel=1, release=0.3, genres=['lofi', 'blues', 'ragtime', 'country', 'hiphop'])
add('VCSL', 'Chordophones/Zithers/', r'Upright Piano, Knight/Sustains', 'VCSL', 'vcsl', 'piano', 'Piano vertical Knight', 'piano-vertical-knight',
    maxlen=4.0, maxnotes=20, nvel=2, release=0.35, genres=POP + ['lofi', 'folk'])
add('VCSL', 'Electrophones/TX81Z/', r'FM Piano', 'VCSL', 'vcsl', 'piano_electrico', 'Piano FM TX81Z', 'piano-fm-tx81z',
    maxlen=3.5, maxnotes=18, nvel=2, release=0.3, genres=['synthpop', 'city pop', 'rnb', 'house', 'synthwave'])
add('VCSL', 'Electrophones/TX81Z/', r'Clavisynth', 'VCSL', 'vcsl', 'teclado', 'Clavisynth TX81Z', 'clavisynth-tx81z',
    maxlen=2.5, maxnotes=16, nvel=2, release=0.2, genres=['funk', 'synthpop', 'disco'])
add('VCSL', 'Chordophones/Zithers/', r'Harpsichord, French/Sustains', 'VCSL', 'vcsl', 'teclado', 'Clavecín francés', 'clavecin-frances',
    maxlen=3.5, maxnotes=18, nvel=1, release=0.25, genres=['barroco', 'cine', 'pop'])
# ---------------------------------------------------------------- órganos
add('VCSL', 'Aerophones/Edge-blown Aerophones/', r'Pipe Organ/Loud', 'VCSL', 'vcsl', 'organo', 'Órgano de tubos', 'organo-de-tubos',
    sustain=True, maxlen=3.0, maxnotes=16, nvel=1, release=0.5, genres=['cine', 'gotico', 'clasica', 'gospel'])
add('VCSL', 'Aerophones/Edge-blown Aerophones/', r"Renaissance Organ/8'", 'VCSL', 'vcsl', 'organo', 'Órgano renacentista 8 pies', 'organo-renacentista',
    sustain=True, maxlen=3.0, maxnotes=16, nvel=1, release=0.3, genres=['folk', 'cine', 'indie'])
# ---------------------------------------------------------------- bajos
add('karoryfer.sneakybass', 'Samples/', r'finger', 'Sneaky Bass', 'sneakybass', 'bajo', 'Bajo eléctrico con dedos', 'bajo-dedos',
    maxlen=3.0, maxnotes=16, nvel=1, release=0.12, genres=['funk', 'soul', 'pop', 'rock', 'reggae', 'cumbia', 'rnb'])
add('karoryfer.sneakybass', 'Samples/', r'pluck', 'Sneaky Bass', 'sneakybass', 'bajo', 'Bajo eléctrico con púa', 'bajo-pua',
    maxlen=3.0, maxnotes=16, nvel=1, release=0.12, genres=['rock', 'punk', 'indie', 'pop'])
add('karoryfer.sneakybass', 'Samples/', r'mute', 'Sneaky Bass', 'sneakybass', 'bajo', 'Bajo eléctrico apagado', 'bajo-apagado',
    maxlen=1.2, maxnotes=16, nvel=1, release=0.08, genres=['funk', 'disco', 'pop', 'reggaeton'])
add('karoryfer.big-little-bass', 'Samples/', r'', 'Big Little Bass', 'biglittlebass', 'bajo', 'Bajo acústico chico', 'bajo-acustico-chico',
    maxlen=3.0, maxnotes=16, nvel=2, release=0.15, genres=['folk', 'pop', 'indie', 'acustico'])
add('karoryfer.black-and-blue-basses', 'Samples/', r'darkblack/reg', 'Black and Blue', 'blackblue', 'bajo', 'Bajo Dark Black', 'bajo-dark-black',
    maxlen=3.0, maxnotes=16, nvel=2, release=0.12, genres=['rock', 'funk', 'soul', 'pop'])
add('karoryfer.black-and-blue-basses', 'Samples/', r'babyblue/reg', 'Black and Blue', 'blackblue', 'bajo', 'Bajo Baby Blue', 'bajo-baby-blue',
    maxlen=3.0, maxnotes=16, nvel=2, release=0.12, genres=['pop', 'indie', 'rock'])
add('karoryfer.pastabass', 'samples/', r'fetuccine', 'Pasta Bass', 'pastabass', 'bajo', 'Bajo Fettuccine', 'bajo-fettuccine',
    maxlen=2.5, maxnotes=16, nvel=2, release=0.12, genres=['funk', 'rock', 'pop'])
add('karoryfer.pastabass', 'samples/', r'spaghetti', 'Pasta Bass', 'pastabass', 'bajo', 'Bajo Spaghetti', 'bajo-spaghetti',
    maxlen=2.5, maxnotes=16, nvel=2, release=0.12, genres=['funk', 'rock', 'pop'])
add('karoryfer.swagbass', 'notes/', r'', 'Swag Bass', 'swagbass', 'bajo', 'Bajo Swag', 'bajo-swag', filt=notin('open'),
    maxlen=3.0, maxnotes=16, nvel=2, release=0.12, genres=['funk', 'rnb', 'neo soul', 'pop'])
add('karoryfer.fashionbass', 'notes/', r'', 'Fashion Bass', 'fashionbass', 'bajo', 'Bajo Fashion', 'bajo-fashion',
    maxlen=3.0, maxnotes=16, nvel=2, release=0.12, genres=['disco', 'funk', 'pop', 'nu disco'])
add('Karoryfer.HorsePulse', 'Samples/', r'mp', 'Horse Pulse', 'horsepulse', 'bajo', 'Bajo Horse Pulse', 'bajo-horse-pulse',
    maxlen=3.0, maxnotes=16, nvel=1, release=0.15, genres=['rock', 'indie', 'post punk'])
add('karoryfer.growlybass', 'sustain/', r'', 'Growly Bass', 'growlybass', 'bajo', 'Bajo gruñón', 'bajo-grunon',
    maxlen=3.0, maxnotes=16, nvel=2, release=0.15, genres=['rock', 'metal', 'grunge', 'punk'])
add('dsmolken.double-bass', 'pizz/', r'', 'Smolken', 'dsmolken', 'contrabajo', 'Contrabajo pizzicato', 'contrabajo-pizz',
    maxlen=3.0, maxnotes=16, nvel=1, release=0.2, genres=['jazz', 'bossa', 'lofi', 'swing', 'blues'])
add('karoryfer.meatbass', 'Samples/', r'pizz', 'Meat Bass', 'meatbass', 'contrabajo', 'Contrabajo Meat pizzicato', 'contrabajo-meat',
    maxlen=3.0, maxnotes=16, nvel=2, release=0.2, genres=['jazz', 'folk', 'lofi', 'rockabilly'])
add('VSCO-2-CE', 'Strings/', r'Solo Contrabass/Pizz', 'VSCO 2', 'vsco', 'contrabajo', 'Contrabajo de orquesta pizzicato', 'contrabajo-orquesta-pizz',
    maxlen=3.0, maxnotes=14, nvel=1, release=0.2, genres=['cine', 'jazz', 'orquestal'])
# ---------------------------------------------------------------- guitarras
add('karoryfer.emilyguitar', 'notes/', r'', 'Emily Guitar', 'emilyguitar', 'guitarra', 'Guitarra acústica Emily', 'guitarra-emily',
    maxlen=3.5, maxnotes=16, nvel=2, release=0.25, genres=['folk', 'pop', 'indie', 'acustico', 'country', 'latino'])
add('karoryfer.shinyguitar', 'Samples/', r'acoustic', 'Shiny Guitar', 'shinyguitar', 'guitarra', 'Guitarra acústica Shiny', 'guitarra-shiny-acustica',
    filt=notin('release', 'mute', 'perc'), maxlen=3.5, maxnotes=16, nvel=2, release=0.25, genres=['folk', 'pop', 'acustico', 'bossa', 'reggaeton'])
add('karoryfer.shinyguitar', 'Samples/', r'electric', 'Shiny Guitar', 'shinyguitar', 'guitarra', 'Guitarra eléctrica limpia Shiny', 'guitarra-shiny-electrica',
    filt=notin('release', 'mute', 'perc'), maxlen=3.5, maxnotes=16, nvel=2, release=0.25, genres=['indie', 'funk', 'rnb', 'pop', 'cumbia', 'math rock'])
add('karoryfer.black-and-green-guitars', 'Samples/', r'black/ord', 'Black and Green', 'blackgreen', 'guitarra', 'Guitarra eléctrica Black', 'guitarra-black',
    maxlen=3.0, maxnotes=16, nvel=2, release=0.2, genres=['rock', 'surf', 'indie', 'blues'])
add('karoryfer.black-and-green-guitars', 'Samples/', r'green/ord', 'Black and Green', 'blackgreen', 'guitarra', 'Guitarra eléctrica Green', 'guitarra-green',
    maxlen=3.0, maxnotes=16, nvel=2, release=0.2, genres=['rock', 'country', 'indie', 'surf'])
add('VCSL', 'Chordophones/Composite Chordophones/', r'Strumstick/Finger', 'VCSL', 'vcsl', 'cuerda_pulsada', 'Strumstick', 'strumstick',
    maxlen=3.0, maxnotes=14, nvel=1, release=0.3, genres=['folk', 'country', 'indie'])
add('VCSL', 'Chordophones/Zithers/', r'Dan Tranh/Normal', 'VCSL', 'vcsl', 'cuerda_pulsada', 'Đàn tranh', 'dan-tranh',
    maxlen=3.5, maxnotes=16, nvel=1, release=0.3, genres=['world', 'asia', 'cine', 'lofi'])
add('hungarian_zither', 'Samples/', r'mel', 'Hungarian Zither', 'zither', 'cuerda_pulsada', 'Cítara húngara', 'citara-hungara',
    maxlen=3.5, maxnotes=16, nvel=1, release=0.3, genres=['folk', 'world', 'cine'])
# ---------------------------------------------------------------- cuerdas frotadas
add('VSCO-2-CE', 'Strings/', r'Solo Violin/Arco Vib', 'VSCO 2', 'vsco', 'cuerdas', 'Violín solista', 'violin-solista',
    sustain=True, maxlen=3.0, maxnotes=14, nvel=1, release=0.35, genres=['cine', 'orquestal', 'pop', 'folk', 'baladas'])
add('VSCO-2-CE', 'Strings/', r'Violin Section/susVib', 'VSCO 2', 'vsco', 'cuerdas', 'Sección de violines', 'violines-seccion',
    sustain=True, maxlen=3.0, maxnotes=14, nvel=1, release=0.45, genres=['cine', 'orquestal', 'pop', 'disco', 'baladas'])
add('VSCO-2-CE', 'Strings/', r'Viola Section/susvib', 'VSCO 2', 'vsco', 'cuerdas', 'Sección de violas', 'violas-seccion',
    sustain=True, maxlen=3.0, maxnotes=14, nvel=1, release=0.45, genres=['cine', 'orquestal'])
add('VSCO-2-CE', 'Strings/', r'Cello Section/susvib', 'VSCO 2', 'vsco', 'cuerdas', 'Sección de chelos', 'chelos-seccion',
    sustain=True, maxlen=3.0, maxnotes=14, nvel=1, release=0.45, genres=['cine', 'orquestal', 'baladas'])
add('VSCO-2-CE', 'Strings/', r'Violin Section/Pizz', 'VSCO 2', 'vsco', 'cuerdas', 'Violines pizzicato', 'violines-pizz',
    maxlen=1.5, maxnotes=14, nvel=1, release=0.15, genres=['cine', 'pop', 'navidad'])
add('VSCO-2-CE', 'Strings/', r'Solo Violin/spic', 'VSCO 2', 'vsco', 'cuerdas', 'Violín spiccato', 'violin-spiccato',
    maxlen=1.0, maxnotes=14, nvel=1, release=0.1, genres=['cine', 'orquestal', 'pop'])
add('karoryfer-bigcat.cello', 'Samples/', r'staccato', 'Big Cat Cello', 'bigcatcello', 'cuerdas', 'Chelo staccato', 'chelo-staccato',
    maxlen=1.5, maxnotes=14, nvel=2, release=0.15, genres=['cine', 'pop', 'indie'])
add('karoryfer-bigcat.cello', 'Samples/', r'pizzcato', 'Big Cat Cello', 'bigcatcello', 'cuerdas', 'Chelo pizzicato', 'chelo-pizz',
    maxlen=2.0, maxnotes=14, nvel=1, release=0.2, genres=['cine', 'pop', 'indie', 'lofi'])
add('karoryfer.meatbass', 'Samples/', r'arco_looped', 'Meat Bass', 'meatbass', 'cuerdas', 'Contrabajo con arco', 'contrabajo-arco',
    filt=lambda s, f: s.endswith('down'), sustain=True, maxlen=3.0, maxnotes=12, nvel=1, release=0.4, genres=['cine', 'oscuro', 'jazz'])
# ---------------------------------------------------------------- metales y maderas
add('VSCO-2-CE', 'Brass/', r'Trumpet/sus', 'VSCO 2', 'vsco', 'metales', 'Trompeta', 'trompeta', sustain=True, maxlen=3.0, maxnotes=12, nvel=1,
    release=0.25, genres=['cine', 'ska', 'salsa', 'jazz', 'banda', 'mariachi', 'funk'])
add('VSCO-2-CE', 'Brass/', r'Trumpet/stac', 'VSCO 2', 'vsco', 'metales', 'Trompeta staccato', 'trompeta-staccato', maxlen=1.0, maxnotes=12, nvel=1,
    release=0.1, genres=['ska', 'salsa', 'funk', 'banda', 'cumbia'])
add('VSCO-2-CE', 'Brass/', r'F Horn/sus', 'VSCO 2', 'vsco', 'metales', 'Corno francés', 'corno', sustain=True, maxlen=3.0, maxnotes=12, nvel=1,
    release=0.35, genres=['cine', 'orquestal', 'epico'])
add('VSCO-2-CE', 'Brass/', r'Tenor Trombone/sus', 'VSCO 2', 'vsco', 'metales', 'Trombón tenor', 'trombon', sustain=True, maxlen=3.0, maxnotes=12,
    nvel=1, release=0.25, genres=['ska', 'salsa', 'jazz', 'banda', 'cine'])
add('VSCO-2-CE', 'Brass/', r'Tuba/sus', 'VSCO 2', 'vsco', 'metales', 'Tuba', 'tuba', sustain=True, maxlen=3.0, maxnotes=12, nvel=1,
    release=0.25, genres=['banda', 'polka', 'cine', 'norteño'])
add('karoryfer.war-tuba', 'Samples/', r'', 'War Tuba', 'wartuba', 'metales', 'Tuba de guerra', 'tuba-de-guerra',
    filt=lambda s, f: '_s_' in s and s.endswith('_cnd'), maxlen=2.0, maxnotes=14, nvel=2, release=0.2, genres=['banda', 'balcanica', 'cine', 'ska'])
add('VSCO-2-CE', 'Woodwinds/', r'Flute/susNV', 'VSCO 2', 'vsco', 'maderas', 'Flauta', 'flauta', sustain=True, maxlen=3.0, maxnotes=12, nvel=1,
    release=0.25, genres=['cine', 'folk', 'lofi', 'bossa', 'jazz'])
add('VSCO-2-CE', 'Woodwinds/', r'Clarinet/susLong', 'VSCO 2', 'vsco', 'maderas', 'Clarinete', 'clarinete', sustain=True, maxlen=3.0, maxnotes=12,
    nvel=1, release=0.25, genres=['jazz', 'klezmer', 'cine', 'banda'])
add('VSCO-2-CE', 'Woodwinds/', r'Oboe/Vib', 'VSCO 2', 'vsco', 'maderas', 'Oboe', 'oboe', sustain=True, maxlen=3.0, maxnotes=12, nvel=1,
    release=0.25, genres=['cine', 'orquestal'])
add('VSCO-2-CE', 'Woodwinds/', r'Bassoon/sus', 'VSCO 2', 'vsco', 'maderas', 'Fagot', 'fagot', sustain=True, maxlen=3.0, maxnotes=12, nvel=1,
    release=0.25, genres=['cine', 'orquestal'])
add('VCSL', 'Aerophones/Edge-blown Aerophones/', r'Baroque Alto Recorder/Sustain', 'VCSL', 'vcsl', 'maderas', 'Flauta dulce contralto', 'flauta-dulce',
    sustain=True, maxlen=3.0, maxnotes=12, nvel=1, release=0.2, genres=['folk', 'medieval', 'lofi', 'infantil'])
add('VCSL', 'Aerophones/Edge-blown Aerophones/', r'Ocarina, Typical/Sustains/Sus', 'VCSL', 'vcsl', 'maderas', 'Ocarina', 'ocarina',
    sustain=True, maxlen=3.0, maxnotes=12, nvel=1, release=0.2, genres=['folk', 'andina', 'lofi', 'videojuegos'])
add('karoryfer.squidpipes', 'Samples/', r'short_clean', 'Squid Pipes', 'squidpipes', 'maderas', 'Gaita corta', 'gaita-corta',
    maxlen=1.2, maxnotes=12, nvel=1, release=0.1, genres=['folk', 'celta', 'medieval'])
# ---------------------------------------------------------------- saxofones y lengüetas
add('VCSL', 'Aerophones/Reed Aerophones/', r'Tenor Saxophone/Non-Vibrato', 'VCSL', 'vcsl', 'saxofon', 'Saxofón tenor', 'sax-tenor',
    sustain=True, maxlen=3.0, maxnotes=14, nvel=1, release=0.2, genres=['jazz', 'soul', 'funk', 'rnb', 'pop', 'ska', 'synthwave'])
add('karoryfer.weresax', 'Samples/', r'alto', 'Weresax', 'weresax', 'saxofon', 'Saxofón alto', 'sax-alto', mics={'cnd': (1.0, 0.0), 'dnm': (0.7, 0.0)},
    maxlen=2.5, maxnotes=14, nvel=2, release=0.2, genres=['jazz', 'funk', 'pop', 'ska', 'cumbia'])
add('karoryfer.bear-sax', 'Samples/', r'', 'Bear Sax', 'bearsax', 'saxofon', 'Saxofón barítono', 'sax-baritono', filt=lambda s, f: '_vl' in s,
    maxlen=2.5, maxnotes=14, nvel=2, release=0.2, genres=['funk', 'soul', 'ska', 'rock'])
add('VCSL', 'Aerophones/Free Aerophones/', r'Harmonica-Hohner-Special20-C/Sustains/Normal', 'VCSL', 'vcsl', 'armonica', 'Armónica diatónica en Do',
    'armonica-do', sustain=True, maxlen=3.0, maxnotes=12, nvel=1, release=0.2, genres=['blues', 'country', 'folk', 'rock'])
add('VCSL', 'Aerophones/Free Aerophones/', r'Harmonica-Hohner-Super64/Sustains/Normal', 'VCSL', 'vcsl', 'armonica', 'Armónica cromática',
    'armonica-cromatica', sustain=True, maxlen=3.0, maxnotes=14, nvel=1, release=0.2, genres=['jazz', 'blues', 'pop', 'cine'])
# ---------------------------------------------------------------- láminas, campanas, mundo
add('VCSL', 'Idiophones/Struck Idiophones/', r'Marimba', 'VCSL', 'vcsl', 'mallets', 'Marimba', 'marimba', maxlen=2.5, maxnotes=16, nvel=2,
    release=0.3, genres=['tropical', 'afro', 'pop', 'house', 'cumbia', 'chiapas', 'reggaeton'])
add('VCSL', 'Idiophones/Struck Idiophones/', r'Vibraphone/Hard Mallets', 'VCSL', 'vcsl', 'mallets', 'Vibráfono', 'vibrafono', maxlen=4.0, maxnotes=16,
    nvel=2, release=0.6, genres=['jazz', 'lofi', 'bossa', 'house', 'soul'])
add('VCSL', 'Idiophones/Struck Idiophones/', r'Xylophone/Medium Mallets', 'VCSL', 'vcsl', 'mallets', 'Xilófono', 'xilofono', maxlen=1.5, maxnotes=14,
    nvel=2, release=0.2, genres=['infantil', 'cine', 'pop'])
add('VCSL', 'Idiophones/Struck Idiophones/', r'Glockenspiel', 'VCSL', 'vcsl', 'mallets', 'Glockenspiel', 'glockenspiel', maxlen=3.0, maxnotes=14,
    nvel=2, release=0.5, genres=['indie', 'navidad', 'pop', 'cine', 'lofi'])
add('VCSL', 'Idiophones/Struck Idiophones/', r'Tubular Bells 2', 'VCSL', 'vcsl', 'campanas', 'Campanas tubulares', 'campanas-tubulares',
    maxlen=6.0, maxnotes=14, nvel=1, release=1.0, genres=['cine', 'navidad', 'gotico', 'rock'])
add('VCSL', 'Idiophones/Struck Idiophones/', r'Hand Chimes', 'VCSL', 'vcsl', 'campanas', 'Campanas de mano', 'campanas-de-mano',
    maxlen=5.0, maxnotes=14, nvel=1, release=1.0, genres=['navidad', 'ambient', 'cine'])
add('VCSL', 'Idiophones/Struck Idiophones/', r'Balafon/Hard Mallet', 'VCSL', 'vcsl', 'mundo', 'Balafón', 'balafon', maxlen=2.0, maxnotes=14,
    nvel=2, release=0.3, genres=['afro', 'world', 'house', 'amapiano'])
add('VCSL', 'Idiophones/Plucked Idiophones/', r'Kalimba, Tanzania', 'VCSL', 'vcsl', 'mundo', 'Kalimba', 'kalimba', maxlen=3.0, maxnotes=16,
    nvel=1, release=0.4, genres=['afro', 'lofi', 'ambient', 'pop', 'trap'])
add('VCSL', 'Idiophones/Plucked Idiophones/', r'Mbira dzaVadzimu Nyamaropa, Zimbabwe, Low B', 'VCSL', 'vcsl', 'mundo', 'Mbira', 'mbira',
    maxlen=3.0, maxnotes=16, nvel=1, release=0.4, genres=['afro', 'world', 'ambient'])
add('jlearman.SteelDrum', 'flac/', r'', 'Steel Drum', 'steeldrum', 'mundo', 'Tambor de acero', 'steel-drum', midi_numbers=True,
    filt=lambda s, f: True, maxlen=2.5, maxnotes=16, nvel=2, release=0.3, genres=['caribe', 'calypso', 'tropical house', 'reggae', 'pop'])
add('VCSL', 'Chordophones/Composite Chordophones/', r'Concert Harp', 'VCSL', 'vcsl', 'arpa', 'Arpa de concierto', 'arpa-concierto',
    maxlen=4.0, maxnotes=16, nvel=1, release=0.6, genres=['cine', 'clasica', 'baladas', 'navidad'])
add('VCSL', 'Chordophones/Composite Chordophones/', r'Folk Harp', 'VCSL', 'vcsl', 'arpa', 'Arpa folk', 'arpa-folk',
    maxlen=4.0, maxnotes=16, nvel=1, release=0.6, genres=['folk', 'celta', 'andina', 'jarocho', 'cine'])
add('VCSL', 'Membranophones/Struck Membranophones/', r'Timpani 2/Hit', 'VCSL', 'vcsl', 'timbales_orq', 'Timbales de orquesta', 'timbales-orquesta',
    maxlen=4.0, maxnotes=12, nvel=2, release=0.6, genres=['cine', 'epico', 'orquestal'], octave_check=False)
# ---------------------------------------------------------------- sintetizadores muestreados
add('karoryfer.string-cyborgs', 'Samples/', r'zinc/full', 'String Cyborgs', 'stringcyborgs', 'sintetizador', 'Cuerdas cyborg Zinc', 'cyborg-zinc',
    sustain=True, maxlen=3.0, maxnotes=14, nvel=1, release=0.5, genres=['synthwave', 'cine', 'ambient', 'electronica'])
add('karoryfer.string-cyborgs', 'Samples/', r'blackheart', 'String Cyborgs', 'stringcyborgs', 'sintetizador', 'Cuerdas cyborg Blackheart',
    'cyborg-blackheart', filt=lambda s, f: not s.startswith(('singlecycle', 'noise')), sustain=True, maxlen=3.0, maxnotes=14, nvel=1, release=0.5,
    genres=['synthwave', 'darkwave', 'cine'])
add('karoryfer.caveman-cosmonaut', 'Samples/', r'', 'Caveman Cosmonaut', 'caveman', 'sintetizador', 'Cosmonauta: todos', 'cosmonauta-todos',
    filt=lambda s, f: s.startswith('all_all_all'), sustain=True, maxlen=3.0, maxnotes=14, nvel=1, release=0.4, genres=['synthpop', 'retro', 'lofi'])
add('karoryfer.caveman-cosmonaut', 'Samples/', r'', 'Caveman Cosmonaut', 'caveman', 'sintetizador', 'Cosmonauta: flautas', 'cosmonauta-flautas',
    filt=lambda s, f: s.startswith('flutes'), sustain=True, maxlen=3.0, maxnotes=14, nvel=1, release=0.4, genres=['synthpop', 'retro', 'lofi'])
add('karoryfer.caveman-cosmonaut', 'Samples/', r'', 'Caveman Cosmonaut', 'caveman', 'sintetizador', 'Cosmonauta: bajo', 'cosmonauta-bajo',
    filt=lambda s, f: s.startswith('bass_all'), sustain=True, maxlen=3.0, maxnotes=12, nvel=1, release=0.2, genres=['synthpop', 'retro', 'italo'])

def all_instruments():
    out = []
    for local, base, folder, pack, src, cat, name, key, kw in SPECS:
        insts = note_scan(local, base, folder, pack, src, cat, name, key=key, **kw)
        for i in insts:
            out.append((local, i))
    return out
