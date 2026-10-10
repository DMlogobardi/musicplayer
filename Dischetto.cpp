#include "Dischetto.hpp"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void Dischetto::simulaGenerazioneDisco(int numeroNote, int sampleRate, float durataSecondi) {
    note.clear();
    this->sampleRate = sampleRate;

    // Esempio: Scala a frequenze arbitrarie o microtonali
    const std::vector<std::pair<std::string, float>> scalaStandard = {
        {"Do",  261.63f},
        {"Re",  293.66f},
        {"Mi",  329.63f},
        {"Fa",  349.23f},
        {"Sol", 392.00f},
        {"La",  440.00f},
        {"Si",  493.88f}
    };

    int totaleCampioni = static_cast<int>(sampleRate * durataSecondi);

    // Definizione di 3 layer di velocity pre-calcolati (Piano, MezzoForte, Forte)
    const std::vector<float> soglieVelocity = { 0.33f, 0.66f, 1.00f };

    for (const auto& elemento : scalaStandard) {
        NotaData nota;
        nota.nome = elemento.first;
        nota.frequenza = elemento.second;

        for (float soglia : soglieVelocity) {
            LayerVelocity layer;
            layer.soglia_velocity = soglia;
            layer.buffer_campioni.resize(totaleCampioni);

            // Timbro e armoniche variano offline in base alla velocity
            // Con velocity alta (soglia = 1.0), le armoniche superiori sono molto più forti
            double peso_f  = 0.60;
            double peso_a2 = 0.30 * (soglia * 0.8 + 0.2);
            double peso_a3 = 0.10 * (soglia * 1.5);

            for (int c = 0; c < totaleCampioni; ++c) {
                double t = static_cast<double>(c) / static_cast<double>(sampleRate);

                double fondamentale = peso_f  * std::sin(2.0 * M_PI * nota.frequenza * 1.0 * t);
                double armonica2    = peso_a2 * std::sin(2.0 * M_PI * nota.frequenza * 2.0 * t);
                double armonica3    = peso_a3 * std::sin(2.0 * M_PI * nota.frequenza * 3.0 * t);

                // SIMULAZIONE DOPPIO DECADIMENTO (Prompt sound + Sustained sound)
                double decadimento_veloce = 0.70 * std::exp(-3.5 * t); // Attacco iniziale
                double decadimento_lento  = 0.30 * std::exp(-0.8 * t); // Coda persistente
                double decadimento_totale = decadimento_veloce + decadimento_lento;

                layer.buffer_campioni[c] = static_cast<float>((fondamentale + armonica2 + armonica3) * decadimento_totale);
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