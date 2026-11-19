#!/bin/bash
# Descarga el motor de audio (Tracktion Engine v3.2.0) y la version exacta de JUCE que ese motor usa.
# Se llama desde build_mac.sh; no hace nada si ya estan descargados.
set -e
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
TE_TAG="v3.2.0"
JUCE_SHA="19edd538429c93d277bf95b55aaa7e3eb545f951"   # JUCE 8.0.6, el que fija Tracktion Engine v3.2.0

if [ ! -f "$ROOT/tracktion_engine/modules/CMakeLists.txt" ]; then
  echo "-> Descargando Tracktion Engine $TE_TAG"
  rm -rf "$ROOT/tracktion_engine"
  git clone --depth 1 --branch "$TE_TAG" https://github.com/Tracktion/tracktion_engine.git "$ROOT/tracktion_engine"
fi

if [ ! -f "$ROOT/tracktion_engine/modules/juce/CMakeLists.txt" ]; then
  echo "-> Descargando JUCE (version fijada por el motor)"
  J="$ROOT/tracktion_engine/modules/juce"
  rm -rf "$J"; mkdir -p "$J"
  git -C "$J" init -q
  git -C "$J" remote add origin https://github.com/juce-framework/JUCE.git
  if ! git -C "$J" fetch -q --depth 1 origin "$JUCE_SHA"; then
    git -C "$J" fetch -q origin
  fi
  git -C "$J" checkout -q "$JUCE_SHA"
fi
echo "   motor listo"
