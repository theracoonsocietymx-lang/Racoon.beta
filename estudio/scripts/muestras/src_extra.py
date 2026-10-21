# Biblioteca Racoon: texturas (Skunk), loops (Racoon), voces sueltas, ondas de un ciclo y tablas de ondas (Badger)
import os, re
import numpy as np
from core import tree, fetch, read, write, trim, analyse, slug, resample
from model import Inst, CATS

CATS.setdefault('textura', ('Texturas', 'texturas', 14.0, 'Custom', False))
CATS.setdefault('loop', ('Loops', 'loops', 20.0, 'Custom', False))
CATS.setdefault('ciclo', ('Ondas de un ciclo', 'ondas', 1.0, 'Custom', False))
CATS.setdefault('tabla_ondas', ('Tablas de ondas', 'ondas', 1.0, 'Custom', False))

class RawInst:
    """Un archivo, sin recorte (texturas largas, loops, ondas)."""
    def __init__(self, pack, src, key, name, cat, path, genres=(), tags=(), maxlen=None, folder=None, keep=False, bpm=None, bars=None, mono=False):
        self.pack, self.src, self.key, self.name, self.cat, self.path = pack, src, key, name, cat, path
        self.genres, self.tags, self.maxlen, self.folder, self.keep, self.bpm, self.bars, self.mono = list(genres), list(tags), maxlen, folder, keep, bpm, bars, mono
        self.members = {(1, 1): [(path, 1.0, 0.0)]}
    def choose(self):
        return [0]
    def render(self, local, root):
        f = fetch(local, [self.path])
        if self.path not in f:
            return []
        x, sr = read(f[self.path])
        if sr > 48000:
            x = resample(x, sr, 48000); sr = 48000
        if not self.keep:
            x = trim(x, sr, self.maxlen or CATS[self.cat][2], floor_db=-62.0, fade_ms=200.0, keep_lead=self.cat in ('loop',))
        if self.mono or x.shape[1] == 2 and np.allclose(x[:, 0], x[:, 1], atol=1e-4):
            x = x.mean(axis=1, keepdims=True)
        pk = float(np.abs(x).max()) if x.size else 0
        if pk <= 0:
            return []
        if self.cat not in ('ciclo', 'tabla_ondas'):
            x = x * (10 ** (-1.0 / 20) / pk)
        folder = self.folder or '%s/%s' % (CATS[self.cat][1], slug(self.pack))
        rel = '%s/%s.flac' % (folder, slug(self.key))
        write(os.path.join(root, rel), x.astype(np.float32), sr)
        e = dict(id=rel[:-5], archivo=rel, nombre=self.name, cat=self.cat, grupo='%s/%s' % (slug(self.pack), slug(self.key)), pack=self.pack,
                 fuente=self.src, capa=1, capas=1, rr=1, sr=sr, canales=x.shape[1], **analyse(x, sr))
        if self.cat == 'ciclo':
            e['ciclo'] = x.shape[0]
        if self.cat == 'tabla_ondas':
            e['ciclo'] = 256; e['cuadros'] = x.shape[0] // 256
        if self.bpm:
            e['bpm'] = self.bpm; e['compases'] = self.bars
        if self.genres:
            e['generos'] = self.genres
        if self.tags:
            e['etiquetas'] = self.tags
        return [e]

def loop_bpm(dur, lo=70, hi=180):
    for beats in (4, 8, 16, 32, 2, 64):
        b = 60.0 * beats / dur
        if lo <= b <= hi:
            return round(b, 2), beats / 4.0
    return None, None

def textures():
    out = []
    L = 'sonic-pi'
    names = {'ambi_choir': 'Coro ambiental', 'ambi_dark_woosh': 'Soplido oscuro', 'ambi_drone': 'Dron', 'ambi_glass_hum': 'Zumbido de vidrio',
             'ambi_glass_rub': 'Vidrio frotado', 'ambi_haunted_hum': 'Zumbido embrujado', 'ambi_lunar_land': 'Aterrizaje lunar',
             'ambi_piano': 'Piano ambiental', 'ambi_sauna': 'Sauna', 'ambi_soft_buzz': 'Zumbido suave', 'ambi_swoosh': 'Barrido',
             'tbd_pad_1': 'Pad The Black Dog 1', 'tbd_pad_2': 'Pad The Black Dog 2', 'tbd_pad_3': 'Pad The Black Dog 3', 'tbd_pad_4': 'Pad The Black Dog 4',
             'tbd_fxbed_loop': 'Cama de efectos', 'tbd_voctone': 'Tono vocal', 'tbd_highkey_c4': 'Tecla aguda', 'loop_drone_g_97': 'Dron en Sol',
             'guit_harmonics': 'Armónicos de guitarra', 'guit_em9': 'Guitarra Mi menor 9', 'guit_e_fifths': 'Guitarra quintas', 'guit_e_slide': 'Guitarra slide',
             'misc_cineboom': 'Golpe de cine', 'vinyl_hiss': 'Siseo de vinilo', 'bass_drop_c': 'Caída de bajo'}
    for p in tree(L):
        m = re.fullmatch(r'etc/samples/(\w+)\.flac', p)
        if m and m.group(1) in names:
            out.append(RawInst('Sonic Pi', 'sonicpi', m.group(1), names[m.group(1)], 'textura', p,
                               ['ambient', 'lofi', 'cine', 'downtempo', 'trip-hop'], ['textura'], maxlen=14.0))
    L = 'VCSL'
    for p in tree(L):
        if re.search(r'Wine Glasses/Sustains/Slow/.*\.wav$', p):
            n = re.search(r'glass\d_([A-G]#?\d)', p).group(1)
            out.append(RawInst('VCSL', 'vcsl', 'copa-' + n, 'Copa de vidrio ' + n, 'textura', p, ['ambient', 'cine'], ['textura', 'afinado ' + n], maxlen=12.0))
        elif re.search(r'Didgeridoo/Didgeridoo1_(Sus\d|Phrase\d)_Main\.wav$', p):
            k = re.search(r'(Sus\d|Phrase\d)', p).group(1)
            out.append(RawInst('VCSL', 'vcsl', 'didgeridoo-' + k, 'Didgeridoo ' + k.replace('Sus', 'sostenido ').replace('Phrase', 'frase '), 'textura', p,
                               ['world', 'ambient', 'tribal'], ['textura'], maxlen=12.0))
        elif re.search(r'Ocean Drum/OceanDrum_Sus_\d_Mid\.wav$', p):
            k = re.search(r'Sus_(\d)', p).group(1)
            out.append(RawInst('VCSL', 'vcsl', 'tambor-oceano-' + k, 'Tambor de océano ' + k, 'textura', p, ['ambient', 'cine', 'chill'], ['textura'], maxlen=12.0))
        elif re.search(r'Vibraphone/Bowed/Vibes_bowed_\w+_rr1_Main\.wav$', p):
            n = re.search(r'bowed_([A-G]#?\d)', p).group(1)
            out.append(RawInst('VCSL', 'vcsl', 'vibrafono-arco-' + n, 'Vibráfono con arco ' + n, 'textura', p, ['ambient', 'cine'], ['textura', 'afinado ' + n], maxlen=12.0))
        elif re.search(r'Suspended Cymbal 1/susCymb1_bowed_\d\.wav$', p) or re.search(r'Suspended Cymbal 1/susCymb1_cresc_\d+s\.wav$', p):
            k = os.path.splitext(os.path.basename(p))[0]
            out.append(RawInst('VCSL', 'vcsl', k, 'Platillo ' + ('con arco ' if 'bowed' in k else 'crescendo ') + k[-1], 'textura', p, ['cine', 'ambient', 'edm'],
                               ['textura', 'subida' if 'cresc' in k else 'metal'], maxlen=12.0))
    L = 'VSCO-2-CE'
    for p in tree(L):
        m = re.fullmatch(r'Miscellania Raw/Misc 1/(bubbles\d?|chain_loop|chaingrindLoop|ambience\d|vibraring\d|cymb_gong\d?|cymbgong_weird|brick_scrape\d?)\.wav', p)
        if m:
            k = m.group(1)
            nm = {'bubbles': 'Burbujas', 'chain_loop': 'Cadena en bucle', 'chaingrindLoop': 'Cadena raspada', 'ambience': 'Ambiente', 'vibraring': 'Anillo de vibráfono',
                  'cymb_gong': 'Platillo y gong', 'cymbgong_weird': 'Gong raro', 'brick_scrape': 'Ladrillo raspado'}
            base = re.sub(r'\d+$', '', k)
            out.append(RawInst('VSCO 2', 'vsco', k, nm.get(base, k) + (' ' + re.findall(r'\d+$', k)[0] if re.findall(r'\d+$', k) else ''), 'textura', p,
                               ['cine', 'experimental', 'ambient'], ['textura'], maxlen=12.0))
    L = 'karoryfer.scarypiano'
    for p in tree(L):
        m = re.fullmatch(r'Samples/horror/piano loud/([A-G]b?\d)-\d+-\d+_Dehumanised\.wav', p)
        if m and m.group(1)[-1] in '1357':
            n = m.group(1)
            out.append(RawInst('Scary Piano', 'scarypiano', 'horror-' + n, 'Piano de terror ' + n, 'textura', p, ['cine', 'horror', 'darkwave', 'trap'],
                               ['textura', 'afinado ' + n], maxlen=10.0))
    return out

def loops():
    out = []
    L = 'sonic-pi'
    names = {'loop_amen': 'Amen', 'loop_amen_full': 'Amen completo', 'loop_breakbeat': 'Breakbeat', 'loop_compus': 'Compus', 'loop_electric': 'Eléctrico',
             'loop_garzul': 'Garzul', 'loop_industrial': 'Industrial', 'loop_mehackit1': 'Mehackit 1', 'loop_mehackit2': 'Mehackit 2', 'loop_mika': 'Mika',
             'loop_perc1': 'Percusión 1', 'loop_perc2': 'Percusión 2', 'loop_safari': 'Safari', 'loop_tabla': 'Tabla', 'loop_weirdo': 'Raro',
             'loop_3d_printer': 'Impresora 3D', 'arovane_beat_a': 'Arovane A', 'arovane_beat_b': 'Arovane B', 'arovane_beat_c': 'Arovane C',
             'arovane_beat_d': 'Arovane D', 'arovane_beat_e': 'Arovane E'}
    genres = {'loop_amen': ['jungle', 'dnb', 'breakbeat', 'hiphop'], 'loop_amen_full': ['jungle', 'dnb', 'breakbeat'], 'loop_breakbeat': ['breakbeat', 'hiphop', 'big beat'],
              'loop_industrial': ['industrial', 'techno', 'ebm'], 'loop_tabla': ['world', 'trip-hop'], 'loop_safari': ['afro', 'world']}
    for p in tree(L):
        m = re.fullmatch(r'etc/samples/(\w+)\.flac', p)
        if m and m.group(1) in names:
            k = m.group(1)
            out.append(RawInst('Sonic Pi', 'sonicpi', k, 'Loop ' + names[k], 'loop', p, genres.get(k, ['idm', 'experimental', 'lofi', 'electronica']), ['loop'],
                               keep=True))
    return out

def voices():
    out = []
    L = 'karoryfer.272-merry-orks'
    bad = ('bullshit', 'fuck', 'shit')
    for p in tree(L):
        m = re.fullmatch(r'samples/(consonants|dipthongs|vowels|misc|predrop)/(\w+?)(?:_rr(\d+))?\.wav', p)
        if not m or any(b in m.group(2) for b in bad):
            continue
        if m.group(3) and m.group(3) != '1':
            continue
        kind = {'consonants': 'Consonante', 'dipthongs': 'Diptongo', 'vowels': 'Vocal', 'misc': 'Exclamación', 'predrop': 'Frase'}[m.group(1)]
        w = m.group(2).replace('_from_', ' de ').replace('_', ' ')
        out.append(RawInst('Merry Orks', 'merryorks', m.group(1) + '-' + m.group(2), '%s: %s' % (kind, w), 'voz', p,
                           ['edm', 'hiphop', 'house', 'pop', 'trap'], ['voz', 'en inglés'], maxlen=3.0))
    return out

def wavetables():
    out = []
    L = 'sonic-pi'
    for p in tree(L):
        m = re.fullmatch(r'etc/wavetables/AKWF/(AKWF_\w+)/(AKWF_\w+)\.wav', p)
        if m:
            grp = m.group(1).replace('AKWF_', '')
            out.append(RawInst('AKWF', 'akwf', m.group(2), 'AKWF ' + m.group(2).replace('AKWF_', ''), 'ciclo', p, (), ['un ciclo', grp],
                               keep=True, folder='ondas/akwf/' + slug(grp), mono=True))
    L = 'wavedit-online'
    for p in tree(L):
        m = re.fullmatch(r'samples/([^/]+)\.WAV', p, re.I)
        if m:
            out.append(RawInst('WaveEdit Online', 'waveedit', m.group(1), 'WaveEdit ' + m.group(1).replace('_', ' ').strip(), 'tabla_ondas', p, (),
                               ['64 cuadros'], keep=True, folder='ondas/waveedit', mono=True))
    return out

def all_extra():
    jobs = []
    for i in textures():
        jobs.append(('sonic-pi' if i.src == 'sonicpi' else {'vcsl': 'VCSL', 'vsco': 'VSCO-2-CE', 'scarypiano': 'karoryfer.scarypiano'}[i.src], i))
    for i in loops():
        jobs.append(('sonic-pi', i))
    for i in voices():
        jobs.append(('karoryfer.272-merry-orks', i))
    for i in wavetables():
        jobs.append(('sonic-pi' if i.src == 'akwf' else 'wavedit-online', i))
    return jobs
