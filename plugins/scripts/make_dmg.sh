#!/bin/bash
# Arma el instalador .dmg: la app con el VST3 y el AU adentro, mas un acceso a Aplicaciones.
# Al abrir la app por primera vez, ella misma instala los plugins en ~/Library/Audio/Plug-Ins.
# Uso: bash scripts/make_dmg.sh <App.app> <Plugin.vst3> <Plugin.component> "<Nombre>" <version> <carpeta_salida> [fondo.png]
set -e
APP="$1"; VST3="$2"; AU="$3"; NAME="$4"; VERSION="$5"; OUTDIR="$6"; BG="$7"
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
STAGE_APP="$WORK/$NAME.app"
ditto "$APP" "$STAGE_APP"
PLUG="$STAGE_APP/Contents/Resources/Plugins"
mkdir -p "$PLUG"
ditto "$VST3" "$PLUG/$(basename "$VST3")"
ditto "$AU" "$PLUG/$(basename "$AU")"
# firma ad hoc: primero lo de adentro, despues la app (sella los recursos)
codesign --force --deep -s - "$PLUG/$(basename "$VST3")"
codesign --force --deep -s - "$PLUG/$(basename "$AU")"
codesign --force -s - "$STAGE_APP"
xattr -cr "$STAGE_APP" 2>/dev/null || true

mkdir -p "$OUTDIR"
OUT="$OUTDIR/$NAME $VERSION.dmg"
rm -f "$OUT"
VOL="$NAME $VERSION"

# dmgbuild arma la ventana con fondo y posiciones sin necesitar el Finder (sirve tambien en GitHub)
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
