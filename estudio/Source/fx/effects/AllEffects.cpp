#include "AllEffects.h"

namespace racoon::fx
{
void registerDynamics (std::vector<FxDef>&);
void registerSidechain (std::vector<FxDef>&);
void registerEqFilters (std::vector<FxDef>&);
void registerUtility (std::vector<FxDef>&);
void registerDistortion (std::vector<FxDef>&);
void registerModulation (std::vector<FxDef>&);
void registerDelays (std::vector<FxDef>&);
void registerReverbs (std::vector<FxDef>&);
void registerSpectral (std::vector<FxDef>&);
void registerExtras1 (std::vector<FxDef>&);
void registerExtras2 (std::vector<FxDef>&);
void registerPrisma (std::vector<FxDef>&);

// devices that are not processors of their own: modulators live on the track, the rack and the external effect are Tracktion devices
static void registerDevices (std::vector<FxDef>& v)
{
    v.push_back ({ "lfo", "LFO", "Moduladores", "Mueve cualquier perilla del canal con una onda: vibrato, barridos, trémolo."_u, Kind::modulator, nullptr, {} });
    v.push_back ({ "seguidor-envolvente", "Seguidor de envolvente", "Moduladores", "Mueve una perilla según el volumen de la pista: más fuerte, más se abre."_u, Kind::modulator, nullptr, {} });
    v.push_back ({ "forma", "Forma", "Moduladores", "Una curva de hasta cuatro puntos que se repite y mueve cualquier perilla."_u, Kind::modulator, nullptr, {} });
    v.push_back ({ "rack", "Rack de efectos", "Ruteo", "Varias cadenas de efectos en paralelo y ocho macros para moverlas juntas."_u, Kind::external, nullptr, {} });
    v.push_back ({ "efecto-externo", "Efecto externo", "Ruteo", "Manda el sonido a un aparato real por tu interfaz de audio y lo trae de vuelta."_u, Kind::external, nullptr, {} });
}

void appendAllEffects (std::vector<FxDef>& v)
{
    registerDynamics (v);
    registerSidechain (v);
    registerEqFilters (v);
    registerDistortion (v);
    registerModulation (v);
    registerDelays (v);
    registerReverbs (v);
    registerSpectral (v);
    registerExtras1 (v);
    registerExtras2 (v);
    registerPrisma (v);
    registerDevices (v);
    registerUtility (v);
}
}
