# Salas reales para la reverb de convolución de Racoon ("Sala real"). Toma respuestas de impulso de OpenAIR (Universidad de
# York) tal como las publica el proyecto Reverb.js en GitHub, solo las que su licencia deja redistribuir y usar en música
# comercial: dominio público, CC BY y CC BY-SA. Las que son "no comercial" o "sin derivados" se quedan fuera.
#
#   python3 salas.py --root "/ruta/Muestras Racoon" [--src /ruta/a/Reverb.js] [--solo-libres]
#
# --solo-libres deja solo las de dominio público y CC BY (las que van en el paquete; el instalador arma todas).
# Por cada sala: arranca medio milisegundo antes del primer sonido, la cola se corta a -70 dB con un fundido, se pasa a
# 48 kHz estéreo y se escribe en FLAC de 24 bits. salas/salas.json guarda su nombre, licencia, autor, origen y su caída.
import argparse, json, math, os, subprocess, sys
import numpy as np
import soundfile as sf

REPO = 'https://github.com/andibrae/Reverb.js.git'
COMMIT = 'HEAD'

# archivo en Reverb.js, id, nombre, descripción corta, licencia
SALAS = [
    ('Basement', 'sotano', 'Taller en sótano', 'Un taller de carpintería en un sótano: cuarto chico de madera.', 'Dominio público (CC0)'),
    ('DomesticLivingRoom', 'sala-casa', 'Sala de una casa', 'La sala de una casa con muebles: corta y cálida.', 'Dominio público (CC0)'),
    ('EmptyApartmentBedroom', 'recamara-vacia', 'Recámara vacía', 'Un cuarto de departamento sin muebles: rebota brillante.', 'Dominio público (CC0)'),
    ('PurnodesRailroadTunnel', 'tunel-tren', 'Túnel de tren', 'Un túnel de ferrocarril: largo y oscuro.', 'Dominio público (CC0)'),
    ('SaintLawrenceChurchMolenbeekWersbeekBelgium', 'iglesia-san-lorenzo', 'Iglesia de San Lorenzo', 'Una iglesia de pueblo en Bélgica.', 'Dominio público (CC0)'),
    ('SpokaneWomansClub', 'salon-spokane', 'Salón de baile Spokane', 'Un salón de 25 por 13 metros con piso de madera y techo curvo.', 'CC BY'),
    ('FalklandPalaceRoyalTennisCourt', 'cancha-falkland', 'Cancha del palacio de Falkland', 'Una cancha de tenis real del siglo XVI, de piedra.', 'CC BY-SA'),
    ('HamiltonMausoleum', 'mausoleo-hamilton', 'Mausoleo Hamilton', 'Una de las colas más largas de cualquier edificio: unos 15 segundos.', 'CC BY-SA'),
    ('InsidePiano', 'dentro-piano', 'Dentro de un piano', 'La caja y las cuerdas de un piano resonando.', 'CC BY-SA'),
    ('LadyChapelStAlbansCathedral', 'capilla-st-albans', 'Capilla de St Albans', 'La capilla de la catedral de St Albans.', 'CC BY-SA'),
    ('MaesHowe', 'maeshowe', 'Tumba Maeshowe', 'Una tumba de piedra de hace 5,000 años en Escocia.', 'CC BY-SA'),
    ('MidiverbMark2Preset29', 'midiverb', 'Midiverb II', 'Una reverb digital de los 80, preset 29.', 'CC BY-SA'),
    ('R1NuclearReactorHall', 'reactor-r1', 'Sala del reactor R1', 'La sala de concreto de un reactor nuclear en Suecia.', 'CC BY-SA'),
    ('SportsCentreUniversityOfYork', 'gimnasio', 'Gimnasio', 'Un gimnasio grande de universidad.', 'CC BY-SA'),
    ('StAndrewsChurch', 'iglesia-st-andrews', 'Iglesia de St Andrews', 'Una iglesia de piedra mediana.', 'CC BY-SA'),
    ('StMarysAbbeyReconstructionPhase1', 'abadia-st-mary', 'Abadía de St Mary', 'La abadía de York reconstruida en computadora.', 'CC BY-SA'),
    ('StPatricksChurchPatringtonPosition1', 'iglesia-st-patrick', 'Iglesia de St Patrick', 'Una iglesia gótica grande del siglo XIV.', 'CC BY-SA'),
    ('StairwayUniversityOfYork', 'escalera', 'Escalera de concreto', 'Un cubo de escaleras: rebote metálico.', 'CC BY-SA'),
    ('TerrysFactoryWarehouse', 'bodega', 'Bodega de fábrica', 'La bodega vacía de una fábrica de chocolates.', 'CC BY-SA'),
    ('TerrysTypingRoom', 'oficina', 'Oficina vieja', 'La sala de mecanógrafas de la misma fábrica.', 'CC BY-SA'),
    ('TyndallBruceMonument', 'monumento-tyndall', 'Monumento Tyndall Bruce', 'Un monumento de piedra abierto.', 'CC BY-SA'),
    ('UndergroundCarPark', 'estacionamiento', 'Estacionamiento subterráneo', 'Concreto y columnas.', 'CC BY-SA'),
    ('YorkMinster', 'catedral-york', 'Catedral de York', 'La catedral gótica más grande del Reino Unido: 160 metros de largo.', 'CC BY-SA'),
]


def fetch(dest):
    if os.path.isdir(os.path.join(dest, '.git')):
        return dest
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    env = dict(os.environ, GIT_LFS_SKIP_SMUDGE='1')
    subprocess.run(['git', 'clone', '--depth', '1', '--filter=blob:none', '--no-checkout', REPO, dest], check=True, env=env)
    files = []
    for f, *_ in SALAS:
        files += ['Library/%s.wav' % f, 'Library/%s.attribution.txt' % f]
    subprocess.run(['git', '-C', dest, 'checkout', COMMIT, '--'] + files, check=True, env=env)
    return dest


def resample(x, sr, to=48000):
    if sr == to:
        return x
    from scipy.signal import resample_poly
    g = math.gcd(int(sr), to)
    return resample_poly(x, to // g, int(sr) // g, axis=0).astype(np.float32)


def t60(x, sr):
    # Schroeder: la integral hacia atrás de la energía, de -5 a -25 dB, extrapolada a -60
    e = (x ** 2).sum(axis=1)
    edc = np.cumsum(e[::-1])[::-1]
    edc = 10 * np.log10(edc / edc[0] + 1e-30)
    try:
        i5 = np.argmax(edc <= -5); i25 = np.argmax(edc <= -25)
        return round(float((i25 - i5) / sr * 3.0), 2) if i25 > i5 else 0.0
    except Exception:
        return 0.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', required=True)
    ap.add_argument('--src', default=os.path.expanduser('~/.cache/racoon-muestras/repos/Reverb.js'))
    ap.add_argument('--solo-libres', action='store_true')
    a = ap.parse_args()
    src = fetch(a.src)
    out = os.path.join(a.root, 'salas')
    os.makedirs(out, exist_ok=True)
    salas = []
    for f, sid, nombre, desc, lic in SALAS:
        if a.solo_libres and 'SA' in lic:
            continue
        path = os.path.join(src, 'Library', f + '.wav')
        if not os.path.exists(path):
            print('falta', f); continue
        x, sr = sf.read(path, dtype='float32', always_2d=True)
        if x.shape[1] == 1:
            x = np.repeat(x, 2, axis=1)
        x = x[:, :2]
        x = resample(x, sr)
        # DC and what is below 20 Hz go (zero phase): some measurements left an offset that was 96 % of a tail's energy
        from scipy.signal import butter, sosfiltfilt
        x = x - x.mean(axis=0, keepdims=True)
        x = sosfiltfilt(butter(4, 20.0, 'highpass', fs=48000, output='sos'), x, axis=0).astype(np.float32)
        peak = float(np.max(np.abs(x))) + 1e-12
        env = np.max(np.abs(x), axis=1)
        onset = int(np.argmax(env > peak * 0.05))
        x = x[max(0, onset - 24):]
        env = np.max(np.abs(x), axis=1)
        above = np.where(env > peak * 10 ** (-70 / 20))[0]
        end = int(above[-1]) + 1 if len(above) else len(x)
        x = x[:min(end, 12 * 48000)]                 # 12 s at most: past that a convolution costs much and adds little
        fade = min(len(x) // 4, 48000)
        if fade > 0:
            x[-fade:] *= np.linspace(1.0, 0.0, fade)[:, None] ** 2
        x = x / (np.max(np.abs(x)) + 1e-12) * 0.9
        rel = 'salas/%s.flac' % sid
        sf.write(os.path.join(a.root, rel), x, 48000, subtype='PCM_24')
        attrib = open(os.path.join(src, 'Library', f + '.attribution.txt'), encoding='utf-8', errors='replace').read()
        url = next((l.strip() for l in attrib.splitlines() if l.strip().startswith('http')), 'https://www.openair.hosted.york.ac.uk')
        salas.append(dict(id=sid, nombre=nombre, descripcion=desc, archivo=rel, licencia=lic, autor='OpenAIR, Universidad de York',
                          origen=url, via='https://github.com/andibrae/Reverb.js', segundos=round(len(x) / 48000, 2), t60=t60(x, 48000)))
        print('  sala %-32s %5.2f s  T60 %5.2f s  %s' % (nombre, len(x) / 48000, salas[-1]['t60'], lic))
    json.dump(dict(version=1, salas=salas), open(os.path.join(out, 'salas.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    # credits: the rooms' section of CREDITOS.txt (replaced each time)
    cred = os.path.join(a.root, 'CREDITOS.txt')
    text = open(cred, encoding='utf-8').read() if os.path.exists(cred) else ''
    text = text.split('\nSALAS REALES (reverb de convolución)')[0].rstrip('\n')
    lines = ['', '', 'SALAS REALES (reverb de convolución)',
             'Respuestas de impulso de OpenAIR (Universidad de York), tomadas de Reverb.js (https://github.com/andibrae/Reverb.js).',
             'Solo las de dominio público, CC BY y CC BY-SA; las "no comercial" o "sin derivados" se dejaron fuera.', '']
    for s_ in salas:
        lines.append('%s: %s, licencia %s, %s' % (s_['nombre'], s_['autor'], s_['licencia'], s_['origen']))
    open(cred, 'w', encoding='utf-8').write(text + '\n'.join(lines) + '\n')
    size = sum(os.path.getsize(os.path.join(a.root, s['archivo'])) for s in salas)
    print('salas: %d, %.1f MB' % (len(salas), size / 1e6))


if __name__ == '__main__':
    main()
