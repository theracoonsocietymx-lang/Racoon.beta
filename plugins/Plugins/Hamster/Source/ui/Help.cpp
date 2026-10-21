#include "Help.h"

namespace be::ui
{
using namespace juce;

namespace
{
const std::map<String, String>& globals()
{
    static const std::map<String, String> m {
        { "g_master", "Volumen general de Hamster. Si satura, bájalo aquí y no en el fader de tu DAW."_u },
        { "g_bpm", "Tempo interno, el que se usa cuando tu DAW está parado."_u },
        { "g_swing", "Retrasa los pasos pares para que el ritmo se mueva menos cuadrado. Entre 50 y 60% suena a house." },
        { "g_pattern", "Patrón que está sonando. Cada kit trae 8."_u },
        { "g_fill", "Toca un fill mientras lo sostienes. Los pasos marcados como Fill suenan solo aquí."_u },
        { "g_break", "Quita el bombo y los graves mientras está encendido, para un respiro antes del drop."_u },
        { "g_scatter", "Repite pedacitos del ritmo, el efecto de stutter de los DJs." },
        { "g_morph", "Mezcla poco a poco el patrón actual con otro."_u },
        { "g_groove", "Cuánto se aplica el groove analizado con Groove DNA."_u },
        { "g_humanize", "Pequeñas variaciones de tiempo y fuerza para que no suene a máquina."_u },
        { "g_accent", "Qué tanto resaltan los pasos con acento."_u },
        { "g_quality", "Eco gasta menos CPU. Normal sirve para casi todo. High y Ultra suenan más limpio en agudos."_u },
        { "g_noterepeat", "Al sostener un pad lo repite a este valor de nota, como en una caja de ritmos de pads." },
        { "g_velcurve", "Cambia cómo responde a la fuerza con que tocas los pads."_u },
        { "g_scenefade", "Cruza entre la escena A y la B." },
    };
    return m;
}

const std::map<String, String>& track()
{
    static const std::map<String, String> m {
        { "level", "Volumen de esta pista." },
        { "pan", "Posición izquierda y derecha."_u },
        { "tune", "Afinación en semitonos. Un bombo afinado a la tonalidad del track pega más."_u },
        { "fine", "Afinación fina en centésimas."_u },
        { "decay", "Largo del golpe. Corto es seco, largo deja cola." },
        { "velsens", "Qué tanto cambia el golpe con la fuerza."_u },
        { "choke", "Pistas en el mismo grupo se callan entre sí, como el hat abierto con el cerrado."_u },
        { "circuit", "Circuit Morph. Lleva el sonido de 808 a 909, a sample y a FM de forma continua." },
        { "a_level", "Volumen de la capa analógica."_u },
        { "a_drift", "Pequeñas variaciones de afinación y tiempo entre golpes, como una máquina analógica."_u },
        { "b_level", "Volumen de la capa FM." },
        { "b_fb", "Retroalimentación del FM. Arriba suena más ruidoso y metálico."_u },
        { "b_morph", "Transforma el timbre de la capa FM." },
        { "c_level", "Volumen de la capa de sample." },
        { "c_start", "Desde dónde empieza a sonar el sample."_u },
        { "c_end", "Dónde termina el sample."_u },
        { "c_pitch", "Afinación del sample."_u },
        { "c_stretch", "Estira el sample en el tiempo sin cambiar la afinación."_u },
        { "c_rr", "Cambia un poco el inicio en cada golpe para que no suene igual." },
        { "d_level", "Volumen de la capa de textura (ruido, metal, granular)." },
        { "d_freq", "Frecuencia de la textura." },
        { "d_decay", "Largo de la textura." },
        { "d_tone", "Tono de la textura." },
        { "env_attack", "Ataque. Corto golpea, largo entra suave." },
        { "env_hold", "Cuánto se queda arriba antes de caer."_u },
        { "env_decay", "Caída del volumen."_u },
        { "env_release", "Lo que tarda en apagarse." },
        { "penv_amt", "Barrido de afinación al inicio del golpe. Es lo que da el 'pum' del bombo."_u },
        { "penv_time", "Qué tan rápido baja el barrido de afinación."_u },
        { "fenv_amt", "Cuánto abre el filtro al inicio del golpe."_u },
        { "f_cutoff", "Frecuencia de corte del filtro de la pista." },
        { "f_res", "Resonancia del filtro." },
        { "f_drive", "Saturación del filtro."_u },
        { "drv_amt", "Saturación de la pista. Agrega cuerpo y hace que se oiga en bocinas chicas."_u },
        { "crush", "Bitcrush. Ensucia el sonido como un sampler viejo." },
        { "eq_low", "Graves de la pista." },
        { "eq_high", "Agudos de la pista." },
        { "comp", "Compresión de la pista. Le da golpe y lo hace más parejo."_u },
        { "transient", "Realza o suaviza el ataque del golpe." },
        { "fader", "Volumen en el mezclador." },
        { "send1", "Envío al bus de efectos 1."_u }, { "send2", "Envío al bus de efectos 2."_u },
        { "send3", "Envío al bus de efectos 3."_u }, { "send4", "Envío al bus de efectos 4."_u },
    };
    return m;
}

const std::map<String, String>& suffix()
{
    static const std::map<String, String> m {
        { "mix", "Mezcla entre la señal original y la procesada."_u }, { "level", "Volumen." }, { "time", "Tiempo." },
        { "feedback", "Retroalimentación. Más alto repite más."_u }, { "size", "Tamaño del espacio."_u }, { "damp", "Apaga los agudos de la cola." },
        { "rate", "Velocidad." }, { "depth", "Profundidad." }, { "freq", "Frecuencia." }, { "drive", "Saturación."_u },
        { "thresh", "Umbral. A partir de este nivel actúa."_u }, { "ratio", "Qué tan fuerte comprime."_u }, { "attack", "Ataque." },
        { "release", "Lo que tarda en soltar." }, { "width", "Ancho estéreo."_u }, { "tone", "Tono." }, { "amt", "Cantidad." },
    };
    return m;
}
}

String helpFor (const String& id, const String& paramName)
{
    String text;
    if (auto it = globals().find (id); it != globals().end()) text = it->second;
    else if (id.startsWithChar ('t') && id.containsChar ('_') && CharacterFunctions::isDigit (id[1]))
    {
        const auto local = id.fromFirstOccurrenceOf ("_", false, false);
        if (auto t = track().find (local); t != track().end()) text = t->second;
        else if (local.startsWith ("a_p")) text = "Control del circuito analógico de este sonido. El nombre arriba de la perilla dice qué mueve."_u;
        else if (local.startsWith ("b_r")) text = "Relación de frecuencia de este operador FM. Números enteros suenan afinados, decimales suenan metálicos."_u;
        else if (local.startsWith ("b_l")) text = "Nivel de este operador FM.";
        else if (local.startsWith ("b_d")) text = "Caída de este operador FM."_u;
    }
    else if (id.startsWith ("mac"))
        text = "Macro. Mueve varios controles a la vez.";
    else if (auto s = suffix().find (id.fromLastOccurrenceOf ("_", false, false)); s != suffix().end())
        text = s->second;
    String out = paramName;
    if (text.isNotEmpty()) out << "\n" << text;
    out << "\nDoble click regresa al valor original. Shift + arrastrar mueve fino. Click derecho asigna MIDI o modulación."_u;
    return out;
}
} // namespace be::ui
