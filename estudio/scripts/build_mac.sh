#!/bin/bash
# Racoon: compila el DAW en macOS, lo instala en Aplicaciones y arma el instalador .dmg.
# Uso:  bash scripts/build_mac.sh   (o doble click en "Crear instalador.command")
# Si encuentra los 11 plugins ya compilados (carpeta hermana plugins o RacoonFamily) o instalados en esta Mac,
# los mete dentro de Racoon.app: al abrir Racoon por primera vez se instalan solos.
set -e
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
CMAKE_VER="3.30.5"

echo ""
echo "=== RACOON / compilacion para macOS ==="
echo ""

if ! xcode-select -p >/dev/null 2>&1; then
  echo "Faltan las Command Line Tools de Xcode. Se abre el instalador de Apple."
  xcode-select --install || true
  echo "Cuando termine la instalacion, vuelve a correr este mismo comando."
  exit 1
fi

CMAKE="$(command -v cmake || true)"
if [ -z "$CMAKE" ]; then
  if [ ! -x "$ROOT/.tools/cmake/CMake.app/Contents/bin/cmake" ]; then
    echo "-> Descargando CMake $CMAKE_VER (portatil, no se instala en el sistema)"
    mkdir -p "$ROOT/.tools"
    curl -fL --progress-bar -o "$ROOT/.tools/cmake.tar.gz" \
      "https://github.com/Kitware/CMake/releases/download/v$CMAKE_VER/cmake-$CMAKE_VER-macos-universal.tar.gz"
    tar -xzf "$ROOT/.tools/cmake.tar.gz" -C "$ROOT/.tools"
    rm -rf "$ROOT/.tools/cmake"
    mv "$ROOT/.tools/cmake-$CMAKE_VER-macos-universal" "$ROOT/.tools/cmake"
  fi
  CMAKE="$ROOT/.tools/cmake/CMake.app/Contents/bin/cmake"
fi

bash "$ROOT/scripts/fetch_deps.sh"

echo "-> Configurando"
"$CMAKE" -S "$ROOT" -B "$ROOT/build-mac" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
echo "-> Compilando (la primera vez tarda bastante: el motor de audio es grande)"
"$CMAKE" --build "$ROOT/build-mac" --config Release --target Racoon -j "$(sysctl -n hw.ncpu)"

APP="$ROOT/build-mac/Racoon_artefacts/Release/Racoon.app"
codesign --force --deep -s - "$APP"

# Plugins de la familia para viajar dentro de la app
EXTRA=()
find_family () {
  local product="$1" target="$2"
  for folder in plugins RacoonFamily; do
    local c="$ROOT/../$folder/build-mac/${target}_artefacts/Release"
    if [ -d "$c/VST3/$product.vst3" ]; then
      EXTRA+=("$c/VST3/$product.vst3")
      if [ -d "$c/AU/$product.component" ]; then EXTRA+=("$c/AU/$product.component"); fi
      return 0
    fi
  done
  local lib="$HOME/Library/Audio/Plug-Ins"
  if [ -d "$lib/VST3/$product.vst3" ]; then
    EXTRA+=("$lib/VST3/$product.vst3")
    if [ -d "$lib/Components/$product.component" ]; then EXTRA+=("$lib/Components/$product.component"); fi
  fi
  return 0
}
for entry in Badger:Bass Hamster:Drums Dormouse:Pads Squirrel:Sampler Chipmunk:Chords Mole:Rhythm Skunk:Sound_Design \
             Ferret:Guitar Porcupine:Choir Hedgehog:Mastering Chinchilla:Vocals; do
  IFS=: read -r animal what <<< "$entry"
  find_family "Racoon - $animal ${what//_/ }" "Racoon$animal"
done
if [ ${#EXTRA[@]} -gt 0 ]; then
  echo "-> Plugins de la familia que viajan dentro de Racoon:"
  for e in "${EXTRA[@]}"; do echo "   $(basename "$e")"; done
else
  echo "-> No encontre plugins de la familia compilados: Racoon los usara si los instalas con sus propios .dmg"
fi

VERSION="$(grep -m1 -Eo 'project\(RACOON VERSION [0-9.]+' "$ROOT/CMakeLists.txt" | awk '{print $3}')"
VERSION="${VERSION%.0}"
if [ -z "$RACOON_MUESTRAS_DIR" ]; then
  # la completa si ya se armo en esta Mac; si no, la del paquete
  CANDIDATAS=("$ROOT/../muestras" "$HOME/Music/Racoon/Muestras Racoon")
  if grep -q '"perfil":"completo"' "$HOME/Music/Racoon/Muestras Racoon/catalogo.json" 2>/dev/null; then
    CANDIDATAS=("$HOME/Music/Racoon/Muestras Racoon")
  fi
  for m in "${CANDIDATAS[@]}"; do
    if [ -f "$m/catalogo.json" ]; then export RACOON_MUESTRAS_DIR="$(cd "$m" && pwd)"; break; fi
  done
fi
if [ -z "$RACOON_EFECTOS_DIR" ] && [ -f "$HOME/Music/Racoon/Efectos por género/.racoon-efectos-grabados" ]; then
  export RACOON_EFECTOS_DIR="$HOME/Music/Racoon/Efectos por género"
fi
echo "-> Armando el instalador .dmg"
if [ ${#EXTRA[@]} -gt 0 ]; then
  DMG="$(bash "$ROOT/scripts/make_dmg.sh" "$APP" "Racoon" "$VERSION" "$ROOT/dist" "${EXTRA[@]}" | tail -1)"
else
  DMG="$(bash "$ROOT/scripts/make_dmg.sh" "$APP" "Racoon" "$VERSION" "$ROOT/dist" | tail -1)"
fi

# Instala en esta Mac
echo "-> Instalando en Aplicaciones"
DEST="/Applications"; [ -w "$DEST" ] || DEST="$HOME/Applications"
mkdir -p "$DEST"
rm -rf "$DEST/Racoon.app"
hdiutil attach -nobrowse -quiet -mountpoint "$ROOT/build-mac/dmgmount" "$DMG"
ditto "$ROOT/build-mac/dmgmount/Racoon.app" "$DEST/Racoon.app"
hdiutil detach -quiet "$ROOT/build-mac/dmgmount"
xattr -dr com.apple.quarantine "$DEST/Racoon.app" 2>/dev/null || true
touch "$DEST/Racoon.app"

echo ""
echo "Instalador: $DMG"
echo "Instalado en: $DEST/Racoon.app"
echo ""
echo "Abre Racoon. La primera vez instala los plugins que viajan dentro y abre el proyecto de ejemplo Housito."
