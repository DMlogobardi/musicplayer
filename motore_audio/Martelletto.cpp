#include "Martelletto.hpp"
#include <stdexcept>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

Martelletto::Martelletto() 
    : id(-1), nota(0.0f), volume(0.0f), stato(StatoMartelletto::IDLE), indice_lettura(0) {}

// --- GETTER ---
int Martelletto::getId() const {
    return id;
}

float Martelletto::getNota() const {
    return nota;
}

float Martelletto::getVolume() const {
    return volume;
}

Martelletto::StatoMartelletto Martelletto::getStato() const {
    return stato;
}

bool Martelletto::eLibero() const {
    return stato == StatoMartelletto::IDLE;
}

// --- SETTER ---
void Martelletto::setId(int newId) {
    if(newId < 0) {
        throw std::invalid_argument("ID must be non-negative");
    }
    id = newId;
}

void Martelletto::setNota(float newNota) {
    if(newNota < 0.0f) {
        throw std::invalid_argument("Nota must be non-negative");
    }
    nota = newNota;
}

void Martelletto::setVolume(float newVolume) {
    if(newVolume < 0.0f || newVolume > 1.0f) {
        throw std::invalid_argument("Volume must be between 0.0 and 1.0");
    }
    volume = newVolume;
}

void Martelletto::setStato(StatoMartelletto newStato) {
    stato = newStato;
}

void Martelletto::colpisci(float nuovaNota, float volumeIniziale) {
    nota = nuovaNota;
    volume = volumeIniziale;
    stato = StatoMartelletto::IN_PERCUSSIONE;
    
    // Reset REALE della posizione del tempo per QUESTO specifico martelletto
    indice_lettura = 0; 
}

void Martelletto::rilascia() {
    if (stato == StatoMartelletto::IN_PERCUSSIONE) {
        stato = StatoMartelletto::IN_SMORZAMENTO;
    }
}

float Martelletto::suona(int sampleRate) {
    if (stato == StatoMartelletto::IDLE) {
        return 0.0f;
    }

    double t = static_cast<double>(indice_lettura) / static_cast<double>(sampleRate);

    float campione_base = std::sin(2.0 * M_PI * nota * t);

    indice_lettura++;

    if (stato == StatoMartelletto::IN_PERCUSSIONE) {
        volume *= 0.99995f; // Decadimento naturale della corda
    } else if (stato == StatoMartelletto::IN_SMORZAMENTO) {
        volume *= 0.995f;   // Decadimento rapido (smorzatore in feltro)
    }

    if (volume < 0.001f) {
        volume = 0.0f;
        stato = StatoMartelletto::IDLE;
        indice_lettura = 0;
    }

    return campione_base * volume;
}