#include "Martelletto.hpp"
#include <stdexcept>
#include <cmath>
#include <algorithm>

Martelletto::Martelletto() 
    : id(-1), nota_colpita(nullptr), buffer_corrente(nullptr), guadagno_velocity(0.0f), stato(StatoMartelletto::IDLE), indice_lettura(0) {}

// --- GETTER ---
int Martelletto::getId() const {
    return id;
}

float Martelletto::getGuadagno_velocity() const {
    return guadagno_velocity;
}

float Martelletto::getInviluppo() const {
    return inviluppo;
}

Martelletto::StatoMartelletto Martelletto::getStato() const {
    return stato;
}

const NotaData* Martelletto::getNota() const {
    return nota_colpita;
}

float Martelletto::getVolume() const {
    return inviluppo * guadagno_velocity;
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

void Martelletto::configura(int sampleRate) {
    if(sampleRate <= 0) {
        throw std::invalid_argument("Sample rate must be positive");
    }

    sample_rate = sampleRate;
    lunghezza_fade = static_cast<unsigned long long>(0.005 * sample_rate); // 5ms fade-out
    durata_declick = static_cast<unsigned long long>(0.002 * sample_rate); 
}

void Martelletto::reset() {
    stato = StatoMartelletto::IDLE;
    nota_colpita = nullptr;
    buffer_corrente = nullptr;
    indice_lettura = 0;
    inviluppo = 0.0f;
    guadagno_velocity = 0.0f;
    ultimo_campione_uscita = 0.0f;
    valore_residuo_pop = 0.0f;
    campioni_declick = 0;
    indice_nota = 0;
}

void Martelletto::colpisci(size_t idxNota, const NotaData* nota, float velocity) {
    if (nota == nullptr) {
        return; // Non armare il martelletto se la nota non esiste!
    }
    if (std::isnan(velocity) || velocity <= 0.0f) {
        return; // Non armare il martelletto se la velocity è nulla o negativa!
    }

    if (nota->layers.empty()) return;
    float v_clamped = std::min(velocity, 1.0f);

    size_t idx_layer = 0;
    for (size_t i = 0; i < nota->layers.size(); ++i) {
        if (v_clamped <= nota->layers[i].soglia_velocity) {
            idx_layer = i;
            break;
        }
        idx_layer = i;
    }

    const auto* target_buffer = &nota->layers[idx_layer].buffer_campioni;

    if (target_buffer == nullptr || target_buffer->empty()) {
        return; 
    }

    if (stato != StatoMartelletto::IDLE) {
        valore_residuo_pop = ultimo_campione_uscita;
        campioni_declick = durata_declick;
    } else {
        valore_residuo_pop = 0.0f;
        campioni_declick = 0;
    }

    indice_nota = idxNota;
    nota_colpita = nota;
    buffer_corrente = target_buffer;
    guadagno_velocity = std::pow(v_clamped, 2.0f);

    guadagno_L = nota->guadagno_L;
    guadagno_R = nota->guadagno_R;

    stato = StatoMartelletto::IN_PERCUSSIONE;
    inviluppo = 1.0f;
    indice_lettura = 0; 
}

void Martelletto::rilascia(float tau) {
    if (stato == StatoMartelletto::IN_PERCUSSIONE || stato == StatoMartelletto::IN_SUSTAIN) {
        if(tau < 0.001f) tau = 0.001f;
        coef_rilascio = std::exp(-1.0f / (tau * sample_rate));
        stato = StatoMartelletto::IN_SMORZAMENTO;
    }
}

float Martelletto::suona() {
    if (stato == StatoMartelletto::IDLE || nota_colpita == nullptr || buffer_corrente == nullptr) {
        return 0.0f;
    }

    if (indice_lettura >= buffer_corrente->size()) {
        stato = StatoMartelletto::IDLE;
        nota_colpita = nullptr;
        buffer_corrente = nullptr;
        indice_lettura = 0;
        return 0.0f;
    }

    float campione_base = (*buffer_corrente)[indice_lettura];

    unsigned long long restanti = buffer_corrente->size() - indice_lettura;
    if (restanti < lunghezza_fade) {
        float fade_factor = static_cast<float>(restanti) / static_cast<float>(lunghezza_fade);
        campione_base *= fade_factor;
    }

    indice_lettura++;

    if (stato == StatoMartelletto::IN_SMORZAMENTO) {
        inviluppo *= coef_rilascio;   // Decadimento rapido (smorzatore in feltro)
    }

    if (inviluppo < 1e-5f) {
        reset();
        return 0.0f;
    }

    float uscita_corrente = campione_base * guadagno_velocity * inviluppo;

    if (campioni_declick > 0) {
        float fattore_declick = static_cast<float>(campioni_declick) / static_cast<float>(durata_declick);
        uscita_corrente = uscita_corrente * (1.0f - fattore_declick) + (valore_residuo_pop * fattore_declick);
        campioni_declick--;
    }

    ultimo_campione_uscita = uscita_corrente;

    return uscita_corrente;
}