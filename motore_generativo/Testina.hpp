#ifndef TESTINA_HPP
#define TESTINA_HPP

#include "NotaData.hpp"
#include <vector>
#include <string>
#include <cstddef>

class Testina {
public:
    struct ParametriSintesi {
        int sampleRate;                       // Contrattuale da AudioDeviceManager
        float livelloNormalizzazione = 0.35f; // Volume naturale calcolato per lasciare headroom agli accordi
    };

    explicit Testina(const ParametriSintesi& parametri);

    /**
     * Genera la fisica completa di una singola nota e popola direttamente la cella NotaData.
     * @param frequenzaFondamentale Frequenza f0 in Hz calcolata dal Masterizzatore
     * @param nomeNota             Etichetta diagnostica (es. "Do4", "Nota_12")
     * @param panL                 Guadagno equal-power canale sinistro
     * @param panR                 Guadagno equal-power canale destro
     * @param destinazione         Riferimento alla cella NotaData nel Dischetto
     */
    void generaSingolaNota(float frequenzaFondamentale,
                           const std::string& nomeNota,
                           float panL,
                           float panR,
                           NotaData& destinazione);

private:
    ParametriSintesi params;

    // --- Calcoli fisici adattivi dipendenti esclusivamente da f0 ---
    float calcolaDurataBuffer(float f0) const;
    float calcolaInarmonicita(float f0) const;
    float calcolaPuntoPercussione(float f0) const;
    int determinaNumeroCorde(float f0) const;

    // --- Sintesi ultra-rapida tramite fasori a 64 bit per un livello di velocity ---
    void sintetizzaLayerFasori(float f0, 
                               float sogliaVelocity, 
                               size_t totaleCampioni, 
                               std::vector<float>& bufferUscita);
};

#endif // TESTINA_HPP
