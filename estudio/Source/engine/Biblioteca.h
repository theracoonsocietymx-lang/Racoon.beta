#pragma once
#if __has_include (<JuceHeader.h>)
 #include <JuceHeader.h>
#else
 #include <juce_audio_formats/juce_audio_formats.h>
 #include <juce_gui_basics/juce_gui_basics.h>
#endif
#include <functional>
#include <map>
#include <vector>

// Muestras Racoon: miles de muestras con licencia libre (CC0, CC BY, dominio público, GPL) que comparten el estudio y
// todos los plugins. Vive en una carpeta (Música/Racoon/Muestras Racoon) con un catalogo.json que la describe. Esta clase la
// encuentra, lee el catálogo una sola vez y arma los menús. Si la carpeta no está, todo sigue funcionando sin ella.
namespace racoon::biblio
{
struct Muestra
{
    juce::String id, archivo, nombre, cat, grupo, pack, fuente;
    int capa = 1, capas = 1, rr = 1, canales = 1;
    float dur = 0.0f, tonoHz = 0.0f, rmsDb = -60.0f;
    int brillo = 0;
    double bpm = 0.0;
    juce::StringArray generos, etiquetas;
};

struct Grupo                      // un sonido: todas sus capas de velocidad y alternancias
{
    juce::String id, nombre, cat, pack, fuente;
    std::vector<int> muestras;    // índices en Catalogo::muestras, ordenados por capa y alternancia
    int capas = 1, alternancias = 1;
    float tonoHz = 0.0f, dur = 0.0f;
    juce::StringArray generos, etiquetas;
};

struct Instrumento                // multimuestra cromático con su mapa SFZ (para Squirrel)
{
    juce::String id, archivo, nombre, cat, pack, fuente, rangoBajo, rangoAlto;
    int notas = 0, capas = 1;
    bool bucle = false;
    juce::StringArray generos;
};

struct Kit                        // 16 pistas para Hamster
{
    struct Pista { int pista = 0; juce::String nombre, rol, grupo; };
    juce::String id, nombre;
    juce::StringArray generos;
    int bpm = 120;
    std::vector<Pista> pistas;
};

struct Fuente { juce::String id, nombre, autor, licencia, url; };

struct Onda                       // una onda de un ciclo (AKWF) o una tabla de 64 cuadros (WaveEdit)
{
    juce::String id, archivo, nombre, grupo, fuente;
    int ciclo = 600, cuadros = 1;
};

class Catalogo
{
public:
    static const Catalogo& get();                 // se carga una vez, la primera vez que alguien la pide
    static juce::Array<juce::File> carpetasPosibles();
    static juce::File carpetaUsuario();           // Música/Racoon/Muestras Racoon
    static juce::File buscarCarpeta();            // la primera que tenga catalogo.json

    bool disponible() const { return ! muestras.empty() || ! instrumentos.empty(); }
    juce::File raiz;
    juce::String error;

    std::vector<Muestra> muestras;
    std::vector<Grupo> grupos;
    std::vector<Instrumento> instrumentos;
    std::vector<Kit> kits;
    std::map<juce::String, Fuente> fuentes;
    std::map<juce::String, juce::String> nombresCategoria;

    const Grupo* grupo (const juce::String& id) const;
    const Muestra* muestra (const juce::String& id) const;
    const Instrumento* instrumento (const juce::String& id) const;
    const Kit* kit (const juce::String& idONombre) const;
    std::vector<const Grupo*> gruposDe (const juce::StringArray& cats) const;
    std::vector<const Kit*> kitsDeGenero (const juce::String& genero) const;
    juce::String nombreCategoria (const juce::String& cat) const;
    juce::File archivo (const juce::String& rel) const { return raiz.getChildFile (rel); }
    juce::String credito (const juce::String& fuente) const;     // "Nombre, autor (licencia)"
    const std::vector<Onda>& ondas() const;                       // se lee aparte (ondas.json), solo si alguien la pide

    // lectura de audio sin normalizar (las capas de velocidad conservan su nivel relativo)
    static bool leer (const juce::File&, juce::AudioBuffer<float>& out, double& rate, double maxSegundos = 30.0);

    // categorías de batería por rol de Hamster ("Kick", "Snare"...), en orden de preferencia
    static juce::StringArray categoriasDeRol (const juce::String& rol);

private:
    Catalogo();
    void cargar (const juce::File& carpeta);
    std::map<juce::String, int> indiceGrupo, indiceMuestra;
    mutable std::vector<Onda> ondasCache;
    mutable bool ondasLeidas = false;
    mutable juce::CriticalSection ondasLock;
};

// ---------------------------------------------------------------------------- menús
// Agrega al menú los grupos de esas categorías, por categoría y paquete. onElegir recibe el id del grupo.
void menuGrupos (juce::PopupMenu& m, const juce::StringArray& cats, std::function<void (const juce::String&)> onElegir, const juce::String& actual = {});
// Menú completo: primero las categorías sugeridas, luego todas las demás de la biblioteca.
void menuBiblioteca (juce::PopupMenu& m, const juce::StringArray& sugeridas, std::function<void (const juce::String&)> onElegir, const juce::String& actual = {});
void menuInstrumentos (juce::PopupMenu& m, std::function<void (const juce::String&)> onElegir, const juce::String& actual = {});
void menuOndas (juce::PopupMenu& m, bool tablas, std::function<void (const Onda&)> onElegir);
juce::String avisoSinBiblioteca();
} // namespace racoon::biblio
