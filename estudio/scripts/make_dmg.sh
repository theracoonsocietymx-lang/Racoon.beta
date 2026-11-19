#!/bin/bash
# Arma el instalador .dmg de Racoon: la app (con los plugins de la familia adentro, si se pasan) y un acceso a Aplicaciones.
# Uso: bash scripts/make_dmg.sh <Racoon.app> "<Nombre>" <version> <carpeta_salida> [plugin.vst3 plugin.component ...]
set -e
APP="$1"; NAME="$2"; VERSION="$3"; OUTDIR="$4"; shift 4
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
BG="$ROOT/Resources/dmg/background.png"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
STAGE_APP="$WORK/$NAME.app"
ditto "$APP" "$STAGE_APP"
if [ $# -gt 0 ]; then
  PLUG="$STAGE_APP/Contents/Resources/Plugins"
  mkdir -p "$PLUG"
  for b in "$@"; do
    ditto "$b" "$PLUG/$(basename "$b")"
    codesign --force --deep -s - "$PLUG/$(basename "$b")"
  done
fi
# Muestras Racoon viaja dentro de la app: al abrirla por primera vez se copia a Musica/Racoon
if [ -n "$RACOON_MUESTRAS_DIR" ] && [ -f "$RACOON_MUESTRAS_DIR/catalogo.json" ]; then
  echo "   (con Muestras Racoon adentro: $(du -sh "$RACOON_MUESTRAS_DIR" | cut -f1))" >&2
  ditto "$RACOON_MUESTRAS_DIR" "$STAGE_APP/Contents/Resources/Muestras Racoon"
  rm -f "$STAGE_APP/Contents/Resources/Muestras Racoon/.entradas.json"
fi
# los efectos por genero grabados (el instalador los bajo a Musica/Racoon); Racoon sintetiza el resto al abrir
if [ -n "$RACOON_EFECTOS_DIR" ] && [ -f "$RACOON_EFECTOS_DIR/.racoon-efectos-grabados" ]; then
  echo "   (con Efectos por genero adentro: $(du -sh "$RACOON_EFECTOS_DIR" | cut -f1))" >&2
  ditto "$RACOON_EFECTOS_DIR" "$STAGE_APP/Contents/Resources/Efectos por género"
fi
# firma ad hoc: lo de adentro primero, despues la app (sella los recursos)
codesign --force --deep -s - "$STAGE_APP"
xattr -cr "$STAGE_APP" 2>/dev/null || true

mkdir -p "$OUTDIR"
OUT="$OUTDIR/$NAME $VERSION.dmg"
rm -f "$OUT"
VOL="$NAME $VERSION"

if ! python3 -c "import dmgbuild" 2>/dev/null; then
  python3 -m pip install --user --quiet dmgbuild 2>/dev/null || python3 -m pip install --user --quiet --break-system-packages dmgbuild 2>/dev/null || true
fi
if python3 -c "import dmgbuild" 2>/dev/null && \
   python3 -m dmgbuild -s "$HERE/dmg_settings.py" -D app="$STAGE_APP" -D bg="$BG" "$VOL" "$OUT" >/dev/null; then
  :
else
  echo "   (dmgbuild no disponible, se arma el instalador sin fondo)"
  STAGE="$WORK/stage"; mkdir -p "$STAGE"
  mv "$STAGE_APP" "$STAGE/"
  ln -s /Applications "$STAGE/Aplicaciones"
  hdiutil create -srcfolder "$STAGE" -volname "$VOL" -fs HFS+ -format UDZO -ov "$OUT" >/dev/null
fi
echo "$OUT"
