#!/bin/bash
# Familia Racoon: compila, instala y empaqueta los plugins en macOS (VST3 + AU + app de cada uno).
# Uso:  bash scripts/build_mac.sh                 (todos)
#       bash scripts/build_mac.sh Dormouse Squirrel (solo esos)
# Deja un instalador por plugin en dist/: "Racoon <Animal> <version>.dmg".
set -e
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
JUCE_TAG="8.0.16-security"
CMAKE_VER="3.30.5"

# animal | codigo | tipo de Audio Unit | color del instalador | lo que es (va en el nombre; _ es espacio)
PLUGINS="
Badger:BsEn:aumu:rosa:Bass
Hamster:DrEn:aumu:menta:Drums
Dormouse:RcDm:aumu:menta:Pads
Squirrel:RcSq:aumu:limon:Sampler
Chipmunk:RcCp:aumu:rosa:Chords
Mole:RcMl:aumu:limon:Rhythm
Skunk:RcSn:aumf:rosa:Sound_Design
Ferret:RcFr:aumu:limon:Guitar
Porcupine:RcPr:aumu:menta:Choir
Hedgehog:RcHg:aufx:limon:Mastering
Chinchilla:RcCn:aumf:rosa:Vocals
"
ONLY=("$@")
wanted () {
  [ ${#ONLY[@]} -eq 0 ] && return 0
  for w in "${ONLY[@]}"; do [ "$w" = "$1" ] && return 0; done
  return 1
}

echo ""
echo "=== FAMILIA RACOON / compilacion para macOS ==="
echo ""

# 1. Herramientas de linea de comandos de Apple
if ! xcode-select -p >/dev/null 2>&1; then
  echo "Faltan las Command Line Tools de Xcode. Se abre el instalador de Apple."
  xcode-select --install || true
  echo "Cuando termine la instalacion, vuelve a correr este mismo comando."
  exit 1
fi

# 2. CMake: el del sistema o una copia portatil dentro del proyecto
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

# 3. JUCE
if [ ! -f "$ROOT/JUCE/CMakeLists.txt" ]; then
  rm -rf "$ROOT/JUCE"
  echo "-> Descargando JUCE $JUCE_TAG"
  git clone --depth 1 --branch "$JUCE_TAG" https://github.com/juce-framework/JUCE.git "$ROOT/JUCE"
fi

# 4. Compilacion universal (Apple Silicon + Intel)
ONLY_LIST=""
if [ ${#ONLY[@]} -gt 0 ]; then ONLY_LIST="$(IFS=';'; echo "${ONLY[*]}")"; fi
echo "-> Configurando"
"$CMAKE" -S "$ROOT" -B "$ROOT/build-mac" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DRF_ONLY="$ONLY_LIST"
echo "-> Compilando (la primera vez tarda: son varios plugins)"
"$CMAKE" --build "$ROOT/build-mac" --config Release -j "$(sysctl -n hw.ncpu)"

VERSION="$(grep -m1 -Eo 'project\(RACOON_FAMILY VERSION [0-9.]+' "$ROOT/CMakeLists.txt" | awk '{print $3}')"
VERSION="${VERSION%.0}"
VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$VST3_DIR" "$AU_DIR" "$HOME/Applications" "$ROOT/dist"
SUMMARY=()

for entry in $PLUGINS; do
  IFS=: read -r ANIMAL CODE AUTYPE TONE WHAT <<< "$entry"
  wanted "$ANIMAL" || continue
  NAME="Racoon - $ANIMAL ${WHAT//_/ }"
  ART="$ROOT/build-mac/Racoon${ANIMAL}_artefacts/Release"
  VST3="$ART/VST3/$NAME.vst3"; AU="$ART/AU/$NAME.component"; APP="$ART/Standalone/$NAME.app"
  echo ""
  echo "-> $NAME"

  # firma local (ad hoc) e instalacion en las carpetas del usuario
  for b in "$VST3" "$AU" "$APP"; do codesign --force --deep -s - "$b"; done
  # el nombre de antes (sin lo que es) se borra para que no aparezca dos veces
  OLD2="Racoon $ANIMAL ${WHAT//_/ }"
  rm -rf "$VST3_DIR/$NAME.vst3" "$AU_DIR/$NAME.component" \
         "$VST3_DIR/Racoon $ANIMAL.vst3" "$AU_DIR/Racoon $ANIMAL.component" "$HOME/Applications/Racoon $ANIMAL.app" \
         "$VST3_DIR/$OLD2.vst3" "$AU_DIR/$OLD2.component" "$HOME/Applications/$OLD2.app"
  cp -R "$VST3" "$VST3_DIR/"
  cp -R "$AU" "$AU_DIR/"
  xattr -dr com.apple.quarantine "$VST3_DIR/$NAME.vst3" "$AU_DIR/$NAME.component" 2>/dev/null || true

  # con el instalador unico (RACOON_TODO=1) todo viaja dentro de Racoon: no hace falta un .dmg ni una app por plugin
  DMG="-"
  if [ "${RACOON_TODO:-0}" != "1" ]; then
    rm -rf "$HOME/Applications/$NAME.app"
    cp -R "$APP" "$HOME/Applications/"
    xattr -dr com.apple.quarantine "$HOME/Applications/$NAME.app" 2>/dev/null || true
    LNAME="$(echo "$ANIMAL" | tr '[:upper:]' '[:lower:]')"
    DMG="$(bash "$ROOT/scripts/make_dmg.sh" "$APP" "$VST3" "$AU" "$NAME" "$VERSION" "$ROOT/dist" "$ROOT/Resources/dmg/$LNAME/background.png" | tail -1)"
  fi
  SUMMARY+=("$NAME|$AUTYPE|$CODE|$DMG")
done

# Audio Units: refresca la cache y valida cada uno con la herramienta de Apple
killall -9 AudioComponentRegistrar 2>/dev/null || true
echo ""
echo "-> Validando Audio Units (auval)"
for s in "${SUMMARY[@]}"; do
  IFS='|' read -r NAME AUTYPE CODE DMG <<< "$s"
  if auval -v "$AUTYPE" "$CODE" Dv20 > "$ROOT/build-mac/auval_$CODE.log" 2>&1; then
    echo "   $NAME: PASS"
  else
    echo "   $NAME: auval reporto un problema (build-mac/auval_$CODE.log)"
  fi
done

if [ "${RACOON_TODO:-0}" != "1" ]; then
  echo ""
  echo "Instaladores en dist/:"
  for s in "${SUMMARY[@]}"; do IFS='|' read -r NAME AUTYPE CODE DMG <<< "$s"; echo "  $(basename "$DMG")"; done
fi
echo ""
echo "Instalado en esta Mac:"
echo "  VST3:        $VST3_DIR"
echo "  Audio Unit:  $AU_DIR"
echo "  Apps:        $HOME/Applications (y /Applications si se pudo)"
echo ""
echo "En tu DAW, en los ajustes de plugins: activa 'Use VST3 Plug-in System Folders' y/o 'Use Audio Units v2', luego Rescan."
echo "Aparecen como Plug-Ins > Racoon > Racoon <Animal>. Racoon (el estudio) los encuentra solo."
