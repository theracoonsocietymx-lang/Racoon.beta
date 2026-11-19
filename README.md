# Racoon

Racoon es un estudio para hacer música electrónica y una familia de 11 plugins (VST3) gratuitos. Lo hacemos para que cualquiera pueda producir sin piratear nada. Todo el código está aquí, con licencia libre, y las muestras que trae se pueden usar y repartir.

El historial de cambios, versión por versión, está en `LEEME.txt`.

## Qué hay en este repositorio

- `estudio/`: Racoon, el estudio (la app). C++ con JUCE 8 y Tracktion Engine 3.2.
- `plugins/`: los 11 plugins de la familia (Badger, Hamster, Dormouse, Squirrel, Ferret, Porcupine, Chipmunk, Mole, Skunk, Hedgehog y Chinchilla). C++ con JUCE 8.
- `muestras/`: Muestras Racoon, 3060 golpes de batería, 76 instrumentos SFZ y 5063 ondas de 55 bibliotecas gratuitas. El autor y la licencia de cada una están en `muestras/CREDITOS.txt`.
- `Instalar Racoon.command`: el instalador para Mac. Compila los plugins y el estudio y los deja en su lugar.

## Instalar en Mac

Doble clic en `Instalar Racoon.command`. Pide macOS 11 o más nuevo y las herramientas de línea de comandos de Xcode (`xcode-select --install`). La primera vez tarda porque compila todo en tu Mac.

## Licencia

- El estudio (`estudio/`) es GPL-3.0. El texto está en `LICENSE` y en `estudio/LICENSE`.
- Los plugins (`plugins/`) son AGPL-3.0, porque JUCE 8 se publica con esa licencia. El texto está en `plugins/LICENSE`.
- Las muestras conservan la licencia de su autor (CC0, dominio público, CC BY 3.0 y 4.0, The Unlicense y GPL). Los textos están en `muestras/LICENCIAS/`.

En corto, puedes usar Racoon, estudiarlo, cambiarlo y repartirlo, incluso cobrando. Si repartes una versión modificada, tiene que llevar su código fuente y la misma licencia. Nadie puede tomar Racoon y venderlo cerrado.

Las rolas que hagas con Racoon son tuyas. La licencia cubre el programa, no tu música.

## Cómo contribuir

Las aportaciones llegan por pull request. Cualquiera puede hacer un fork y cambiar su copia como quiera; para que un cambio entre a Racoon, lo revisa el equipo de The Racoon Society y decide si lo acepta.

### Antes de empezar

1. Revisa los issues abiertos. Si lo que quieres hacer es grande (un plugin nuevo, cambiar cómo se guarda un proyecto, otro sistema operativo), abre primero un issue y cuéntalo. Así nadie trabaja dos semanas en algo que no va a entrar.
2. Haz un fork y crea una rama con un nombre que diga qué hace: `arreglo/squirrel-sin-sonido`, `mejora/dj-tempo-swing`.
3. Trabaja sobre la última versión de `main`. Antes de mandar el pull request, trae los cambios nuevos de `main` a tu rama.

### Compilar y probar

Mac:

```
bash plugins/scripts/build_mac.sh
bash estudio/scripts/build_mac.sh
```

Linux, para correr las pruebas:

```
cd plugins
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DRF_TESTS=ON
cmake --build build
xvfb-run -a ./build/FamilyTest_artefacts/Release/FamilyTest

cd ../estudio
bash scripts/fetch_deps.sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DRACOON_TESTS=ON
cmake --build build
xvfb-run -a ./build/RacoonTest_artefacts/Release/RacoonTest
```

`RacoonTest` necesita `RACOON_PLUGIN_PATH` apuntando a las carpetas de los VST3 compilados, y `RACOON_MUESTRAS` apuntando a `muestras/`. Con `RACOON_SIN_UI=1` corre solo el motor y con `RACOON_ONLY_UI=1` solo la interfaz.

### Reglas para los pull requests

1. Un pull request, un cambio. Un arreglo de Hamster y una función nueva del modo DJ van en dos pull requests.
2. Todas las pruebas pasan: `FamilyTest` para los plugins y `RacoonTest` para el estudio. Si arreglas un error, agrega la prueba que lo habría atrapado.
3. Si cambias algo que se oye o se ve, explica en la descripción qué cambió y cómo probarlo. Una captura o un audio corto ayuda mucho.
4. Compila en Mac con Apple Silicon. En particular, no uses `jmin` ni `jmax` con argumentos `size_t`: en Apple Silicon chocan con `SIMDRegister` y el build falla. Usa `std::min` y `std::max`.
5. La interfaz está en español y en inglés. Cada texto nuevo que vea el usuario lleva su traducción en el diccionario de `I18n`.
6. El código usa el estilo que ya tiene: nombres en inglés o español según el archivo, comentarios en inglés, sin dependencias nuevas sin hablarlo antes en un issue.
7. Racoon es para música electrónica. No se aceptan géneros fuera de la electrónica ni sonidos que no vengan de ahí.
8. En el código, la interfaz y la documentación no se menciona otro software de producción por su nombre.
9. Muestras nuevas solo con licencia que permita repartir y modificar (CC0, dominio público, CC BY, The Unlicense, GPL). Nada con NC (no comercial) ni ND (sin derivadas). Agrega el autor, la licencia y la fuente a `muestras/CREDITOS.txt`.
10. Nada de llaves, tokens, contraseñas, rutas de tu computadora ni archivos `.env`. Si necesitas un dato privado para probar, va en una variable de entorno y no se sube.
11. Al mandar un pull request aceptas que tu código entre con la licencia de la carpeta donde cae (GPL-3.0 en `estudio/`, AGPL-3.0 en `plugins/`).

### Qué pasa después

El equipo revisa los pull requests abiertos cada semana. Puede pedirte cambios, aceptarlo o cerrarlo con una explicación. Un pull request sin respuesta del autor por 30 días se cierra; lo puedes volver a abrir cuando quieras.

## Reportar un error

Abre un issue con:

- qué hiciste, paso a paso;
- qué esperabas y qué pasó;
- tu sistema (por ejemplo macOS 14.5 en M2) y la versión de Racoon;
- si falló la instalación, el registro que deja el instalador (`paso.log`).

## Seguridad

Si encuentras un problema de seguridad, no abras un issue público. Escribe a theracoonsocietymx@gmail.com.
