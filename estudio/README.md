# Racoon 1.0

El DAW de la familia Racoon. Producción por arreglo (línea de tiempo), mezcla y exportación. No tiene Session View.
Aloja VST3 y Audio Unit. Los plugins de la familia (todos se llaman "Racoon - ...") aparecen arriba del navegador: los diez en INSTRUMENTOS (Badger Bass, Hamster Drums, Dormouse Pads, Squirrel Sampler, Ferret Guitar, Porcupine Choir, Chipmunk Chords, Mole Rhythm, Skunk Sound Design y Chinchilla Vocals) y Hedgehog Mastering arriba de EFECTOS, porque va en el Master. Chipmunk crea su canal con Dormouse Pads detrás, Mole con Hamster Drums; Skunk y Chinchilla crean un canal de audio.
Trae 45 efectos propios, en español, que no son VST: viven dentro de Racoon y se guardan con el proyecto.

## Cómo se trabaja

Cada pista es un canal. Un canal tiene un instrumento y, encima, sus efectos, en el orden en que pasa el sonido.

1. El botón + debajo de las pistas crea un canal (también el clic derecho en el espacio vacío). Puedes arrastrar un instrumento desde la izquierda. En los canales solo hay instrumentos y sonidos: los retornos (Reverb, Delay) viven en MASTER, y ahí se crean con «+ Retorno».
2. Clic en el nombre de la pista: abajo se abre la pestaña CANAL con una tarjeta por dispositivo.
3. + EFECTO agrega un efecto al final. Para cambiar el orden, arrastra la barra de arriba de una tarjeta. Los canales también se reordenan arrastrando su nombre.
4. Clic derecho en cualquier perilla > «Automatizar en la línea de tiempo». Todos los parámetros de todos los efectos se pueden automatizar.
5. Lo que toca un efecto MIDI (Chipmunk, Mole) se pasa a un clip editable: clic derecho en su tarjeta > «Pasar a clip MIDI lo que toca».
6. En el piano roll, la REJILLA va de 4 compases a 1/64, con tresillos y modo libre. Los botones − y + la cambian, y el clic derecho en una nota tiene «doble golpe».

7. La pestaña MASTER es el mezclador: cada pista con su medidor y el número de lo más alto que ha sonado (clic para reiniciar), después los retornos y al final la salida.
8. Faders y perillas solo se mueven si los agarras: un clic al lado no les hace nada.

## Biblioteca Racoon

La primera vez que se abre, Racoon escribe su biblioteca en Música/Racoon/Biblioteca Racoon: 365 sonidos (unos 68 MB), todos sintetizados por Racoon, sin audio de terceros. En el navegador, pestaña SAMPLES, el botón Biblioteca te lleva ahí.

- One-shots: bombos (incluye 808 largos), cajas, palmas, charles cerrados y abiertos, rides y crashes, aros, shakers, congas, bongos, panderos, cencerros, bloques, claves y toms.
- Loops de dos compases, exactos para repetirse, en 12 estilos: Deep House, Organic House, House, Tech House, Techno, Afro House, Disco, Breakbeat, Hip Hop, Lo-fi, Trap y UK Garage. Cada estilo trae batería completa A y B, un loop top (sin bombo ni caja) y uno de percusión.
- Fills, bajos 808 y pulsados en las doce notas, y transiciones (subidas, bajadas, impactos, platillos al revés).

## Muestras Racoon

Aparte de la Biblioteca (que Racoon sintetiza), el paquete trae Muestras Racoon en la carpeta muestras: 3060 golpes reales de batería y percusión en 1389 sonidos, 76 instrumentos cromáticos (SFZ), 111 texturas, 21 loops, 129 voces sueltas y 5063 ondas, de 55 bibliotecas gratuitas que permiten redistribuir y modificar (CC0, dominio público, CC BY, The Unlicense y GPL). Cada fuente con su autor y su licencia está en muestras/CREDITOS.txt.

- La primera vez que abre, Racoon la copia a Música/Racoon/Muestras Racoon (unos 420 MB, en segundo plano), donde la encuentran los plugins en cualquier DAW.
- En el navegador, pestaña SAMPLES, el botón Muestras te lleva ahí. Los archivos se ven con su nombre (Frankensnare 14x5 arce, capa 2 de 3...) y las carpetas con su categoría.
- Al elegir un género, Hamster toca con el kit de muestras de ese estilo (Deep house, Techno, Trap duro, Reggaetón y dembow, Cumbia, Afro, Boom bap, Lo-fi, Disco, Synthpop 80...). Con RACOON_KIT_SINTETICO=1 vuelve al kit sintetizado.
- En los plugins: Hamster (MUESTRAS y KIT DE MUESTRAS, y 42 kits de fábrica «Muestras / ...»), Squirrel (instrumentos, golpes y kits en el teclado; ahora lee SFZ), Mole (su BATERÍA puede ser cualquier kit), Badger (ondas AKWF, tablas WaveEdit y bajos 808) y Skunk (texturas y voces como material).
- scripts/muestras tiene el programa que la arma desde las bibliotecas originales; ver su LEEME.txt.

## Los 45 efectos

- Dinámica: Compresor, Compresor de bus, Puerta, Limitador, Dinámica multibanda, Bus de batería.
- EQ y filtros: EQ de ocho bandas, EQ de tres bandas, EQ de canal, Filtro automático.
- Distorsión y color: Saturador, Amplificador, Gabinete, Válvula dinámica, Erosión, Overdrive, Pedal, Reductor, Vinilo.
- Modulación: Chorus y ensamble, Phaser y flanger, Paneo automático.
- Delay y eco: Delay, Eco, Delay filtrado, Delay granular, Repetidor, Looper, Alinear.
- Reverb y espacio: Reverb, Reverb híbrida, Cuerpo resonante, Resonadores.
- Creativos y espectrales: Resonador espectral, Tiempo espectral, Transpositor, Vocoder.
- Utilidades: Utilidad, Espectro, Afinador.
- Moduladores: LFO, Seguidor de envolvente, Forma (su curva se dibuja con el ratón). Van al final del canal; con MAPEAR eliges qué perillas mueven.
- Ruteo: Rack de efectos (cadenas en paralelo con 8 macros) y Efecto externo (saca el sonido por tu interfaz y lo regresa; «Medir el retraso» manda unos clics y corrige solo la latencia).

Cubren las familias de efectos de audio que trae un DAW completo: dinámica, ecualización, filtros, distorsión, retardos, reverberación, modulación y utilidades.

## Instalar en tu Mac

1. Descomprime la carpeta y haz doble clic en `Crear instalador.command`.
   Si macOS dice que no puede verificarlo: Ajustes del Sistema > Privacidad y seguridad > «Abrir de todos modos».
2. La primera vez descarga el motor de audio (unos 200 MB) y compila. Tarda bastante porque arma la versión universal (Apple Silicon e Intel).
3. Al terminar se abre la carpeta `dist` con `Racoon 1.0.dmg` y Racoon ya queda en Aplicaciones.
   El .dmg sirve para instalarlo en otras Macs: se abre, se arrastra Racoon a Aplicaciones y listo.

Si las carpetas `RacoonBadger`, `RacoonHamster` y `RacoonFamily` están junto a esta (y ya compilaste sus instaladores), o si los plugins ya están instalados en la Mac,
el instalador de Racoon los mete dentro de la app. Al abrir Racoon por primera vez los instala en `~/Library/Audio/Plug-Ins`, igual que hacen sus propias apps.

## Primer uso

Al abrir, Racoon pregunta qué vas a producir: 44 géneros en siete familias (House, Techno y trance, Bass music, Urbano y latino, Hip hop y R&B, Retro, pop y funk, Chill). Cada uno arma un bloque de 16 compases (4 frases de 4 compases) a su tempo, con su ritmo de batería, su bajo, su progresión y su kit de Hamster: el dembow del reggaetón, el medio tiempo del trap y el dubstep, la batería partida del drum & bass a 174, el 2-step del UK garage, el cencerro del phonk... Y cada género es otra rola, no la misma canción con otro ritmo: tiene su tonalidad, su progresión, su melodía y una voz cantada escrita para él (Chinchilla, con su nombre de archivo en 17 Voces/Canciones). Los 44 traen voz y todos los canales arrancan con su parte escrita; en techno, dubstep, drill, phonk, hardstyle y los demás de club la voz viene picada en sílabas cortas, como se usa ahí. Cada género está en su propia tonalidad de la rueda de Camelot (el picker la muestra al pasar el mouse, por ejemplo «La menor (8A)»), repartidas en las 12 menores de la rueda, tres o cuatro géneros en cada una; solo pop y disco quedan en mayor. Todo suena grave y oscuro: los pads van en el centro del piano, los acordes justo encima, la melodía y la voz no pasan de La4, y cada instrumento carga un preset elegido para la familia del género (Medianoche, Pozo y Bruma en Dormouse, coros lejanos en Porcupine, guitarras oscuras en Ferret). Y todo suena en esa tonalidad. El bajo, los pads, los acordes, la melodía, la guitarra, el coro y la voz usan solo notas de la escala; en los géneros menores que lo piden (trap, drill, cumbia, dembow, hardstyle...) el acorde V es mayor, con la sensible, como se toca ahí. Los acordes ya no se apilan en las mismas notas: los pads van en el centro y abiertos, los acordes de Chipmunk arriba y cerrados (sin la raíz cuando son de novena), la guitarra abajo, rasgueada en los géneros de guitarra y arpegiada en los demás, y el coro en su registro, y cada parte pasa de un acorde al siguiente moviendo las voces lo menos posible. Hay géneros con séptimas, con novenas, con sus2 o con quintas al aire. La voz y la textura también son MIDI: Chinchilla canta las notas de su clip con su propia voz («Cantar las notas MIDI») y Skunk toca un material de su biblioteca a la altura de las notas del suyo, así que los dos siguen el tempo de la rola y se editan como cualquier secuencia. La percusión con tono también se afina: Hamster mide su bombo, sus toms, congas, caja y cencerro y los mueve a la tonalidad (el bombo a la raíz o la quinta), Mole afina sus congas y toms, Hamster además pone su kit en notas General MIDI, así el charles del clip toca el charles de cualquier kit. «Sorpréndeme» elige uno al azar; «Proyecto vacío» empieza sin nada. Archivo > Nuevo vuelve a preguntar.

- Hamster lleva la batería, Badger el bajo, Dormouse los pads, Chipmunk los acordes, Squirrel la melodía, Ferret la guitarra, Porcupine el coro y Mole la percusión.
- Skunk suena sobre un loop de percusión de la biblioteca y Chinchilla sobre una voz cantada.
- Las frases cambian: la primera presenta, la segunda suma acordes y melodía, la tercera es el break (sin bombo y con guitarra y coro), la cuarta junta todo.
- En MASTER están los retornos (reverb y delay) y Hedgehog con la Partícula.
- Archivo > Abrir el último proyecto vuelve a lo que estabas haciendo. Archivo > Nuevo proyecto vacío empieza sin nada.

- Espacio reproduce. La zona rosa de la regla es el loop.
- Doble clic en un clip de un instrumento Racoon abre ese instrumento con las notas del clip cargadas en su propio secuenciador (el piano roll de abajo en Dormouse, Chipmunk, Squirrel, Ferret y Porcupine; el patrón en Hamster y Badger; los carriles dibujados en Mole), y el clip también se abre abajo. El botón del plugin cambia a «Devolver al clip»: un clic le regresa la secuencia a ese mismo clip, sin crear otro, y se deshace con Cmd+Z. Hamster y Mole toman hasta 8 compases y Badger hasta 4; lo que el clip tenga después se queda igual. Clic derecho en el clip > Editar en (su plugin) hace lo mismo. En clips de otros instrumentos, doble clic abre sus notas abajo. En Notas, un clip de Hamster o de Mole se ve por tambores con los nombres del kit (Bombo, Bombo 2, Caja, Palmas, Aro, Charles cerrado...), en el mismo orden que en la caja de ritmos, y ahí se pinta igual que en ella. Al abrir un clip, el editor se acomoda solo sobre sus notas. Doble clic en un hueco escribe una nota; doble clic en una nota la borra.
- En los clips de Hamster el editor muestra el nombre de cada sonido (Bombo, Caja, Palmas, Charles...).
- Arrastra Badger o Hamster desde la izquierda a la línea de tiempo para crear otra pista. Su canal aparece abajo, en CANAL.
- La barra de abajo explica lo que tienes bajo el ratón.
- Un clic en la maqueta (un clip o el carril vacío) muestra abajo todo lo que hay en ese canal, igual que un clic en su instrumento. Doble clic en un clip lo abre en su instrumento.
- En el bloque de inicio la percusión de Mole es un clip que se ve y se edita; Mole lo toca con su sonido (Reproducir: «Lo que dice la maqueta»).
- Selección y movimiento:
  - Shift o Cmd + clic suma clips a la selección; al arrastrar uno por su barra de nombre se mueven todos juntos, también a otros canales. Alt al arrastrar los copia.
  - El movimiento y el tamaño caen en las líneas que ves: con zoom lejano en compases, de cerca hasta el valor de REJILLA. Mientras arrastras, el clip muestra su inicio y su final (compás.tiempo.dieciseisavo). Cmd/Ctrl mueve sin rejilla. Las flechas mueven lo seleccionado una línea de la rejilla; con Shift, un compás.
  - Arrastrar sobre el carril o sobre el cuerpo de un clip (debajo de su nombre) elige un pedazo de tiempo en uno o varios canales, aunque corte clips a la mitad. Cmd+C lo copia, Cmd+X lo corta, Supr lo borra y Cmd+D lo repite justo después (vuelve a pulsar para seguir). Cmd+V pega en el canal y el compás donde hiciste clic. También desde el clic derecho.
- Clic derecho en un canal > Crear secuencia con (su plugin): se abre el plugin; escribe la secuencia ahí y pulsa «A la maqueta». El clip cae al final de ese canal, después de lo último que tenga, y el loop crece para incluirlo. El mismo botón CREAR SECUENCIA está arriba del canal, en CANAL. Con un instrumento que no es Racoon, Crear secuencia abre un clip vacío de 4 compases en el editor de notas.
- Clic derecho en el fader, el pan o un envío de cualquier canal (en MASTER o en la pista) > Ver automatización: abre su carril en la línea de tiempo para dibujar el cambio.
- Un .mid exportado desde un plugin Racoon (botón Exportar) se arrastra a la línea de tiempo: si se llama "Racoon Dormouse ...", abre un canal con Dormouse y el clip trae sus notas y su CC, que mueve las perillas que el plugin tenía automatizadas.

Arriba a la derecha está el selector **FÁCIL / COMPLETO**. En fácil se ve lo esencial. En completo aparecen los envíos a retornos,
la automatización (botón A de cada pista), congelar, pasar a audio, grupos, sidechain, fundidos de clip y salidas separadas de Hamster.

## Qué hace esta versión

- Línea de tiempo con pistas de audio, de instrumento, retornos y grupos (buses). Mover, recortar, dividir, duplicar (Alt+arrastrar), fundidos, rejilla, zoom, marcadores y loop.
- Piano roll con varias notas a la vez, velocidad, rejilla, cuantizar, duplicar, ayuda de escala y vista plegada.
- Grabación de MIDI (teclado) y de audio (entrada elegida por pista). Si un clip trae varias tomas, se elige la toma desde su clic derecho.
- Clips de audio: ganancia, transponer, seguir el tempo (warp por estiramiento, sin marcadores), invertir, fundidos.
- Mezclador: volumen, pan, medidores, insertos, envíos post-fader, master con insertos.
- Sidechain: clic derecho en un efecto que lo admita (por ejemplo el Compresor) > «Sidechain desde» una pista.
- Hamster con salidas separadas: clic derecho en su pista > «Separar salidas de Hamster». Crea ocho pistas, una por cada sonido del kit, con su propio canal en el mezclador.
- Congelar pista, pasar a audio (bounce) y consolidar.
- Deshacer y rehacer de todo, autoguardado cada minuto y recuperación al abrir.
- Plantillas propias (menú > Guardar como plantilla).
- Exportar WAV o FLAC (16, 24 o 32 bits; 44.1, 48 o 96 kHz), canción completa o solo el loop, normalizar, dither y stems.
- Escáner de plugins VST3 y AU en un proceso aparte, para que un plugin roto no tumbe Racoon.

## Lo que falta respecto a la especificación

- MP3: no se incluye el codificador por licencia. Exporta WAV o FLAC.
- Comping por carriles de tomas (hoy solo se elige la toma), punch in/out desde la interfaz, marcadores de warp.
- Expresión por nota, grooves y edición de varios clips a la vez en el piano roll.
- Proyectos portables: los audios importados se referencian donde estén; todavía no se copian dentro de la carpeta del proyecto.
- Windows (ASIO / WASAPI).
- Firma de Apple: la app va firmada «ad hoc». Para distribuirla sin el aviso de Gatekeeper hace falta una cuenta de desarrollador de Apple y notarizarla.
- Las pruebas de aceptación de carga (100 pistas, 25 VST3) no están medidas.
- El rack de efectos se abre en su propia ventana; dentro de él no se arrastran tarjetas, se mueven con las flechas < >.

## Licencia

Racoon usa Tracktion Engine, que se publica bajo GPLv3 o licencia comercial. Esta versión usa la GPLv3, así que Racoon también es GPLv3:
quien reciba la app tiene derecho a este código fuente (ver `LICENSE`). JUCE 8 se publica bajo AGPLv3, que es compatible con la GPLv3.
Si más adelante se quiere cerrar el código, hay que comprar la licencia comercial de Tracktion Engine y la de JUCE.
Las tipografías Dela Gothic One, Golos Text, Alumni Sans y JetBrains Mono (números de las pantallas) son SIL Open Font License.
Algunos algoritmos de edición y análisis (golpes, silencios, audio a MIDI, cuantizar) se reescribieron en C++ a partir de SoundCraft, que es MIT/Apache-2.0: el detalle está en `ATRIBUCIONES.txt`.
Badger y Hamster son plugins aparte que Racoon carga como VST3; no forman parte de este código.
Las muestras de Muestras Racoon son de terceros, cada una con su licencia libre (muestras/CREDITOS.txt y muestras/LICENCIAS).

## Para desarrollar

- `Source/engine`: el proyecto abierto (`Session`), catálogo de plugins, comportamiento del motor e instalador de la familia.
- `Source/ui`: interfaz con la marca (tema compartido con Badger y Hamster), línea de tiempo, piano roll, mezclador, navegador y paneles.
- `Source/fx`: los efectos nativos (`NativeFx` los registra; `effects/` tiene uno por archivo de grupo). `FxTest` los prueba todos.
- `Tests`: arnés sin ventana. Compila con `-DRACOON_TESTS=ON` y ejecuta `RacoonTest`
  (con `RACOON_PLUGIN_PATH` apuntando a las carpetas de los VST3 de Badger y Hamster prueba también el alojamiento y las salidas de Hamster).
- `scripts/fetch_deps.sh` descarga Tracktion Engine v3.2.0 y JUCE en la versión exacta que ese motor fija.
