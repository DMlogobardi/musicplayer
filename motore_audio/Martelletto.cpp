#include "Martelletto.hpp"
#include <stdexcept>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static constexpr float FREQ_SENZA_SMORZATORI = 1400.0f;

Martelletto::Martelletto() 
    : id(-1), nota_colpita(nullptr), buffer_layer_inf(nullptr), buffer_layer_sup(nullptr), peso_layer_inf(1.0f), peso_layer_sup(0.0f), guadagno_velocity(0.0f), stato(StatoMartelletto::IDLE), indice_lettura(0) {}

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
    return livello;
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
    durata_attacco = static_cast<unsigned long long>(0.002 * sample_rate); // 2ms micro-attacco anti-click

    coef_livello = std::exp(-1.0f / (0.05f * static_cast<float>(sample_rate)));
}

void Martelletto::reset() {
    stato = StatoMartelletto::IDLE;
    nota_colpita = nullptr;
    buffer_layer_inf = nullptr;
    buffer_layer_sup = nullptr;
    peso_layer_inf = 1.0f;
    peso_layer_sup = 0.0f;
    indice_lettura = 0;
    inviluppo = 0.0f;
    guadagno_velocity = 0.0f;
    ultimo_campione_uscita = 0.0f;
    valore_residuo_pop = 0.0f;
    campioni_declick = 0;
    indice_nota = 0;
    livello = 0.0f;
    filtro_smorzatore_stato = 0.0f;
    alpha_smorzatore = 0.0f;
    smorzamento_ribattuto = false;
    attacco_da_ribattuto = false;
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

    // Task 3: Crossfade continuo tra layer adiacenti per preservare la continuità timbrica
    size_t numLayers = nota->layers.size();
    size_t idx_inf = 0;
    size_t idx_sup = 0;
    float w_inf = 1.0f;
    float w_sup = 0.0f;

    if (numLayers == 1) {
        idx_inf = 0;
        idx_sup = 0;
        w_inf = 1.0f;
        w_sup = 0.0f;
    } else if (v_clamped <= nota->layers.front().soglia_velocity) {
        idx_inf = 0;
        idx_sup = 0;
        w_inf = 1.0f;
        w_sup = 0.0f;
    } else if (v_clamped >= nota->layers.back().soglia_velocity) {
        idx_inf = numLayers - 1;
        idx_sup = numLayers - 1;
        w_inf = 1.0f;
        w_sup = 0.0f;
    } else {
        for (size_t i = 0; i < numLayers - 1; ++i) {
            float s_inf = nota->layers[i].soglia_velocity;
            float s_sup = nota->layers[i + 1].soglia_velocity;
            if (v_clamped >= s_inf && v_clamped <= s_sup) {
                idx_inf = i;
                idx_sup = i + 1;
                float intervallo = s_sup - s_inf;
                float t = (intervallo > 1e-5f) ? ((v_clamped - s_inf) / intervallo) : 0.0f;
                // Crossfade Equal-Power a energia costante: preserva densità acustica ed elimina cali o phasing
                float angolo = t * (static_cast<float>(M_PI) * 0.5f);
                w_inf = std::cos(angolo);
                w_sup = std::sin(angolo);
                break;
            }
        }
    }

    buffer_layer_inf = &nota->layers[idx_inf].buffer_campioni;
    buffer_layer_sup = &nota->layers[idx_sup].buffer_campioni;
    peso_layer_inf = w_inf;
    peso_layer_sup = w_sup;

    if ((buffer_layer_inf == nullptr || buffer_layer_inf->empty()) &&
        (buffer_layer_sup == nullptr || buffer_layer_sup->empty())) {
        return;
    }

    // Registra se il Note-On interrompe una voce già attiva (ribattuto/steal)
    // Il flag guida la rampa d'attacco in suona(): solo i ribattuti la usano,
    // i Note-On freschi lasciano passare il transiente d'impatto integro.
    attacco_da_ribattuto = (stato != StatoMartelletto::IDLE);

    if (attacco_da_ribattuto) {
        valore_residuo_pop = ultimo_campione_uscita;
        campioni_declick = durata_declick;
    } else {
        valore_residuo_pop = 0.0f;
        campioni_declick = 0;
    }

    indice_nota = idxNota;
    nota_colpita = nota;
    // Calibrazione dinamica reale gran coda (30 dB di escursione tra pp e ff)
    guadagno_velocity = std::pow(v_clamped, 1.75f);
    livello = guadagno_velocity;

    guadagno_L = nota->guadagno_L;
    guadagno_R = nota->guadagno_R;

    stato = StatoMartelletto::IN_PERCUSSIONE;
    inviluppo = 1.0f;
    indice_lettura = 0; 
    filtro_smorzatore_stato = 0.0f;
    alpha_smorzatore = 0.0f;
    smorzamento_ribattuto = false;
}

void Martelletto::rilascia(float releaseVelocity, float profonditaPedale) {
    if (stato != StatoMartelletto::IN_PERCUSSIONE && stato != StatoMartelletto::IN_SUSTAIN) return;

    const float f = (nota_colpita != nullptr) ? nota_colpita->frequenza : 261.63f;

    // Se la nota è nel registro acuto estremo (> 1400 Hz), non ci sono smorzatori meccanici
    if (f > FREQ_SENZA_SMORZATORI) {
        coef_rilascio = 1.0f;
        alpha_smorzatore = 0.0f;
        smorzamento_ribattuto = false;
        stato = StatoMartelletto::IN_SMORZAMENTO;
        return;
    }

    // Se il pedale è premuto a fondo (P >= 0.85), la nota resta in sustain libero
    if (profonditaPedale >= 0.85f) {
        stato = StatoMartelletto::IN_SUSTAIN;
        return;
    }

    // Tempo di smorzamento naturale del feltro: Do2 ≈ 0.75s, Do4 ≈ 0.45s, Do6 ≈ 0.30s
    float tau_base = std::clamp(0.45f * std::pow(261.63f / f, 0.35f), 0.22f, 0.95f);

    float fattore_pedale = 1.0f;
    if (profonditaPedale > 0.15f) {
        float t_pedale = (profonditaPedale - 0.15f) / 0.70f;
        fattore_pedale = 1.0f + 5.0f * t_pedale; // Decadimento prolungato fino a 6x nel mezzo pedale
    }

    float v_rel = std::clamp(releaseVelocity, 0.05f, 1.0f);
    float fattore_rilascio = 1.35f - 0.50f * v_rel; // Variazione dolce tra 1.3x e 0.85x

    float tau_effettivo = std::max(0.12f, tau_base * fattore_pedale * fattore_rilascio);
    coef_rilascio = std::exp(-1.0f / (tau_effettivo * static_cast<float>(sample_rate)));

    // Assorbimento acustico progressivo (taglio morbido senza salti a scatto)
    float fc = std::clamp(f * 4.5f + 400.0f, 600.0f, 9000.0f);
    float w = 2.0f * static_cast<float>(M_PI) * fc / static_cast<float>(sample_rate);
    alpha_smorzatore = std::exp(-w);

    smorzamento_ribattuto = false;
    stato = StatoMartelletto::IN_SMORZAMENTO;
}

void Martelletto::smorzaDaRibattuto(float tauSecondi) {
    if (stato == StatoMartelletto::IDLE) return;

    const float f = (nota_colpita != nullptr) ? nota_colpita->frequenza : 261.63f;
    if (tauSecondi < 0.001f) tauSecondi = 0.001f;

    // Assorbimento ultra-rapido da feltro del martelletto (15-25 ms)
    coef_rilascio = std::exp(-1.0f / (tauSecondi * static_cast<float>(sample_rate)));

    // Taglio energico: il feltro del martelletto schiaccia la corda
    float fc = std::clamp(f * 0.8f, 60.0f, 2000.0f);
    float w = 2.0f * static_cast<float>(M_PI) * fc / static_cast<float>(sample_rate);
    alpha_smorzatore = std::exp(-w);

    smorzamento_ribattuto = true;
    stato = StatoMartelletto::IN_SMORZAMENTO;
}


float Martelletto::suona() {
    if (stato == StatoMartelletto::IDLE || nota_colpita == nullptr || 
        (buffer_layer_inf == nullptr && buffer_layer_sup == nullptr)) {
        return 0.0f;
    }

    bool fine_inf = (buffer_layer_inf == nullptr || indice_lettura >= buffer_layer_inf->size());
    bool fine_sup = (buffer_layer_sup == nullptr || indice_lettura >= buffer_layer_sup->size());

    if (fine_inf && fine_sup) {
        reset();
        return 0.0f;
    }

    float s_inf = (!fine_inf) ? (*buffer_layer_inf)[indice_lettura] : 0.0f;
    float s_sup = (!fine_sup) ? (*buffer_layer_sup)[indice_lettura] : 0.0f;
    float campione_base = peso_layer_inf * s_inf + peso_layer_sup * s_sup;

    // --- MICRO-RAMPA DI ATTACCO (Anti-Click) — solo per ribattuti/steal ---
    // Nei Note-On freschi (da IDLE) il transiente fisico del martelletto deve uscire
    // integro: il buffer parte già con la fisica del colpo, non va soffocato.
    // La rampa coseno serve SOLO quando il martelletto interrompe una voce già attiva
    // (ribattuto o voice-stealing), dove il declick crossfade ha bisogno che il segnale
    // in ingresso parta da zero per raccordarsi con il residuo della voce precedente.
    if (attacco_da_ribattuto && indice_lettura < durata_attacco) {
        float f_att = static_cast<float>(indice_lettura) / static_cast<float>(durata_attacco);
        float rampa = 0.5f * (1.0f - std::cos(f_att * static_cast<float>(M_PI)));
        campione_base *= rampa;
    }

    size_t dim_max = 0;
    if (buffer_layer_inf) dim_max = std::max(dim_max, buffer_layer_inf->size());
    if (buffer_layer_sup) dim_max = std::max(dim_max, buffer_layer_sup->size());

    if (dim_max > indice_lettura) {
        unsigned long long restanti = dim_max - indice_lettura;
        if (restanti < lunghezza_fade) {
            float fade_factor = static_cast<float>(restanti) / static_cast<float>(lunghezza_fade);
            campione_base *= fade_factor;
        }
    }

    indice_lettura++;

    float campione_processato = campione_base;

    if (stato == StatoMartelletto::IN_SMORZAMENTO) {
        inviluppo *= coef_rilascio;   // Decadimento ampiezza generale

        // Filtro passa-basso a polo singolo: y[n] = (1 - alpha) * x[n] + alpha * y[n-1]
        if (alpha_smorzatore > 0.0f) {
            filtro_smorzatore_stato = (1.0f - alpha_smorzatore) * campione_base + alpha_smorzatore * filtro_smorzatore_stato;
            // Protezione denormali sullo stato del filtro IIR
            if (std::abs(filtro_smorzatore_stato) < 1e-15f) {
                filtro_smorzatore_stato = 0.0f;
            }
            campione_processato = filtro_smorzatore_stato;
        }
    } else {
        // Mantiene la memoria del filtro allineata al campione corrente per evitare salti di fase
        filtro_smorzatore_stato = campione_base;
    }

    if (inviluppo < 1e-5f) {
        reset();
        return 0.0f;
    }

    float uscita_corrente = campione_processato * guadagno_velocity * inviluppo;

    if (campioni_declick > 0) {
        float fattore_declick = static_cast<float>(campioni_declick) / static_cast<float>(durata_declick);
        uscita_corrente = uscita_corrente * (1.0f - fattore_declick) + (valore_residuo_pop * fattore_declick);
        campioni_declick--;
    }

    ultimo_campione_uscita = uscita_corrente;
    livello = std::max(std::abs(uscita_corrente), livello * coef_livello);
    if (livello < 1e-6f) livello = 0.0f; // Protezione denormali sul peak follower

    return uscita_corrente;
}