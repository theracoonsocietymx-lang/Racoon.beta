# Biblioteca Racoon: núcleo del constructor (descarga, mezcla de micrófonos, recorte, análisis, catálogo)
import os, re, json, math, subprocess, hashlib, random
from concurrent.futures import ThreadPoolExecutor
import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

# dónde se clonan los repositorios (solo la lista de archivos) y dónde quedan las descargas
_BASE = os.environ.get('RACOON_MUESTRAS_TRABAJO', os.path.join(os.path.expanduser('~'), '.cache', 'racoon-muestras'))
SRC = os.environ.get('RACOON_MUESTRAS_SRC', os.path.join(_BASE, 'repos'))
CACHE = os.environ.get('RACOON_MUESTRAS_CACHE', os.path.join(_BASE, 'descargas'))

# carpeta local -> owner/repo en GitHub
REPOS = {
    'virtuosity_drums': 'sfzinstruments/virtuosity_drums',
    'karoryfer.frankensnare': 'sfzinstruments/karoryfer.frankensnare',
    'karoryfer.unruly-drums': 'sfzinstruments/karoryfer.unruly-drums',
    'karoryfer.big-rusty-drums': 'sfzinstruments/karoryfer.big-rusty-drums',
    'karoryfer.swirly-drums': 'sfzinstruments/karoryfer.swirly-drums',
    'Karoryfer.TheHatWithThePhat': 'sfzinstruments/Karoryfer.TheHatWithThePhat',
    'body_percussion': 'sfzinstruments/body_percussion',
    'karoryfer.gogodze-phu-vol-i': 'sfzinstruments/karoryfer.gogodze-phu-vol-i',
    'karoryfer.gogodze-phu-vol-ii': 'sfzinstruments/karoryfer.gogodze-phu-vol-ii',
    'karoryfer.272-merry-orks': 'sfzinstruments/karoryfer.272-merry-orks',
    'kinwie.dim-cabasa': 'sfzinstruments/kinwie.dim-cabasa',
    'jlearman.SteelDrum': 'sfzinstruments/jlearman.SteelDrum',
    'DrumGizmo.DRSKit': 'sfzinstruments/DrumGizmo.DRSKit',
    'DrumGizmo.MuldjordKit': 'sfzinstruments/DrumGizmo.MuldjordKit',
    'WilkinsonAudio.NakedDrums': 'sfzinstruments/WilkinsonAudio.NakedDrums',
    'VCSL': 'sgossner/VCSL',
    'VSCO-2-CE': 'sgossner/VSCO-2-CE',
    'sonic-pi': 'sonic-pi-net/sonic-pi',
    'sounds-tr808-fischer': 'tidalcycles/sounds-tr808-fischer',
    'open-drums': 'fluid-music/open-drums',
    'sampled': 'oramics/sampled',
    'drum-machines': 'smpldsnds/drum-machines',
    'hydrogen': 'hydrogen-music/hydrogen',
    'wavedit-online': 'smpldsnds/wavedit-online',
    'sfzinstruments-jlearman-jrhodes3d': 'smpldsnds/sfzinstruments-jlearman-jrhodes3d',
    'sfzinstruments-splendid-grand-piano': 'smpldsnds/sfzinstruments-splendid-grand-piano',
    'freepats-old-piano-fb': 'smpldsnds/freepats-old-piano-fb',
    'karoryfer.scarypiano': 'sfzinstruments/karoryfer.scarypiano',
    'SalamanderGrandPiano': 'sfzinstruments/SalamanderGrandPiano',
    'free-drum-samples': 'Boochi44/free-drum-samples',
}
for _r in ('karoryfer.big-little-bass karoryfer.black-and-blue-basses karoryfer.meatbass karoryfer.pastabass karoryfer.growlybass karoryfer.swagbass '
           'karoryfer.fashionbass karoryfer.sneakybass karoryfer.ergo Karoryfer.HorsePulse dsmolken.double-bass karoryfer.black-and-green-guitars '
           'karoryfer.emilyguitar karoryfer.shinyguitar karoryfer.weresax karoryfer.bear-sax karoryfer-bigcat.cello hungarian_zither karoryfer.squidpipes '
           'karoryfer.war-tuba karoryfer.cowsynth karoryfer.caveman-cosmonaut karoryfer.string-cyborgs Osiris_Piano').split():
    REPOS.setdefault(_r, 'sfzinstruments/' + _r)

_trees = {}
def tree(local):
    """lista de archivos del repositorio (sin descargar nada)"""
    if local not in _trees:
        d = os.path.join(SRC, local)
        if not os.path.isdir(d):
            subprocess.run(['git', 'clone', '-q', '--depth', '1', '--filter=blob:none', '--no-checkout',
                            'https://github.com/%s.git' % REPOS[local], d], check=True)
        out = subprocess.run(['git', '-C', d, 'ls-tree', '-r', '--name-only', '-z', 'HEAD'], capture_output=True, check=True).stdout
        _trees[local] = [p for p in out.decode('utf-8').split('\0') if p]
    return _trees[local]

def head_ref(local):
    d = os.path.join(SRC, local)
    return subprocess.run(['git', '-C', d, 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout.strip()

def _cache_path(local, path):
    return os.path.join(CACHE, local, path)

def _download(args):
    local, path = args
    dst = _cache_path(local, path)
    if os.path.exists(dst) and os.path.getsize(dst) > 0:
        return True
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    from urllib.parse import quote
    for attempt in range(4):
        url = 'https://raw.githubusercontent.com/%s/HEAD/%s' % (REPOS[local], quote(path))
        r = subprocess.run(['curl', '-sSf', '--retry', '3', '-o', dst + '.part', url], capture_output=True)
        if r.returncode == 0:
            with open(dst + '.part', 'rb') as f:
                head = f.read(64)
            if head.startswith(b'version https://git-lfs'):
                url = 'https://media.githubusercontent.com/media/%s/HEAD/%s' % (REPOS[local], quote(path))
                r = subprocess.run(['curl', '-sSfL', '--retry', '3', '-o', dst + '.part', url], capture_output=True)
                if r.returncode != 0:
                    continue
            os.replace(dst + '.part', dst)
            return True
    return False

def fetch(local, paths, workers=24):
    paths = sorted(set(paths))
    todo = [(local, p) for p in paths if not os.path.exists(_cache_path(local, p))]
    if todo:
        with ThreadPoolExecutor(workers) as ex:
            ok = list(ex.map(_download, todo))
        bad = [t[1] for t, o in zip(todo, ok) if not o]
        if bad:
            print('  !! no se pudieron bajar %d archivos de %s (p.ej. %s)' % (len(bad), local, bad[0]))
    return {p: _cache_path(local, p) for p in paths if os.path.exists(_cache_path(local, p))}

# ------------------------------------------------------------------ audio
def _ffmpeg(path, sr):
    # solo si soundfile no puede (ffmpeg es opcional: en una Mac sin él, ese archivo se salta)
    try:
        r = subprocess.run(['ffmpeg', '-v', 'quiet', '-i', path, '-f', 'f32le', '-ac', '2', '-ar', str(sr), '-'], capture_output=True)
    except FileNotFoundError:
        return np.zeros((0, 2), dtype=np.float32), sr
    return np.frombuffer(r.stdout, dtype=np.float32).reshape(-1, 2).copy(), sr

def read(path):
    # soundfile (libsndfile 1.1 o más nuevo) lee wav, flac, ogg y mp3; ffmpeg queda de respaldo
    try:
        x, sr = sf.read(path, always_2d=True, dtype='float32')
    except Exception:
        x, sr = _ffmpeg(path, 44100 if path.lower().endswith(('.ogg', '.m4a', '.mp3')) else 48000)
    if x.shape[1] > 2:
        x = x[:, :2]
    return x, sr

def resample(x, sr, target):
    if sr == target:
        return x
    g = math.gcd(int(sr), int(target))
    return resample_poly(x, target // g, sr // g, axis=0).astype(np.float32)

def mix(parts):
    """parts: [(archivo, ganancia, pan -1..1)] -> estéreo float32 y frecuencia"""
    bufs, rate = [], None
    for f, g, pan in parts:
        x, sr = read(f)
        if rate is None:
            rate = min(sr, 48000) if sr > 48000 else sr
        x = resample(x, sr, rate)
        if x.shape[1] == 1:
            l = g * min(1.0, 1.0 - pan); r = g * min(1.0, 1.0 + pan)
            st = np.stack([x[:, 0] * l, x[:, 0] * r], axis=1)
        else:
            st = x * g
            if pan != 0.0:
                st[:, 0] *= min(1.0, 1.0 - pan); st[:, 1] *= min(1.0, 1.0 + pan)
        bufs.append(st)
    n = max(b.shape[0] for b in bufs)
    out = np.zeros((n, 2), dtype=np.float32)
    for b in bufs:
        out[:b.shape[0]] += b
    return out, rate

def db(x):
    return 20.0 * math.log10(max(x, 1e-9))

def trim(x, sr, maxlen, floor_db=-60.0, lead_db=-42.0, fade_ms=40.0, keep_lead=False):
    if x.shape[0] == 0:
        return x
    x = x - x.mean(axis=0, keepdims=True) * (np.abs(x.mean(axis=0)) > 2e-4)
    env = np.abs(x).max(axis=1)
    pk = float(env.max())
    if pk < 1e-6:
        return x[:0]
    start = 0
    if not keep_lead:
        idx = np.argmax(env > pk * 10 ** (lead_db / 20.0))
        start = max(0, int(idx) - int(0.0015 * sr))
    # envolvente suavizada (máximo en ventanas de 10 ms) para encontrar la cola
    w = max(1, int(0.01 * sr))
    nb = len(env) // w
    if nb > 0:
        blocks = env[:nb * w].reshape(nb, w).max(axis=1)
        above = np.nonzero(blocks > pk * 10 ** (floor_db / 20.0))[0]
        end = (int(above[-1]) + 2) * w if len(above) else len(env)
    else:
        end = len(env)
    end = min(len(env), end, start + int(maxlen * sr))
    y = x[start:end].copy()
    if start > 0:
        fi = min(len(y), int(0.0007 * sr))
        y[:fi] *= np.linspace(0, 1, fi, dtype=np.float32)[:, None]
    fo = min(len(y) // 4, int(fade_ms / 1000.0 * sr))
    if fo > 1:
        y[-fo:] *= (0.5 + 0.5 * np.cos(np.linspace(0, math.pi, fo, dtype=np.float32)))[:, None]
    return y

def maybe_mono(x):
    if x.shape[1] == 1:
        return x
    l, r = x[:, 0], x[:, 1]
    el, er = float(np.dot(l, l)), float(np.dot(r, r))
    if el < 1e-12 or er < 1e-12:
        return (l + r)[:, None] * 0.5 if max(el, er) > 0 else x[:, :1]
    c = float(np.dot(l, r)) / math.sqrt(el * er)
    if c > 0.995 and abs(10 * math.log10(el / er)) < 0.5:
        return (0.5 * (l + r))[:, None]
    return x

def yin(x, sr, fmin=30.0, fmax=1500.0):
    n = len(x)
    maxlag = min(n // 2, int(sr / fmin)); minlag = max(2, int(sr / fmax))
    if maxlag <= minlag + 2:
        return 0.0
    W = min(n - maxlag, int(sr * 0.08))
    if W < 64:
        return 0.0
    x = x.astype(np.float64)
    d = np.zeros(maxlag + 1)
    for tau in range(1, maxlag + 1):
        df = x[:W] - x[tau:tau + W]
        d[tau] = np.dot(df, df)
    cm = np.ones(maxlag + 1)
    run = np.cumsum(d[1:])
    cm[1:] = np.where(run > 0, d[1:] * np.arange(1, maxlag + 1) / np.maximum(run, 1e-12), 1)
    best = -1
    for tau in range(minlag, maxlag):
        if cm[tau] < 0.15:
            while tau + 1 < maxlag and cm[tau + 1] < cm[tau]:
                tau += 1
            best = tau
            break
    if best < 0:
        best = minlag + int(np.argmin(cm[minlag:maxlag]))
        if cm[best] > 0.45:
            return 0.0
    t = float(best)
    if 1 < best < maxlag:
        a, b, c = cm[best - 1], cm[best], cm[best + 1]
        den = a - 2 * b + c
        if abs(den) > 1e-12:
            t += 0.5 * (a - c) / den
    return sr / t

def analyse(x, sr, tonal=False):
    m = x.mean(axis=1)
    n = len(m)
    pk = float(np.abs(x).max()) if n else 0.0
    head = m[:int(0.3 * sr)]
    rms = float(np.sqrt(np.mean(head ** 2))) if len(head) else 0.0
    spec = np.abs(np.fft.rfft(head * np.hanning(len(head)))) if len(head) > 32 else np.zeros(2)
    freqs = np.fft.rfftfreq(len(head), 1.0 / sr) if len(head) > 32 else np.zeros(2)
    cen = float((spec * freqs).sum() / max(spec.sum(), 1e-12))
    pitch = 0.0
    if tonal and n > int(0.06 * sr):
        s = int(0.012 * sr)
        pitch = yin(m[s:s + int(0.3 * sr)], sr)
    return dict(dur=round(n / sr, 3), pico_db=round(db(pk), 2), rms_db=round(db(rms), 2), brillo=int(cen), tono_hz=round(pitch, 2))

def write(path, x, sr, bits=16):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if bits == 16:
        rng = np.random.default_rng(abs(hash(path)) % (2 ** 32))
        d = (rng.random(x.shape) - rng.random(x.shape)).astype(np.float32) / 32768.0
        x = np.clip(x + d, -1.0, 32767.0 / 32768.0)
        sf.write(path, x, sr, subtype='PCM_16', format='FLAC')
    else:
        sf.write(path, x, sr, subtype='PCM_24', format='FLAC')

def slug(s):
    s = s.lower()
    for a, b in (('á', 'a'), ('é', 'e'), ('í', 'i'), ('ó', 'o'), ('ú', 'u'), ('ñ', 'n'), ('ü', 'u')):
        s = s.replace(a, b)
    s = re.sub(r'[^a-z0-9]+', '-', s).strip('-')
    return s

NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
def note_number(name):
    m = re.fullmatch(r'([A-Ga-g])([#b]?)(-?\d)', name)
    if not m:
        return None
    base = {'c': 0, 'd': 2, 'e': 4, 'f': 5, 'g': 7, 'a': 9, 'b': 11}[m.group(1).lower()]
    acc = {'#': 1, 'b': -1, '': 0}[m.group(2)]
    return (int(m.group(3)) + 1) * 12 + base + acc

def hz_to_midi(f):
    return 69 + 12 * math.log2(f / 440.0) if f > 0 else None
