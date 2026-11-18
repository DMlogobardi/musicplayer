#include "HammerOrchestrator.hpp"
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <utility>

#if defined(__x86_64__) || defined(_M_X64) || defined(i386) || defined(_M_IX86)
#include <xmmintrin.h>
#include <pmmintrin.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// RAII guard per abilitare FTZ (Flush-To-Zero) e DAZ (Denormals-Are-Zero) a livello hardware
struct ScopedNoDenormals {
#if defined(__x86_64__) || defined(_M_X64) || defined(i386) || defined(_M_IX86)
    unsigned int mxcsr_original;
    ScopedNoDenormals() {
        mxcsr_original = _mm_getcsr();
        _mm_setcsr(mxcsr_original | 0x8040); // Bit 15: FTZ, Bit 6: DAZ
    }
    ~ScopedNoDenormals() {
        _mm_setcsr(mxcsr_original);
    }
#else
    ScopedNoDenormals() {}
#endif
};


HammerOrchestrator::HammerOrchestrator(std::shared_ptr<const Dischetto> dischetto, int sampleRateStream, int numeroCanali)
    : dischetto_riferimento(std::move(dischetto)), sample_rate(sampleRateStream), numeroCanali(numeroCanali), free_top(MAX_VOCI) {
        if (sampleRateStream <= 0) {
            throw std::invalid_argument("Sample rate must be positive");
        }
        if (dischetto_riferimento == nullptr) {
            throw std::invalid_argument("Dischetto reference cannot be null");
        } else if (dischetto_riferimento->getSampleRate() != sampleRateStream) {
            throw std::invalid_argument("Dischetto sample rate does not match the provided sample rate");
        } else if (dischetto_riferimento->getNumeroNote() < 1) {
            throw std::invalid_argument("Dischetto must contain at least one note");
        }
        if (numeroCanali < 1) {
            throw std::invalid_argument("Number of channels must be at least 1");
        } else if (numeroCanali > 2) {
            throw std::invalid_argument("Number of channels cannot exceed 2");
        }

        size_t numero_note_reali = dischetto_riferimento->getNumeroNote();
        tasti_premuti.assign(numero_note_reali, 0);
    
        // Inizializziamo tutti i martelletti e carichiamo la free_stack con tutti gli indici: [0, 1, ..., 31]
        for (size_t i = 0; i < MAX_VOCI; ++i) {
            martelletti[i].setId(static_cast<int>(i));
            martelletti[i].configura(sampleRateStream);
            free_stack[i] = i;
        }

        inizializzaTavolaArmonica(sampleRateStream);
        inizializzaRisonanzaSimpatica(sampleRateStream);
}

void HammerOrchestrator::inizializzaTavolaArmonica(int sampleRate) {
    // 1. Filtro di picco per il corpo cassa (85 Hz, Q=1.5, Gain=+2.5 dB)
    float f0 = 85.0f;
    float Q = 1.5f;
    float gainDB = 2.5f;
    float A = std::pow(10.0f, gainDB / 40.0f);
    float w0 = 2.0f * static_cast<float>(M_PI) * f0 / static_cast<float>(sampleRate);
    float alpha = std::sin(w0) / (2.0f * Q);

    float b0_p = 1.0f + alpha * A;
    float b1_p = -2.0f * std::cos(w0);
    float b2_p = 1.0f - alpha * A;
    float a0_p = 1.0f + alpha / A;
    float a1_p = -2.0f * std::cos(w0);
    float a2_p = 1.0f - alpha / A;

    soundboard_low_L.b0 = soundboard_low_R.b0 = b0_p / a0_p;
    soundboard_low_L.b1 = soundboard_low_R.b1 = b1_p / a0_p;
    soundboard_low_L.b2 = soundboard_low_R.b2 = b2_p / a0_p;
    soundboard_low_L.a1 = soundboard_low_R.a1 = a1_p / a0_p;
    soundboard_low_L.a2 = soundboard_low_R.a2 = a2_p / a0_p;

    // 2. Filtro passa-basso naturale (7200 Hz, assorbimento acustico abete rosso)
    float fc_h = 7200.0f;
    float w_h = 2.0f * static_cast<float>(M_PI) * fc_h / static_cast<float>(sampleRate);
    float alpha_h = std::sin(w_h) / (2.0f * 0.707f);
    float cos_w_h = std::cos(w_h);

    float b0_h = (1.0f - cos_w_h) * 0.5f;
    float b1_h = 1.0f - cos_w_h;
    float b2_h = (1.0f - cos_w_h) * 0.5f;
    float a0_h = 1.0f + alpha_h;
    float a1_h = -2.0f * cos_w_h;
    float a2_h = 1.0f - alpha_h;

    soundboard_high_L.b0 = soundboard_high_R.b0 = b0_h / a0_h;
    soundboard_high_L.b1 = soundboard_high_R.b1 = b1_h / a0_h;
    soundboard_high_L.b2 = soundboard_high_R.b2 = b2_h / a0_h;
    soundboard_high_L.a1 = soundboard_high_R.a1 = a1_h / a0_h;
    soundboard_high_L.a2 = soundboard_high_R.a2 = a2_h / a0_h;

    // 3. Diffusore di fase e dispersione modale (~1.5 ms di ritardo pre-allocato)
    diff_delay = std::max<size_t>(1, static_cast<size_t>(0.0015f * static_cast<float>(sampleRate)));
    soundboard_diff_buffer_L.assign(diff_delay, 0.0f);
    soundboard_diff_buffer_R.assign(diff_delay, 0.0f);
    diff_idx = 0;
}

void HammerOrchestrator::applicaTavolaArmonica(float& inL, float& inR) {
    // Stadio 1 & 2: Risonatore cassa e assorbimento delle altissime frequenze
    float yL = soundboard_high_L.processa(soundboard_low_L.processa(inL));
    float yR = soundboard_high_R.processa(soundboard_low_R.processa(inR));

    // Stadio 3: Dispersione di fase modale All-Pass (g = 0.35)
    constexpr float g = 0.35f;
    float delayed_L = soundboard_diff_buffer_L[diff_idx];
    float delayed_R = soundboard_diff_buffer_R[diff_idx];

    float ap_out_L = -g * yL + delayed_L;
    float ap_out_R = -g * yR + delayed_R;

    soundboard_diff_buffer_L[diff_idx] = yL + g * ap_out_L;
    soundboard_diff_buffer_R[diff_idx] = yR + g * ap_out_R;

    diff_idx = (diff_idx + 1) % diff_delay;

    // Miscelazione Dry/Wet calibrata: 60% Dry (attacco corda), 40% Wet (corpo tavola)
    inL = ap_out_L;
    inR = ap_out_R;
}

void HammerOrchestrator::inizializzaRisonanzaSimpatica(int sampleRate) {
    size_t numNote = dischetto_riferimento->getNumeroNote();
    risonatori_simpatia.resize(numNote);

   // Tempo di risonanza per simpatia delle corde aperte (tau = 2.5 s)
    double tau_sym = 2.5;
    double r = std::exp(-1.0 / (tau_sym * static_cast<double>(sampleRate)));
    double c2 = r * r;
    constexpr double kappa_bridge = 0.030; // Guadagno di picco costante (~ -30.5 dB) su ogni registro

    for (size_t i = 0; i < numNote; ++i) {
        const NotaData* n = dischetto_riferimento->getNota(i);
        if (n == nullptr || n->frequenza <= 0.0f) continue;

        double w0 = 2.0 * M_PI * static_cast<double>(n->frequenza) / static_cast<double>(sampleRate);
        // Normalizzazione esatta per ottenere |H(e^{j*w0})| = kappa_bridge a qualsiasi frequenza
        double norm_picco = (1.0 - r) * std::sqrt(std::max(1e-12, 1.0 - 2.0 * r * std::cos(2.0 * w0) + c2));
        risonatori_simpatia[i].c1 = 2.0 * r * std::cos(w0);
        risonatori_simpatia[i].c2 = c2;
        risonatori_simpatia[i].g = norm_picco * kappa_bridge;
        risonatori_simpatia[i].guadagno_L = n->guadagno_L;
        risonatori_simpatia[i].guadagno_R = n->guadagno_R;
        risonatori_simpatia[i].reset();
    }
}

void HammerOrchestrator::applicaRisonanzaSimpatica(float inBridge, float& outL, float& outR) {
    size_t numNote = risonatori_simpatia.size();
    float sym_L = 0.0f;
    float sym_R = 0.0f;

    for (size_t i = 0; i < numNote; ++i) {
        // Condizione fisica 1 — smorzatore alzato (tasto premuto o pedale)
        bool smorzatoreAlzato = (profondita_pedale > 0.15f) ||
                                (i < tasti_premuti.size() && tasti_premuti[i]);

        if (!smorzatoreAlzato) {
            // Smorzatore a contatto: il feltro dissipa l'energia residua del risonatore
            if (risonatori_simpatia[i].y1 != 0.0f) {
                risonatori_simpatia[i].y1 *= 0.85f;
                risonatori_simpatia[i].y2 *= 0.85f;
                if (std::abs(risonatori_simpatia[i].y1) < 1e-15f) risonatori_simpatia[i].reset();
            }
            continue;
        }

        // Condizione fisica 2 — la corda NON deve essere colpita direttamente da un martelletto.
        // Se lo è, la sua vibrazione è già inclusa nel mix delle voci attive (somma_L/R).
        // Alimentare il risonatore i con il bridge signal (che contiene f_i) causerebbe
        // un loop di feedback positivo instabile: risonatore a f_i riceve f_i → si autoeccita.
        // La risonanza simpatica cross-corda (f_i che eccita il risonatore j, j≠i) è corretta
        // e rimane attiva perché inBridge passa comunque agli altri risonatori.
        bool direttamenteAttiva = false;
        for (size_t k = 0; k < num_voci_attive; ++k) {
            if (martelletti[voci_attive_indici[k]].getIndiceNota() == i) {
                direttamenteAttiva = true;
                break;
            }
        }

        if (direttamenteAttiva) {
            // La corda è direttamente eccitata: non si alimenta il risonatore.
            // Decadimento lentissimo per mantenere coerenza di fase quando il
            // martelletto si allontana e la corda torna a risuonare liberamente.
            risonatori_simpatia[i].y1 *= 0.9999f;
            risonatori_simpatia[i].y2 *= 0.9999f;
            continue;
        }

        // La corda è libera con smorzatore alzato: risuona per simpatia
        float y = risonatori_simpatia[i].processa(inBridge);
        sym_L += y * risonatori_simpatia[i].guadagno_L;
        sym_R += y * risonatori_simpatia[i].guadagno_R;
    }

    outL += sym_L;
    outR += sym_R;
}

bool HammerOrchestrator::noteOn(size_t indiceNota, float velocity) {
    return coda_eventi.push({ TipoEventoAudio::NOTE_ON, indiceNota, velocity });
}

bool HammerOrchestrator::noteOff(size_t indiceNota, float releaseVelocity) {
    return coda_eventi.push({ TipoEventoAudio::NOTE_OFF, indiceNota, releaseVelocity });
}

bool HammerOrchestrator::rilasciaTutto() {
    return coda_eventi.push({ TipoEventoAudio::RILASCIA_TUTTO, 0, 0.0f });
}

bool HammerOrchestrator::sustainPedal(float profondita) {
    float p_clamped = std::clamp(profondita, 0.0f, 1.0f);
    return coda_eventi.push({ TipoEventoAudio::SUSTAIN_PEDAL, 0, p_clamped });
}

inline float softClip(float x) {
    constexpr float T = 0.707f, A = 1.0f - T;
    float a = std::abs(x);
    if (a <= T) return x;
    float d = a - T;
    float y = T + A * d / (A + d);
    return x > 0.0f ? y : -y;
}

size_t HammerOrchestrator::trovaMartellettoDaRubare() {
    using S = Martelletto::StatoMartelletto;
    // Dalla meno alla più "preziosa"
    const S priorita[3] = { S::IN_SMORZAMENTO, S::IN_SUSTAIN, S::IN_PERCUSSIONE };

    for (S classe : priorita) {
        size_t scelto = MAX_VOCI;     // MAX_VOCI = "nessuno trovato"
        float min_livello = 1e9f;
        for (size_t i = 0; i < MAX_VOCI; ++i) {
            if (martelletti[i].getStato() != classe) continue;
            float l = martelletti[i].getVolume();
            if (l < min_livello) { min_livello = l; scelto = i; }
        }
        if (scelto != MAX_VOCI) return scelto;
    }
    return 0;   // irraggiungibile se free_top == 0
}

void HammerOrchestrator::aggiungiVoceAttiva(size_t index) {
    for (size_t i = 0; i < num_voci_attive; ++i) {
        if (voci_attive_indici[i] == index) return;
    }
    voci_attive_indici[num_voci_attive++] = index;
}

void HammerOrchestrator::rimuoviVoceAttiva(size_t index) {
    for (size_t i = 0; i < num_voci_attive; ++i) {
        if (voci_attive_indici[i] == index) {
            voci_attive_indici[i] = voci_attive_indici[num_voci_attive - 1];
            num_voci_attive--;
            return;
        }
    }
}

void HammerOrchestrator::eseguiNoteOnInterno(size_t indiceNota, float velocity) {
    if (dischetto_riferimento == nullptr || indiceNota >= tasti_premuti.size()) return;

    const NotaData* nota = dischetto_riferimento->getNota(indiceNota);
    if (nota == nullptr || !(velocity > 0.0f)) return;

    tasti_premuti[indiceNota] = true;

    // --- GESTIONE DEI RIBATTUTI (Task 1) ---
    // Se la corda sta già vibrando, il nuovo martelletto assorbe parte dell'energia residua
    // tramite contatto del feltro e rieccita la corda con la nuova velocity.
    float livello_residuo = 0.0f;
    for (size_t k = 0; k < num_voci_attive; ++k) {
        size_t i = voci_attive_indici[k];
        Martelletto& m = martelletti[i];
        if (!m.eLibero() && m.getIndiceNota() == indiceNota && !m.isSmorzamentoRibattuto()) {
            livello_residuo = std::max(livello_residuo, m.getVolume());
            // Smorzamento progressivo da feltro: la vecchia voce decade morbidamente in 35 ms senza click
            m.smorzaDaRibattuto(0.035f);
        }
    }

    // Bilancio energetico non lineare dell'impatto: E_eff = (1 - eta) * L_res^2 + v^2
    // eta = 0.60f rappresenta la frazione di energia assorbita dalla compressione del feltro
    constexpr float eta = 0.60f;
    float v_clamped = std::min(velocity, 1.0f);
    float energia_eff = (1.0f - eta) * (livello_residuo * livello_residuo) + (v_clamped * v_clamped);
    float velocity_effettiva = std::clamp(std::sqrt(energia_eff), 0.01f, 1.0f);

    size_t idx = 0;
    if(free_top > 0) {
        idx = free_stack[--free_top];
    } else {
        idx = trovaMartellettoDaRubare();
    }

    martelletti[idx].colpisci(indiceNota, nota, velocity_effettiva);
    aggiungiVoceAttiva(idx);
}

void HammerOrchestrator::eseguiNoteOffInterno(size_t indiceNota, float releaseVelocity) {
    if (indiceNota >= tasti_premuti.size()) return;

    tasti_premuti[indiceNota] = false;

    for (size_t i = 0; i < MAX_VOCI; ++i) {
        if (!martelletti[i].eLibero() && martelletti[i].getIndiceNota() == indiceNota) {
            // Le voci già in assorbimento da ribattuto non devono essere toccate
            if (martelletti[i].isSmorzamentoRibattuto()) continue;

            if (profondita_pedale >= 0.85f) {
                // Se il pedale è premuto a fondo, la nota passa in SUSTAIN invece di morire
                martelletti[i].setStato(Martelletto::StatoMartelletto::IN_SUSTAIN);
            } else {
                martelletti[i].rilascia(releaseVelocity, profondita_pedale);
            }
        }
    }
}

void HammerOrchestrator::eseguiSustainPedalInterno(float profondita) {
    profondita_pedale = std::clamp(profondita, 0.0f, 1.0f);
    sustain_pedal_down = (profondita_pedale > 0.5f);

    if (profondita_pedale >= 0.85f) {
        // PEDALE A FONDO: Re-pedaling (Catch pedal su note in smorzamento naturale)
        for (size_t i = 0; i < MAX_VOCI; ++i) {
            Martelletto& m = martelletti[i];
            if (!m.eLibero() && !m.isSmorzamentoRibattuto()) {
                if (m.getStato() == Martelletto::StatoMartelletto::IN_SMORZAMENTO) {
                    size_t noteIdx = m.getIndiceNota();
                    if (tasti_premuti[noteIdx]) {
                        m.setStato(Martelletto::StatoMartelletto::IN_PERCUSSIONE);
                    } else {
                        m.setStato(Martelletto::StatoMartelletto::IN_SUSTAIN);
                    }
                }
            }
        }
    } else {
        // PEDALE RILASCIATO O MEZZO PEDALE: modula lo smorzamento sulle note non tenute con le dita
        for (size_t i = 0; i < MAX_VOCI; ++i) {
            Martelletto& m = martelletti[i];
            if (!m.eLibero() && !m.isSmorzamentoRibattuto()) {
                size_t noteIdx = m.getIndiceNota();
                if (m.getStato() == Martelletto::StatoMartelletto::IN_SUSTAIN ||
                   (m.getStato() == Martelletto::StatoMartelletto::IN_PERCUSSIONE && !tasti_premuti[noteIdx])) {
                    m.rilascia(0.5f, profondita_pedale);
                }
            }
        }
    }
}

void HammerOrchestrator::eseguiRilasciaTuttoInterno() {
    for (size_t i = 0; i < MAX_VOCI; ++i) {
        martelletti[i].rilascia(1.0f, 0.0f);
    }
    std::fill(tasti_premuti.begin(), tasti_premuti.end(), 0);
    profondita_pedale = 0.0f;
    sustain_pedal_down = false;
}

float HammerOrchestrator::suonaSingoloCampione() {
    // Esegue l'intera catena fisica (voci attive, Tavola Armonica e Limiter)
    // ed effettua il downmix equal-power a canale singolo (-3 dB = 0.7071f)
    float cL = 0.0f;
    float cR = 0.0f;
    suonaCampioneStereo(cL, cR);
    return (cL + cR) * 0.70710678f;
}

void HammerOrchestrator::suonaCampioneStereo(float& outL, float& outR) {
    float somma_L = 0.0f;
    float somma_R = 0.0f;

    // Elabora solo le voci attive reali, evitando di scansionare 128 elementi ad ogni campione
    for (size_t k = 0; k < num_voci_attive; ) {
        size_t idx = voci_attive_indici[k];
        Martelletto& m = martelletti[idx];

        float campione = m.suona();

        if (m.eLibero()) {
            free_stack[free_top++] = idx;
            voci_attive_indici[k] = voci_attive_indici[num_voci_attive - 1];
            num_voci_attive--;
        } else {
            somma_L += campione * m.getGuadagnoL();
            somma_R += campione * m.getGuadagnoR();
            k++;
        }
    }

    float master = guadagno_master.load(std::memory_order_relaxed);
    somma_L *= master;
    somma_R *= master;

    // --- RISONANZA SIMPATICA TRA CORDE LIBERE (Task 6) ---
    // Il ponticello trasmette la vibrazione delle corde attive alle altre corde con feltri alzati
    float inBridge = (somma_L + somma_R) * 0.5f;
    applicaRisonanzaSimpatica(inBridge, somma_L, somma_R);

    // --- STADIO TAVOLA ARMONICA MASTER (Task 7) ---
    // Fa risuonare la cassa di abete rosso prima del limiting finale
    applicaTavolaArmonica(somma_L, somma_R);

    // --- LIMITATORE MASTER STEREO-LINKED (Task 4) ---
    // Calcola il fattore di attenuazione comune g sul picco massimo tra i due canali:
    // M = max(|L|, |R|). Se M > T, g = softClip(M) / M; altrimenti g = 1.0.
    // Applicando lo stesso g a entrambi i canali, il rapporto L/R rimane inalterato:
    // Out_L / Out_R = (L * g) / (R * g) = L / R.
    // L'immagine stereo e la posizione spaziale dei registri rimangono perfettamente stabili.
    constexpr float T = 0.707f;
    float picco_max = std::max(std::abs(somma_L), std::abs(somma_R));
    if (picco_max > T) {
        float picco_compresso = softClip(picco_max);
        float fattore_guadagno = picco_compresso / picco_max;
        somma_L *= fattore_guadagno;
        somma_R *= fattore_guadagno;
    }

    outL = somma_L;
    outR = somma_R;
}

void HammerOrchestrator::processaBlocco(float* bufferOutput, size_t numeroFrame) {
    // Task 7: Protezione hardware immediata contro i denormali su CPU x86/ARM
    ScopedNoDenormals noDenormalsGuard;

    EventoAudio ev;
    while (coda_eventi.pop(ev)) {
        switch (ev.tipo) {
            case TipoEventoAudio::NOTE_ON:
                eseguiNoteOnInterno(ev.indiceNota, ev.velocity);
                break;
            case TipoEventoAudio::NOTE_OFF:
                eseguiNoteOffInterno(ev.indiceNota, ev.velocity);
                break;
            case TipoEventoAudio::SUSTAIN_PEDAL:
                eseguiSustainPedalInterno(ev.velocity);
                break;
            case TipoEventoAudio::RILASCIA_TUTTO:
                eseguiRilasciaTuttoInterno();
                break;
        }
    }

    if (numeroCanali == 1) {
        // --- CASO MONO (1 Canale) ---
        for (size_t i = 0; i < numeroFrame; ++i) {
            bufferOutput[i] = suonaSingoloCampione();
        }
    } else {
        // --- CASO STEREO INTERLEAVED (2 Canali) ---
        for (size_t i = 0; i < numeroFrame; ++i) {
            float cL, cR;
            suonaCampioneStereo(cL, cR);
            bufferOutput[i * 2]     = cL; // Canale Sinistro (L)
            bufferOutput[i * 2 + 1] = cR; // Canale Destro (R)
        }
    }
    voci_attive.store(MAX_VOCI - free_top, std::memory_order_relaxed);
}

size_t HammerOrchestrator::getNumeroVoci() const {
    return MAX_VOCI;
}

size_t HammerOrchestrator::getVociAttive() const {
    return voci_attive.load(std::memory_order_relaxed);
}