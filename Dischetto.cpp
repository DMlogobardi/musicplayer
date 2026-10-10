#include "Dischetto.hpp"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void Dischetto::simulaGenerazioneDisco(int numeroNote, int sampleRate, float durataSecondi) {
    note.clear();

    this->sampleRate = sampleRate;

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

    for (const auto& elemento : scalaStandard) {
        NotaData nota;
        nota.nome = elemento.first;
        nota.frequenza = elemento.second;
        nota.buffer_campioni.resize(totaleCampioni);

        // Pre-calcolo della forma d'onda con armoniche per la nota corrente
        for (int c = 0; c < totaleCampioni; ++c) {
            double t = static_cast<double>(c) / static_cast<double>(sampleRate);

            // Somma di 3 armoniche: Fondamentale (60%) + 2ª armonica (30%) + 3ª armonica (10%)
            double fondamentale = 0.60 * std::sin(2.0 * M_PI * nota.frequenza * 1.0 * t);
            double armonica2    = 0.30 * std::sin(2.0 * M_PI * nota.frequenza * 2.0 * t);
            double armonica3    = 0.10 * std::sin(2.0 * M_PI * nota.frequenza * 3.0 * t);

            // Decadimento naturale della corda pre-calcolato
            double decadimento = std::exp(-1.2 * t);

            nota.buffer_campioni[c] = static_cast<float>((fondamentale + armonica2 + armonica3) * decadimento);
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