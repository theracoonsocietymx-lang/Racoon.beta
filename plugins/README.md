# Familia Racoon

Los plugins de la familia Racoon que viven fuera del DAW. Comparten el mismo marco: la ventana con la marca, presets de fábrica y propios, comparación A/B, capa FÁCIL y capa COMPLETO, ayuda al pasar el ratón y el instalador que se instala solo.

Todos funcionan igual en la parte de abajo, para que aprender uno sea aprenderlos todos:

- Play: toca la secuencia del plugin sin DAW. Cada plugin trae un ejemplo cargado, así que al abrirlo y darle Play ya suena. Los efectos (Skunk, Hedgehog, Chinchilla) traen un sonido de ejemplo que pasa por ellos: una mezcla corta o una voz.
- Deshacer y rehacer, para perillas y notas.
- La secuencia: un piano roll de 1 a 8 compases con tempo y rejilla; funciona como el Piano de Racoon: clic selecciona (Mayús suma), arrastrar en un hueco selecciona con un recuadro, arrastrar mueve las seleccionadas, el borde alarga, Alt+arrastrar copia, doble clic escribe o borra, flechas transponen (Mayús: octava), Cmd+Mayús+flecha copia una octava arriba o abajo y la tecla B pone el lápiz (un clic escribe). Mole usa su propia caja de ritmos de 16 carriles en lugar del piano roll.
- El teclado, con Oct - y Oct +, y las letras de la compu.
- Empieza aquí: cuatro pasos para que suene y para cambiarlo.
- A la maqueta: pasa la secuencia (con su automatización) a la rola. Dentro de Racoon cae como clip al final del canal del plugin, después de lo último que haya, así que nada se borra y la rola se arma loop por loop. En otro DAW se arrastra el botón a una pista. Al pasarla, el plugin deja de tocarla por su cuenta para que no suene doble: Badger apaga su secuenciador, Hamster deja de seguir al DAW (su patrón sigue sonando con su PLAY) y Mole deja su ritmo como clip en su canal y pasa a Reproducir «Lo que dice la maqueta» (toca las notas del clip con su sonido, sin generar). Hedgehog no lo tiene porque vive en el Master. Si el plugin se abrió con doble clic en un clip de Racoon, trae ese clip cargado y el botón dice «Devolver al clip»: la secuencia regresa a ese mismo clip. Mientras editas un clip, Badger solo toca su patrón con su PLAY y Mole toca sus carriles con el Play de abajo; con la canción suena el clip.
- Exportar: un clic guarda la secuencia como .mid en Música/Racoon/Patrones; arrastrar el botón la suelta en tu DAW o en Racoon. Chipmunk y Mole exportan lo que de verdad tocan (los acordes, el ritmo), una frase de 4 compases como mínimo. Badger y Hamster tienen su EXPORTAR arriba, junto a Play.
- Automatización: clic derecho en cualquier perilla, menú o botón > Ver automatización. Abajo aparece su línea sobre un loop de 1, 2, 4, 8 o 16 compases: clic pone un punto, arrastrar dibuja, doble clic borra un punto. Grabar escribe lo que mueves con el ratón mientras suena. La línea suena con Play y con el transporte de tu DAW; mientras sostienes la perilla, manda tu mano. Una perilla con línea lleva un punto rosa.

Cada línea viaja en el .mid exportado como MIDI CC (la primera como CC 20, la siguiente CC 21...). El plugin escucha ese CC: el mismo .mid tocando en la pista del plugin mueve las perillas igual, en tu DAW (como envolvente de MIDI CC del clip) y en Racoon. La automatización se guarda con el proyecto y se deshace con deshacer.

Las ventanas abren al ancho de la pantalla y a la altura que cabe arriba del Dock; si algo no cabe, se desplaza con la rueda. Chipmunk y Mole tocan con su propio sonido (se apaga con «Sonido propio» para mandar su MIDI a otro instrumento).

| Plugin | Qué es | Tipo |
| --- | --- | --- |
| Racoon - Dormouse Pads | Sinte de pads: wavetables, unison, textura granular, 4 envolventes, 4 LFO | Instrumento |
| Racoon - Squirrel Sampler | Sampler: zonas por nota y velocidad, loops, round robin, cortes por transitorios | Instrumento |
| Racoon - Ferret Guitar | Guitar Lab: guitarra modelada (7 instrumentos, 7 afinaciones), guitarrista de ritmo con 7 estilos, arpegios, 10 articulaciones, pedalera de 8, amplificador de 6 modelos y gabinete con micrófonos | Instrumento |
| Racoon - Porcupine Choir | Choir & Ensemble: coro SATB con conducción de voces, armonizador de melodía, vocales y sílabas, cuerdas y metales, escenario y 8 salidas | Instrumento |
| Racoon - Chipmunk Chords | Acordes y arpegios: recibe notas y entrega acordes, arpegios y strum, con su propio sonido | Instrumento (también manda MIDI) |
| Racoon - Mole Rhythm | Groove & Rhythm Lab: 16 carriles euclidianos o dibujados, probabilidad, ratchets, grooves aprendidos y rebanador; toca con su propia batería | Instrumento (también manda MIDI) |
| Racoon - Skunk Sound Design | Sound Design Lab: captura, granular, espectro congelado, morphing, glitch y una matriz de 64 rutas | Efecto |
| Racoon - Hedgehog Mastering | Masterizador para el Master. Su primera pantalla es la Partícula: entrada con marca verde, EQ previo (graves, medios, agudos), una perilla de CANTIDAD de 0 a 200 % que mueve la compresión de tres bandas, la saturación, el aire y el ancho, sidechain interno o externo, limitador adaptativo y salida. Debajo sigue el laboratorio: EQ dinámica de 12 bandas, multibanda de 6, resonancias, match EQ, LUFS, referencias, asistente y control de calidad | Efecto (va en el Master) |
| Racoon - Chinchilla Vocals | Vocal Lab: afinación, editor de notas, formantes, 8 armonías, doble, vocoder, talkbox, robot, chopper y matriz | Efecto (con MIDI y sidechain) |
| Racoon - Badger Bass | Sinte de bajo ácido con secuenciador y piano roll | Instrumento |
| Racoon - Hamster Drums | Caja de ritmos de 16 pistas con salidas separadas | Instrumento |

Son once plugins, y Racoon (el estudio) completa los doce. Todos empiezan con "Racoon - " para que salgan juntos en Aplicaciones y en la lista de plugins, y el nombre dice el animal y lo que hace. Los nombres de antes (Racoon Badger, Racoon Badger Bass...) se borran al instalar para que no aparezcan dos veces.

Los seis efectos que antes estaban aquí (compresor, saturador, filtro, EQ, delay y reverb) ya no son VST: viven dentro de Racoon como efectos nativos, junto con los otros 39 de su lista.

Códigos (para Audio Unit y VST3): Dormouse RcDm, Squirrel RcSq, Chipmunk RcCp, Mole RcMl, Skunk RcSn, Ferret RcFr, Porcupine RcPr, Hedgehog RcHg, Chinchilla RcCn.

## Instalar en una Mac

Doble clic en `Crear instalador.command`. Descarga lo que falte (JUCE y, si no lo tienes, CMake), compila los nueve, los instala en esta Mac y deja un instalador `.dmg` por plugin en `dist/`.

Para instalar solo algunos: `bash scripts/build_mac.sh Dormouse Squirrel`.

Cada `.dmg` sirve para otra Mac sin compilar: se abre, se arrastra la app a Aplicaciones y se abre una vez. La app instala sola el VST3 y el Audio Unit.

Dónde queda cada cosa:

- VST3: `~/Library/Audio/Plug-Ins/VST3/Racoon <Animal>.vst3`
- Audio Unit: `~/Library/Audio/Plug-Ins/Components/Racoon <Animal>.component`
- App independiente: `/Applications` o, si esa carpeta no deja escribir, `~/Applications` (la carpeta Aplicaciones dentro de tu usuario)

Los plugins no tienen icono en el Dock ni en Launchpad: viven dentro de tu DAW. Para verlos, en los ajustes de plugins de tu DAW activa las carpetas de sistema de VST3 o Audio Units y vuelve a buscar plugins (Rescan). Aparecen bajo Racoon.

Racoon (el estudio) los encuentra solo, sin escanear.

## Chipmunk es un efecto MIDI

Chipmunk va antes del instrumento. En Racoon se arrastra a una pista de instrumento y queda antes del sinte. En otros DAWs va en la ranura de efectos MIDI (Audio Unit) cuando el DAW la tiene. Algunos DAWs no cargan efectos MIDI de terceros en la cadena de un instrumento, y ahí no se usa igual que en Racoon.

## Compilar en Linux (pruebas)

```
git clone --depth 1 --branch 8.0.16-security https://github.com/juce-framework/JUCE.git JUCE
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DRF_TESTS=ON
cmake --build build
xvfb-run -a ./build/FamilyTest_artefacts/Release/FamilyTest            # todos
xvfb-run -a ./build/FamilyTest_artefacts/Release/FamilyTest Squirrel   # uno
```

`BadgerTest` y `HamsterTest` prueban la automatización y la exportación de esos dos.

`FamilyTest` corre el contrato común con cada plugin (bloques de 1 a 1024 a 44.1, 48 y 96 kHz, parámetros al azar, guardar y abrir, presets, bypass sin clic, la automatización (que mueve el parámetro con Play, que respeta la mano, que graba, que sigue su CC, que se guarda, que se deshace y que viaja en el .mid), la ventana en FÁCIL y COMPLETO) y las pruebas de cada uno que pide su ficha: reproducción bit-perfect, notas sin colgar, etc.

## Cómo se usan los nuevos

- Ferret: toca un acorde y el guitarrista lo rasguea al tempo de la canción. Las notas MIDI 24 a 33 (C1 a A1 contando C4 = 60; C0 a A0 en DAWs que llaman C3 al Do central) cambian la articulación: sostenido, palm mute, medio mute, nota muerta, slide, hammer-on, vibrato, bend, armónico y raspado. En modo Instrumento cada nota cae en una cuerda real. En la pantalla se ve el diapasón y el patrón; el estilo Usuario se edita paso por paso (64 pasos). EXPORTAR MIDI guarda la guitarra de los acordes que tocaste.
- Porcupine: en SATB reparte tu acorde en soprano, contralto, tenor y bajo moviéndose lo menos posible; en Armonizar tocas solo la melodía y elige los acordes de la tonalidad. Las notas MIDI 24 a 28 cambian la articulación. Para las 8 salidas activa «Multisalida» y las salidas 2 a 8 en tu DAW.
- Hedgehog: deja sonar la parte más fuerte unos segundos y pulsa APLICAR SUGERENCIAS; el limitador llega al volumen de la plataforma elegida. EXPORTAR ARCHIVOS procesa WAV, AIFF o FLAC con los mismos ajustes y los deja en ~/Música/Racoon/Hedgehog/Exportados. REPORTE escribe las medidas y el control de calidad.
- Chinchilla: afina hacia la escala, hacia las notas MIDI que tocas o hacia el editor de notas. Para el editor: CAPTURAR, play a la canción, PARAR; aparecen las notas cantadas y se arrastran arriba o abajo. Vocoder y talkbox usan el sinte interno (toca notas MIDI) o un sidechain.

## Muestras Racoon

Una biblioteca de muestras reales que comparten los plugins y el estudio: 3060 golpes sueltos en 1389 sonidos (cada sonido con hasta 3 capas de velocidad y alternancias), 76 instrumentos cromáticos en SFZ con 996 notas, 111 texturas, 21 loops, 129 voces sueltas y 5063 ondas (4358 de un ciclo y 705 tablas de 64 cuadros). Viene de 55 bibliotecas gratuitas que permiten redistribuir y modificar: CC0, dominio público, CC BY 3.0 y 4.0, The Unlicense y GPL. El crédito de cada una está en CREDITOS.txt dentro de la carpeta, con su autor, su licencia y de dónde salió.

Entre lo que trae: 30 cajas distintas de Frankensnare, los kits completos de Virtuosity, Unruly, Big Rusty, Swirly, Muldjord y DRS (con los micrófonos mezclados: cercano, parche de abajo, overheads y sala), el hi-hat de 20 pulgadas de The Hat With The Phat, la TR-808 de 1994 de Michael Fischer con sus 25 bombos y 25 cajas, tres kits de trap y soul (Trap Duro, Bounce, Soul Vintage), LinnDrum, CR-78, CR-8000, TR-707, TR-505, Drumtraks, Casio RZ-1, shakers, panderetas, congas, bongós, timbales, güiros, cabasas, cencerros, agogós, cajón, djembé, darbuka, tablas, bobobo de Ghana y percusión corporal. En cromáticos: el Salamander Grand, el Splendid Grand de Akai, pianos verticales, piano FM, 12 bajos (eléctricos, acústicos y contrabajo), 5 guitarras, cuerdas, metales, maderas, saxofones, armónicas, marimba, vibráfono, kalimba, mbira, balafón, steel drum, arpas y timbales de orquesta. Los sostenidos traen bucle buscado nota por nota.

Se dejaron fuera a propósito: jRhodes3d (sus muestras son CC BY-NC), la 909 de Rob Roy (prohíbe modificarla), Drum Abuse y SMDrums (sin licencia clara).

Dónde la buscan los plugins, en orden: la variable RACOON_MUESTRAS, Música/Racoon/Muestras Racoon, Documentos/Racoon/Muestras Racoon, una carpeta "muestras" junto al plugin, y la carpeta muestras del paquete (al lado de plugins y estudio). Racoon la copia sola a Música/Racoon/Muestras Racoon la primera vez que abre, para que cualquier DAW la encuentre.

En cada plugin:

- Hamster: botón MUESTRAS en la página KIT (o clic derecho en un pad > Muestras Racoon...). El menú abre primero las categorías del rol de la pista y luego todo lo demás. La pista queda con sus capas de velocidad en las ranuras (modo Velocity), sin capas sintéticas encima y con la envolvente a la medida de la muestra. KIT DE MUESTRAS pone un kit entero en las 16 pistas sin tocar los patrones. Además hay 42 kits de fábrica «Muestras / ...»: uno por biblioteca (Virtuosity, Muldjord Rock, DRS Metal, TR-808, LinnDrum...) y uno por estilo (Trap 808, Trap duro, Bounce, Soul vintage, Reggaetón y dembow, Boom bap, Lo-fi, House orgánico, Deep house, Techno, Cumbia, Salsa, Afro, Cajón flamenco, Jazz con escobillas, Synthpop 80, Disco, Orquestal...). El proyecto guarda solo el nombre de cada muestra, no el audio: pesa poco y vuelve completo.
- Squirrel: botón MUESTRAS. Instrumentos carga un instrumento cromático; Golpes y sonidos sueltos pone uno en todo el teclado con sus capas; Kits de batería pone un kit en el teclado desde C1, con los hi-hats que se cortan entre sí. Squirrel ahora lee SFZ (regiones, teclas por nombre, velocidades, bucles, volumen, afinación, default_path, global/master/group), así que también abre los .sfz de otras bibliotecas libres.
- Mole: BATERÍA, a la izquierda de las herramientas, cambia la batería sintetizada por cualquiera de los 42 kits. Cada golpe cae en su nota General MIDI y el proyecto recuerda el kit.
- Badger: el botón LOAD de un oscilador abre las ondas de un ciclo (AKWF), las tablas de 64 cuadros (WaveEdit Online) y, en el motor Sample, los bombos largos de 808, texturas y voces.
- Skunk: CARGAR... > Muestras Racoon da texturas, voces, efectos, loops y campanas como material para los granos.

Reconstruir o ampliar la biblioteca: en estudio/scripts/muestras está el programa que la arma desde las bibliotecas originales (baja solo los archivos elegidos, mezcla micrófonos, recorta colas, iguala niveles por capa, busca bucles y escribe el catálogo). Con RACOON_PERFIL=completo saca más capas, más alternancias y más notas.

## Lo que todavía no tienen

- Squirrel: warp y estiramiento de tiempo independiente de la afinación, el modo granular para diseño de samples, y la lectura desde disco de archivos muy largos (carga la muestra completa en memoria, hasta 20 minutos).
- Chipmunk: detectar solo la tonalidad de lo que tocas (la tonalidad se elige a mano) y strum en ticks (solo milisegundos).
- Dormouse: MPE (la ficha lo deja para después) y cargar wavetables propias.
- Ferret: una sola guitarra (con doble toma opcional); los acordes se reconocen al tocarlos, no desde una pista de audio.
- Porcupine: las voces son sintetizadas por formantes, no grabaciones; canta vocales y sílabas, no letras libres.
- Hedgehog: el lote exporta WAV de 24 bits (sin MP3); las referencias se cargan hasta 10 minutos.
- Chinchilla: 45 ms de latencia (el host la compensa); el editor de notas trabaja con la captura en tiempo real, no con ARA; formantes hasta ±6 semitonos.

## Licencia

Código propio sobre JUCE 8, que se usa bajo su licencia AGPLv3 (para repartir binarios con el código cerrado haría falta la licencia comercial de JUCE). Los sonidos de fábrica de Squirrel y las wavetables de Dormouse se generan con código del propio proyecto. Las muestras de Muestras Racoon son de terceros, cada una con su licencia libre: están en su propia carpeta con CREDITOS.txt y los textos de las licencias.

## Afinar a la tonalidad

- Hamster: «Afinar el kit a» (y «Escala del kit») mide el bombo, la caja, el rim, los toms, las congas, los bongós y el cencerro, golpe por golpe, y mueve su Tune para que suenen en la tonalidad: el bombo a la raíz o la quinta (lo que mide es la cola, donde el bombo se asienta), lo demás a notas de la escala. Lo vuelve a hacer cuando cambias de kit. El resonador y el modulador de anillo siguen la raíz de esa tonalidad. «Notas del kit: General MIDI» pone cada tambor en su nota GM aunque el kit venga en 36, 37, 38...
- Mole: «Afinar la percusión a» lleva sus toms, congas, bongós y bombo propios a notas de la tonalidad.
- Chipmunk con «Acordes: Apagado» toca las notas tal como llegan; el sonido Pluck de su sonido propio ya suena (antes se apagaba en la primera muestra).
- Chinchilla: «Cantar las notas MIDI» la pone a cantar sola, sin grabar nada: una voz con vocales que cambian en cada sílaba, vibrato y un deslizamiento corto entre notas. «Cantar: vocal» fija una vocal, «Cantar: voz» la hace grave, media o aguda.
- Skunk: la BIBLIOTECA (abajo a la izquierda en la página SONIDO) trae ocho materiales que Skunk hace solo, afinados en Do: Pad de cristal, Coro oscuro, Cuerdas lejanas, Viento afinado, Campanas suaves, Órgano grave, Polvo de cinta y Agua profunda. Con «Granos con el teclado» cada nota MIDI los toca a su altura. CARGAR ARCHIVO usa un WAV, AIFF o FLAC tuyo; GRABAR LO QUE ENTRA graba lo que le llega a Skunk por su canal. Un material congelado ya no se pierde al parar ni al cambiar la frecuencia de muestreo.
- El piano roll de abajo en cada plugin se desplaza: la barra amarilla bajo las notas lo mueve, Shift + rueda avanza en el tiempo, Cmd + rueda (o pellizcar) acerca y aleja. Muestra cuatro compases a la vez y numera cada compás.
