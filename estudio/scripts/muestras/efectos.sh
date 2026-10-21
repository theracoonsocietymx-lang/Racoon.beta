#!/bin/bash
# Efectos por género, la parte grabada: baja de GitHub vinilos, silbatos, sirenas, golpes de metal, láseres, voces,
# multitudes, whooshes y ambientes (bibliotecas CC0 y GPL) y los reparte en Música/Racoon/Efectos por género.
# La parte sintetizada la escribe Racoon solo la primera vez que abre. La llama "Instalar Racoon.command".
HERE="$(cd "$(dirname "$0")" && pwd)"
DEST="${1:-$HOME/Music/Racoon/Efectos por género}"
CACHE="$HOME/Library/Caches/Racoon"
[ "$(uname)" = "Darwin" ] || CACHE="$HOME/.cache/racoon"
PY="$CACHE/python"

if [ -f "$DEST/.racoon-efectos-grabados" ]; then
  echo "Los efectos grabados ya estaban en $DEST."
  exit 0
fi
if [ "$(uname)" = "Darwin" ] && ! xcode-select -p >/dev/null 2>&1; then
  echo "Faltan las herramientas de Xcode; los efectos grabados se bajan la proxima vez que corras el instalador."
  exit 2
fi
if ! curl -sSf --max-time 20 -o /dev/null https://raw.githubusercontent.com/sgossner/VCSL/HEAD/LICENSE 2>/dev/null; then
  echo "Sin internet: Racoon trae los efectos sintetizados; los grabados se bajan la proxima vez que corras el instalador."
  exit 2
fi
if [ ! -x "$PY/bin/python3" ]; then
  python3 -m venv "$PY" || { echo "No se pudo preparar Python."; exit 1; }
fi
"$PY/bin/python3" -m pip install --quiet --upgrade pip >/dev/null 2>&1
"$PY/bin/python3" -m pip install --quiet numpy scipy soundfile || { echo "No se pudieron instalar numpy, scipy y soundfile."; exit 1; }
echo "-> Bajando los efectos grabados (unos 200 MB, una sola vez)"
echo "== avance =="
mkdir -p "$DEST"
RACOON_MUESTRAS_TRABAJO="$CACHE/efectos-trabajo" "$PY/bin/python3" "$HERE/efectos.py" --root "$DEST" || exit 1
rm -rf "$CACHE/efectos-trabajo/descargas"
exit 0
