#ifndef DISCHETTO_HPP
#define DISCHETTO_HPP

#include "NotaData.hpp"
#include <vector>
#include <cstddef>

class Dischetto {
public:
    Dischetto() = default;

    // Metodo di simulazione: pre-genera le note con le armoniche per il nostro test
    void simulaGenerazioneDisco(int numeroNote = 49, int sampleRate = 44100, float durataSecondi = 4.0f);

    // Accesso ultra-veloce O(1) in sola lettura
    const NotaData* getNota(size_t indice) const;
    size_t getNumeroNote() const;

    int getSampleRate() const;

private:
    std::vector<NotaData> note;
    int sampleRate = 44100; // Frequenza di campionamento standard
};

#endif