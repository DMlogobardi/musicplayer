#include "Dischetto.hpp"
#include <cmath>
#include <random>
#include <string>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void Dischetto::simulaGenerazioneDisco(int numeroNote, int sampleRate, float durataSecondi) {
    note.clear();
    this->sampleRate = sampleRate;

    int totaleCampioni = static_cast<int>(sampleRate * durataSecondi);
    const std::vector<float> soglieVelocity = { 0.35f, 0.70f, 1.00f };

    // Frequenza base: MIDI 36 (C2 / Do1)
    int startMidi = 36; 

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> distNoise(-1.0f, 1.0f);

    for (int i = 0; i < numeroNote; ++i) {
        int midiNote = startMidi + i;
        NotaData nota;
        nota.frequenza = 440.0f * std::pow(2.0f, (midiNote - 69) / 12.0f);

        // Nomi convenzionali delle note per debug
        static const char* nomiNote[] = {"Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si"};
        int ottava = (midiNote / 12) - 1;
        nota.nome = std::string(nomiNote[midiNote % 12]) + std::to_string(ottava);

        // Panning Stereo Equal-Power basato sul registro (Bassi a SX, Acuti a DX)
        float panPos = static_cast<float>(i) / static_cast<float>(numeroNote - 1);
        nota.guadagno_L = std::cos(panPos * static_cast<float>(M_PI) * 0.5f);
        nota.guadagno_R = std::sin(panPos * static_cast<float>(M_PI) * 0.5f);

        // Parametro di disarmonicità della corda rigida (Rigid Stiff String Model)
        double B = 0.00025;

        // Smorzamento naturale dipendente dalla frequenza
        double smorzamentoFrequenza = std::sqrt(nota.frequenza / 261.63f);

        for (float soglia : soglieVelocity) {
            LayerVelocity layer;
            layer.soglia_velocity = soglia;
            layer.buffer_campioni.resize(totaleCampioni);

            int numArmoniche = static_cast<int>(6 + soglia * 12); 

            for (int c = 0; c < totaleCampioni; ++c) {
                double t = static_cast<double>(c) / static_cast<double>(sampleRate);
                double segnaleArmonico = 0.0;

                for (int k = 1; k <= numArmoniche; ++k) {
                    double freq_k = k * nota.frequenza * std::sqrt(1.0 + B * k * k);
                    double rollOff = std::pow(k, -1.2 + (1.0 - soglia) * 0.8);
                    double ampiezza_k = (1.0 / static_cast<double>(k)) * rollOff;

                    // Le armoniche superiori decadono più velocemente
                    double smorzamentoArmonica = std::exp(- (1.0 + 0.3 * (k - 1)) * smorzamentoFrequenza * 1.2 * t);
                    segnaleArmonico += ampiezza_k * std::sin(2.0 * M_PI * freq_k * t) * smorzamentoArmonica;
                }

                // Inviluppo a doppio esponenziale (Prompt + Sustained sound)
                double attaccoPronto = 0.65 * std::exp(-4.5 * smorzamentoFrequenza * t);
                double codaSostenuta = 0.35 * std::exp(-0.6 * smorzamentoFrequenza * t);
                double inviluppoTotale = attaccoPronto + codaSostenuta;

                // Transient d'impatto del martelletto in feltro
                double rumoreMartelletto = (t < 0.008) ? (distNoise(rng) * std::exp(-800.0 * t) * 0.15 * soglia) : 0.0;

                double campioneFinale = (segnaleArmonico * inviluppoTotale) + rumoreMartelletto;
                layer.buffer_campioni[c] = static_cast<float>(campioneFinale * 0.45);
            }

            nota.layers.push_back(layer);
        }

        note.push_back(nota);
    }
}

const NotaData* Dischetto::getNota(size_t indice) const {
    if (indice < note.size()) {
        return &note[indice];
    }
    return nullptr;
}

size_t Dischetto::getNumeroNote() const {
    return note.size();
}

int Dischetto::getSampleRate() const {
    return sampleRate;
}