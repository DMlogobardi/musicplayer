#ifndef DISCHETTO_HPP
#define DISCHETTO_HPP

#include "NotaData.hpp"
#include <vector>
#include <cstddef>
#include <string>
#include <utility>

class Dischetto {
public:
    // Costruttore di default: inizializza con tutti i parametri standard di fabbrica
    Dischetto() = default;

    // Costruttore parametrico: qualsiasi argomento omesso assume il valore standard del Dischetto
    Dischetto(int sampleRate, 
              int notePerOttava = 12, 
              std::vector<NotaData> noteGenerate = {}, 
              std::string nome = "Standard Disc", 
              size_t indiceAncora = 0, 
              int ottavePerLato = 3)
        : note(std::move(noteGenerate)),
          sampleRate(sampleRate > 0 ? sampleRate : 44100),
          notePerOttava(notePerOttava > 0 ? notePerOttava : 12),
          nomeDisco(std::move(nome)),
          indiceAncora(indiceAncora),
          ottavePerLato(ottavePerLato >= 0 ? ottavePerLato : 3) {}

    // Metodo di simulazione: pre-genera le note con le armoniche per il nostro test
    void simulaGenerazioneDisco(int numeroNote = 49, int sampleRate = 44100, float durataSecondi = 4.0f);

    // Accesso ultra-veloce O(1) in sola lettura
    const NotaData* getNota(size_t indice) const;
    size_t getNumeroNote() const;

    int getSampleRate() const;

    const std::string& getNomeDisco() const {
        return nomeDisco;
    }

    int getNotePerOttava() const {
        return notePerOttava;
    }

    size_t getIndiceAncora() const {
        return indiceAncora;
    }

    int getOttavePerLato() const {
        return ottavePerLato;
    }

    int getTotaleOttave() const {
        return 2 * ottavePerLato + 1;
    }

private:
    std::vector<NotaData> note;
    int sampleRate = 44100; // Frequenza di campionamento standard
    int notePerOttava = 12; // Divisione dell'ottava (N) con cui è stato generato il dischetto
    std::string nomeDisco = "Standard Disc"; // Nome standard del disco
    size_t indiceAncora = 0; // Indice del tasto centrale (frequenza di riferimento)
    int ottavePerLato = 3;  // Numero di ottave sopra e sotto il centro per Flutter
};

#endif