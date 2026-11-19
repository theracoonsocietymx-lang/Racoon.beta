#!/bin/bash
# Muestras Racoon completa: arma la biblioteca grande en esta Mac, bajando cada muestra de su biblioteca original
# en GitHub (6 capas de fuerza, 4 alternancias, 30 notas por instrumento, pianos de hasta 10 segundos).
# La llama "Instalar Racoon.command"; se puede correr sola. Si se corta, al volver a correrla sigue donde se quedó.
# Uso: bash completa.sh ["/ruta/Muestras Racoon"]
HERE="$(cd "$(dirname "$0")" && pwd)"
DEST="${1:-$HOME/Music/Racoon/Muestras Racoon}"
CACHE="$HOME/Library/Caches/Racoon"
[ "$(uname)" = "Darwin" ] || CACHE="$HOME/.cache/racoon"
WORK="$CACHE/muestras-trabajo"
STAGE="$DEST (armando)"
PY="$CACHE/python"

if grep -q '"perfil":"completo"' "$DEST/catalogo.json" 2>/dev/null; then
  echo "La biblioteca completa ya estaba en $DEST."
  exit 0
fi

if [ "$(uname)" = "Darwin" ] && ! xcode-select -p >/dev/null 2>&1; then
  echo "Faltan las herramientas de Xcode; la biblioteca completa se arma la proxima vez que corras el instalador."
  exit 2
fi

# espacio: unos 5 GB de descargas que se borran al final y 1 GB que se queda
FREE_GB=$(df -Pk "$HOME" | awk 'NR==2 {print int($4/1048576)}')
if [ "${FREE_GB:-0}" -lt "${RACOON_MIN_GB:-12}" ]; then
  echo "Hay ${FREE_GB} GB libres y la biblioteca completa necesita 12 GB mientras se arma. Se queda la biblioteca normal."
  exit 2
fi
if ! curl -sSf --max-time 20 -o /dev/null https://raw.githubusercontent.com/sgossner/VCSL/HEAD/LICENSE 2>/dev/null; then
  echo "Sin internet: se queda la biblioteca normal. Vuelve a correr el instalador con internet para la completa."
  exit 2
fi

echo "-> Preparando Python (numpy, scipy y soundfile, solo para armar la biblioteca)"
if [ ! -x "$PY/bin/python3" ]; then
  python3 -m venv "$PY" || { echo "No se pudo preparar Python."; exit 1; }
fi
"$PY/bin/python3" -m pip install --quiet --upgrade pip >/dev/null 2>&1
"$PY/bin/python3" -m pip install --quiet numpy scipy soundfile || { echo "No se pudieron instalar numpy, scipy y soundfile."; exit 1; }

echo "-> Bajando y armando las muestras (la primera vez, de 20 a 60 minutos segun tu internet)"
echo "== avance =="
mkdir -p "$STAGE"
CPUS=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
if ! RACOON_PERFIL=completo RACOON_MUESTRAS_TRABAJO="$WORK" "$PY/bin/python3" "$HERE/build.py" --root "$STAGE" --workers "$CPUS"; then
  echo "La biblioteca completa no se termino de armar. Se queda la normal; al volver a correr el instalador sigue donde se quedo."
  exit 1
fi
if [ ! -f "$STAGE/catalogo.json" ]; then
  echo "La biblioteca completa no se termino de armar. Se queda la normal."
  exit 1
fi
rm -f "$STAGE/.entradas.json.tmp"

# cambia la normal por la completa de un jalon
OLD="$DEST (anterior)"
rm -rf "$OLD"
[ -d "$DEST" ] && mv "$DEST" "$OLD"
mv "$STAGE" "$DEST" && rm -rf "$OLD"
rm -rf "$WORK/descargas"
echo "Lista: $(du -sh "$DEST" | cut -f1) en $DEST"
exit 0
