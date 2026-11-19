#include "Help.h"

namespace be::ui
{
using namespace juce;

namespace
{
// exact ids
const std::map<String, String>& exact()
{
    static const std::map<String, String> m {
        { "master_volume", "Volumen general de Badger. Si la pista se satura, bájalo aquí y no en el fader de tu DAW."_u },
        { "v_glide", "Desliz. Tiempo que tarda una nota en llegar a la siguiente cuando las tocas ligadas. Arriba de 100 ms se oye el barrido." },
        { "v_mode", "Mono toca una nota a la vez, Legato liga las notas sin volver a atacar, Poly deja tocar acordes y Unison apila voces." },
        { "v_voices", "Cuántas notas pueden sonar al mismo tiempo en modo Poly."_u },
        { "v_quality", "Eco gasta menos CPU. Normal sirve para casi todo. High suena más limpio en agudos y es el que se usa al exportar."_u },
        { "v_unison", "Cuántas copias de cada nota suenan a la vez. Más copias hacen el bajo más ancho."_u },
        { "v_unidetune", "Qué tan desafinadas están las copias del unísono entre sí."_u },
        { "v_transpose", "Sube o baja todo el instrumento en semitonos." },
        { "v_tune", "Afinación de referencia. Casi siempre se deja en 440 Hz."_u },
        { "v_humanize", "Pequeñas variaciones al azar en cada nota para que no suene a máquina."_u },
        { "v_velcurve", "Cambia cómo responde el sonido a la fuerza con que tocas."_u },
        { "acid_morph", "Ácido. Lleva el bajo de un sonido limpio a un 303 chillón moviendo filtro, resonancia y distorsión al mismo tiempo."_u },
        { "acid_cutoff", "Corte del filtro del circuito ácido. Abierto suena brillante, cerrado suena oscuro."_u },
        { "acid_res", "Resonancia del circuito ácido. Arriba de 70% empieza el chillido típico del acid."_u },
        { "acid_envmod", "Cuánto abre el filtro cada nota. Es la perilla que hace el 'uau' del acid."_u },
        { "acid_decay", "Qué tan rápido se cierra el filtro después de cada nota."_u },
        { "acid_accent", "Qué tanto resaltan las notas con acento: suben de volumen y abren más el filtro."_u },
        { "acid_slide", "Tiempo del slide entre notas ligadas del secuenciador." },
        { "acid_drive", "Saturación del circuito ácido."_u },
        { "sub_level", "Volumen del suboscilador, la nota grave pura debajo de todo. Es lo que se siente en el pecho en un club." },
        { "sub_cutoff", "Filtro del sub. Más bajo lo deja más limpio."_u },
        { "sub_drive", "Satura el sub para que se oiga también en bocinas chicas."_u },
        { "seq_bpm", "Tempo del secuenciador cuando no sigue a tu DAW." },
        { "seq_swing", "Retrasa las semicorcheas pares para que el patrón se mueva menos cuadrado."_u },
        { "seq_gate", "Largo de todas las notas del patrón a la vez."_u },
        { "seq_length", "Cuántos pasos tiene el patrón antes de repetirse."_u },
        { "seq_rate", "Duración de cada paso. 1/16 es lo normal en house."_u },
        { "seq_root", "Nota base del patrón."_u },
        { "seq_scale", "Escala del patrón. Con SCALE LOCK todas las notas caen dentro de esta escala."_u },
        { "seq_mode", "Off apaga el secuenciador, Sequencer toca el patrón y Arp convierte lo que tocas en arpegio."_u },
        { "seq_trigger", "Key Held toca el patrón solo mientras sostienes una tecla. Always lo toca con el transporte de tu DAW."_u },
        { "noise_level", "Volumen del ruido, sirve para darle aire o ataque al bajo." },
        { "layer_xfade", "Cruza el sonido entre la capa de cuerpo y la de mordida." },
        { "evo_amount", "Cuánto cambia el sonido solo con el paso de los compases."_u },
    };
    return m;
}

// suffix after the last underscore
const std::map<String, String>& bySuffix()
{
    static const std::map<String, String> m {
        { "cutoff", "Frecuencia de corte. Abierto deja pasar los agudos, cerrado deja el bajo oscuro y redondo." },
        { "res", "Resonancia. Realza la frecuencia de corte. Mucha resonancia chilla." },
        { "drive", "Saturación. Agrega armónicos para que el bajo se oiga en bocinas chicas."_u },
        { "envamt", "Cuánto mueve la envolvente al filtro en cada nota."_u },
        { "keytrack", "El filtro sigue a la nota: en notas agudas se abre más."_u },
        { "velsens", "Qué tanto responde a la fuerza con que tocas."_u },
        { "mix", "Mezcla entre la señal original y la procesada."_u },
        { "level", "Volumen." },
        { "volume", "Volumen." },
        { "pan", "Posición izquierda y derecha."_u },
        { "oct", "Sube o baja por octavas." },
        { "semi", "Sube o baja por semitonos." },
        { "fine", "Afinación fina en centésimas de semitono."_u },
        { "coarse", "Afinación gruesa en semitonos."_u },
        { "detune", "Desafina las copias del unísono para hacer el sonido más ancho."_u },
        { "spread", "Abre las copias del unísono en estéreo."_u },
        { "unison", "Copias de la voz que suenan juntas." },
        { "pos", "Posición dentro de la tabla de ondas. Moverla cambia el timbre."_u },
        { "morph", "Transforma la forma de onda o el tipo de filtro de manera continua." },
        { "attack", "Ataque. Lo que tarda en llegar al máximo. Corto golpea, largo entra suave."_u },
        { "decay", "Caída. Lo que tarda en bajar después del ataque."_u },
        { "sustain", "Nivel que se mantiene mientras sostienes la nota." },
        { "release", "Lo que tarda en apagarse al soltar la nota." },
        { "hold", "Tiempo que se queda arriba antes de empezar a caer." },
        { "rate", "Velocidad." },
        { "depth", "Profundidad del efecto." },
        { "amt", "Cantidad." },
        { "time", "Tiempo." },
        { "feedback", "Retroalimentación. Más alto repite más o resuena más."_u },
        { "size", "Tamaño del espacio."_u },
        { "damp", "Apaga los agudos de la cola." },
        { "predelay", "Espera antes de que empiece la reverb, deja el ataque limpio." },
        { "width", "Ancho estéreo."_u },
        { "thresh", "Umbral. A partir de este nivel empieza a actuar." },
        { "ratio", "Qué tan fuerte comprime."_u },
        { "makeup", "Recupera el volumen después de comprimir."_u },
        { "tone", "Tono. Más oscuro o más brillante."_u },
        { "shape", "Forma o carácter."_u },
        { "freq", "Frecuencia." },
        { "gain", "Ganancia." },
        { "out", "Volumen de salida." },
        { "on", "Enciende o apaga esta sección."_u },
        { "sync", "Sincroniza con el tempo." },
        { "phase", "Fase de inicio de la onda." },
        { "fold", "Pliega la onda sobre sí misma para sacar armónicos metálicos."_u },
        { "drift", "Pequeñas desafinaciones lentas, como un sintetizador analógico."_u },
        { "slop", "Imprecisión analógica entre notas."_u },
        { "lowcut", "Quita graves por debajo de esta frecuencia." },
        { "highcut", "Quita agudos por encima de esta frecuencia." },
        { "smooth", "Suaviza los cambios." },
        { "curve", "Curva de respuesta." },
    };
    return m;
}

String section (const String& id)
{
    if (id.startsWith ("o") && id.length() > 2 && CharacterFunctions::isDigit (id[1]) && id[2] == '_') return "Oscilador " + String::charToString (id[1]);
    if (id.startsWith ("f") && id.length() > 2 && CharacterFunctions::isDigit (id[1]) && id[2] == '_') return "Filtro " + String::charToString (id[1]);
    if (id.startsWith ("e") && id.length() > 2 && CharacterFunctions::isDigit (id[1]) && id[2] == '_') return "Envolvente " + String::charToString (id[1]);
    if (id.startsWith ("l") && id.length() > 2 && CharacterFunctions::isDigit (id[1]) && id[2] == '_') return "LFO " + String::charToString (id[1]);
    if (id.startsWith ("d") && id.length() > 2 && CharacterFunctions::isDigit (id[1]) && id[2] == '_') return "Distorsión "_u + String::charToString (id[1]);
    if (id.startsWith ("ly")) return "Capa " + id.substring (2, 3);
    if (id.startsWith ("fx_")) return "Efecto " + id.fromFirstOccurrenceOf ("fx_", false, false).upToFirstOccurrenceOf ("_", false, false);
    if (id.startsWith ("mac")) return "Macro " + id.substring (3);
    return {};
}
}

String helpFor (const String& id, const String& paramName)
{
    String text;
    if (auto it = exact().find (id); it != exact().end()) text = it->second;
    else
    {
        if (id.startsWith ("mac"))
            text = "Macro. Mueve varios controles a la vez. Arrastra el cuadrito de la esquina sobre cualquier perilla para asignarle más."_u;
        else if (auto s = bySuffix().find (id.fromLastOccurrenceOf ("_", false, false)); s != bySuffix().end())
            text = s->second;
    }
    const String sec = section (id);
    String head = paramName;
    if (sec.isNotEmpty() && ! paramName.containsIgnoreCase (sec)) head = sec + " · "_u + paramName;
    String out = head;
    if (text.isNotEmpty()) out << "\n" << text;
    out << "\nDoble click regresa al valor original. Shift + arrastrar mueve fino. Click derecho asigna MIDI o modulación."_u;
    return out;
}
} // namespace be::ui
