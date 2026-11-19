#!/usr/bin/env python3
# Construye Muestras Racoon desde las bibliotecas originales (GitHub): baja solo los archivos elegidos, mezcla micrófonos,
# recorta, iguala niveles, escribe FLAC y arma el catálogo.
#   python3 build.py                         perfil esencial (el que viene en el paquete, ~400 MB)
#   RACOON_PERFIL=completo python3 build.py  más capas, más alternancias y más notas (varios GB)
#   python3 build.py --dry                   solo cuenta lo que haría
# Necesita: pip install numpy scipy soundfile ; y git y curl en el sistema (ffmpeg es opcional).
import os, sys, json, time, argparse, traceback
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from concurrent.futures import ProcessPoolExecutor, as_completed
import model
from core import slug

from core import _BASE
ENTRY_CACHE = os.path.join(_BASE, 'entradas-' + os.environ.get('RACOON_PERFIL', 'esencial'))

def gather():
    import src_drums as D
    jobs = []
    def add(res):
        if isinstance(res, list):
            for r in res:
                add(r)
            return
        L, insts = res
        for i in insts:
            jobs.append((L, i))
    add(D.virtuosity()); add(D.virtuosity_perc_ranges()); add(D.frankensnare()); add(D.unruly()); add(D.big_rusty())
    add(D.swirly()); add(D.hat_phat()); add(D.body()); add(D.gogodze()); add(D.dim_cabasa()); add(D.muldjord())
    import src_melodic as S
    for L, i in S.all_instruments():
        jobs.append((L, i))
    import src_extra as X
    jobs += X.all_extra()
    try:
        import src_more as M
        for f in M.ALL:
            add(f())
    except ImportError:
        pass
    return jobs

def run_one(args):
    L, inst, root = args
    key = slug(inst.pack) + '__' + slug(inst.key)
    cp = os.path.join(ENTRY_CACHE, key + '.json')
    if os.path.exists(cp):
        ents = json.load(open(cp))
        if all(os.path.exists(os.path.join(root, e['archivo'])) for e in ents):
            return ents
    try:
        ents = inst.render(L, root) if hasattr(inst, 'render') else model.render(inst, L, root)
    except Exception:
        traceback.print_exc()
        return []
    os.makedirs(ENTRY_CACHE, exist_ok=True)
    json.dump(ents, open(cp, 'w'))
    return ents

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dry', action='store_true')
    ap.add_argument('--only', default='')
    ap.add_argument('--root', default=os.path.join(os.getcwd(), 'Muestras Racoon'))
    ap.add_argument('--workers', type=int, default=6)
    a = ap.parse_args()
    jobs = gather()
    if a.only:
        want = [w.strip().lower() for w in a.only.split(',')]
        jobs = [j for j in jobs if any(w in j[1].pack.lower() for w in want)]
    from collections import Counter
    cats = Counter(j[1].cat for j in jobs)
    packs = Counter(j[1].pack for j in jobs)
    est = sum(len(j[1].choose()) if hasattr(j[1], 'choose') else (len(model.select(j[1])) if not getattr(j[1], 'measure', None) else min(j[1].nvel - 1 + j[1].nrr, len(j[1].members))) for j in jobs)
    print('instrumentos: %d, muestras estimadas: %d' % (len(jobs), est))
    print('paquetes:', dict(packs))
    print('categorías:', dict(cats))
    if a.dry:
        for L, i in jobs[:4000]:
            print('  %-14s %-16s %-60s capas/rr=%d' % (i.pack, i.cat, i.name[:60], len(i.members)))
        return
    t0 = time.time()
    entries = []
    with ProcessPoolExecutor(a.workers) as ex:
        futs = [ex.submit(run_one, (L, i, a.root)) for L, i in jobs]
        for n, f in enumerate(as_completed(futs)):
            entries += f.result()
            if n % 25 == 0 or n + 1 == len(futs):
                # "[ 45%]" como al compilar: la ventana de progreso del instalador lo lee
                print('[%3d%%] %d/%d  %.0fs  muestras=%d' % (int(100 * (n + 1) / len(futs)), n + 1, len(futs), time.time() - t0, len(entries)), flush=True)
    entries.sort(key=lambda e: e['id'])
    if a.only:
        # con --only se rehacen esos paquetes y se conservan los demás en el catálogo
        prev = os.path.join(a.root, '.entradas.json')
        if os.path.exists(prev):
            done = set(e['id'] for e in entries)
            packs = set(e['pack'] for e in entries)
            entries += [e for e in json.load(open(prev)) if e['id'] not in done and e['pack'] not in packs]
            entries.sort(key=lambda e: e['id'])
    json.dump(entries, open(os.path.join(a.root, '.entradas.json'), 'w'), ensure_ascii=False)
    print('listo: %d muestras en %.0fs' % (len(entries), time.time() - t0))
    import finalize
    finalize.ROOT = a.root
    finalize.main()

if __name__ == '__main__':
    main()
