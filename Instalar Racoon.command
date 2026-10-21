#!/bin/bash
# Doble clic: compila e instala todo Racoon en esta Mac.
# Los 11 plugins (Badger, Hamster, Dormouse, Squirrel, Chipmunk, Mole, Skunk, Ferret, Porcupine, Hedgehog, Chinchilla)
# y Racoon, el estudio, con los 11 adentro. Al final deja un solo instalador .dmg para otras Macs.
cd "$(dirname "$0")"
TOP="$(pwd)"
export RACOON_TODO=1
STATUS=0

# Ventana de progreso (si esta Mac ya tiene las herramientas de Xcode; si no, todo sigue aqui en la Terminal)
PROG="$HOME/Library/Caches/Racoon/instalacion"
mkdir -p "$PROG"
ESTADO="$PROG/estado.txt"; PASO_LOG="$PROG/paso.log"
paso() { echo "$1|$2|$3" > "$ESTADO"; : > "$PASO_LOG"; }
paso 0 0.01 "Preparando"
if xcode-select -p >/dev/null 2>&1 && [ -f "$TOP/estudio/scripts/progreso.swift" ]; then
  ( swiftc -O -o "$PROG/progreso" "$TOP/estudio/scripts/progreso.swift" >/dev/null 2>&1 && "$PROG/progreso" "$ESTADO" "$PASO_LOG" $$ >/dev/null 2>&1 ) &
fi

echo ""
echo "=== Muestras Racoon: copiando las muestras a Musica/Racoon ==="
paso 0.01 0.03 "Copiando Muestras Racoon a Musica/Racoon"
if [ -f "$TOP/muestras/catalogo.json" ]; then
  DEST="$HOME/Music/Racoon/Muestras Racoon"
  mkdir -p "$DEST"
  if grep -q '"perfil":"completo"' "$DEST/catalogo.json" 2>/dev/null; then
    echo "Ya esta la biblioteca completa en $DEST"
  else
    rsync -a --delete "$TOP/muestras/" "$DEST/" && echo "Listas en $DEST"
  fi
fi

echo ""
echo "=== Modelo para Separar en stems (81 MB, una sola vez) ==="
paso 0.04 0.02 "Bajando el modelo para separar en stems"
MODEL="$HOME/Music/Racoon/Modelos/ggml-model-htdemucs-4s-f16.bin"
if [ ! -s "$MODEL" ]; then
  mkdir -p "$(dirname "$MODEL")"
  if curl -L --fail --progress-bar -o "$MODEL.descargando" "https://huggingface.co/datasets/Retrobear/demucs.cpp/resolve/main/ggml-model-htdemucs-4s-f16.bin"; then
    mv "$MODEL.descargando" "$MODEL" && echo "Listo en $MODEL"
  else
    rm -f "$MODEL.descargando"
    echo "No se pudo descargar ahora. Racoon lo pide la primera vez que separes una cancion."
  fi
else
  echo "Ya estaba."
fi

echo ""
echo "=== Muestras Racoon completa (pianos, orquesta, baterias y guitarras con todas sus capas) ==="
paso 0.06 0.30 "Bajando y armando Muestras Racoon completa"
bash "$TOP/estudio/scripts/muestras/completa.sh" 2>&1 | tee -a "$PASO_LOG"

echo ""
echo "=== Efectos por genero (vinilos, sirenas, silbatos, laseres, voces y ambientes grabados) ==="
paso 0.36 0.04 "Bajando los efectos por genero"
bash "$TOP/estudio/scripts/muestras/efectos.sh" 2>&1 | tee -a "$PASO_LOG"

echo ""
echo "=== 1 de 2: los 11 plugins ==="
paso 0.40 0.34 "Paso 1 de 2: compilando los 11 plugins"
bash "$TOP/plugins/scripts/build_mac.sh" 2>&1 | tee -a "$PASO_LOG"
if [ "${PIPESTATUS[0]}" -ne 0 ]; then STATUS=1; fi

if [ $STATUS -eq 0 ]; then
  echo ""
  echo "=== 2 de 2: Racoon, el estudio ==="
  paso 0.74 0.26 "Paso 2 de 2: compilando Racoon, el estudio"
  bash "$TOP/estudio/scripts/build_mac.sh" 2>&1 | tee -a "$PASO_LOG"
  if [ "${PIPESTATUS[0]}" -ne 0 ]; then STATUS=1; fi
fi

echo ""
if [ $STATUS -eq 0 ]; then
  mkdir -p "$TOP/Instalador"
  cp -f "$TOP"/estudio/dist/*.dmg "$TOP/Instalador/" 2>/dev/null || true
  paso 1 0 "FIN"
  echo "Listo. Racoon esta en Aplicaciones con los 11 plugins, y tu DAW tambien los ve."
  echo "Para otra Mac usa el .dmg de la carpeta Instalador: trae todo junto."
  open "$TOP/Instalador" 2>/dev/null || true
  open -a Racoon 2>/dev/null || true
else
  paso 0 0 "ERROR"
  echo "Algo fallo. Copia el texto de arriba y mandamelo."
fi
echo ""
read -n 1 -s -r -p "Presiona cualquier tecla para cerrar esta ventana."
exit $STATUS
