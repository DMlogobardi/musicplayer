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
        meccanica_pedale.configura(sampleRateStream);
}

void HammerOrchestrator::ModelloMeccanicaPedale::configura(int sampleRate) {
    auto progettaBP = [sampleRate](float f0, float Q, Biquad& bL, Biquad& bR) {
        float w0 = 2.0f * static_cast<float>(M_PI) * f0 / static_cast<float>(sampleRate);
        float alpha = std::sin(w0) / (2.0f * Q);
        float b0 = alpha;
        float b1 = 0.0f;
        float b2 = -alpha;
        float a0 = 1.0f + alpha;
        float a1 = -2.0f * std::cos(w0);
        float a2 = 1.0f - alpha;
        bL.b0 = bR.b0 = b0 / a0;
        bL.b1 = bR.b1 = b1 / a0;
        bL.b2 = bR.b2 = b2 / a0;
        bL.a1 = bR.a1 = a1 / a0;
        bL.a2 = bR.a2 = a2 / a0;
    };

    auto progettaLP = [sampleRate](float f0, float Q, Biquad& bL, Biquad& bR) {
        float w0 = 2.0f * static_cast<float>(M_PI) * f0 / static_cast<float>(sampleRate);
        float alpha = std::sin(w0) / (2.0f * Q);
        float cos_w = std::cos(w0);
        float b0 = (1.0f - cos_w) * 0.5f;
        float b1 = 1.0f - cos_w;
        float b2 = (1.0f - cos_w) * 0.5f;
        float a0 = 1.0f + alpha;
        float a1 = -2.0f * cos_w;
        float a2 = 1.0f - alpha;
        bL.b0 = bR.b0 = b0 / a0;
        bL.b1 = bR.b1 = b1 / a0;
        bL.b2 = bR.b2 = b2 / a0;
        bL.a1 = bR.a1 = a1 / a0;
        bL.a2 = bR.a2 = a2 / a0;
    };

    progettaBP(1400.0f, 0.75f, whoosh_L, whoosh_R); // Soffio feltro a 1400 Hz
    progettaLP(135.0f, 1.3f, thud_L, thud_R);       // Impatto sordo cassa a 135 Hz
    decay_whoosh = std::exp(-1.0f / (0.13f * static_cast<float>(sampleRate)));
    decay_thud = std::exp(-1.0f / (0.075f * static_cast<float>(sampleRate)));
}

void HammerOrchestrator::ModelloMeccanicaPedale::aggiorna(float nuovaProfondita, int sampleRate) {
    float delta = nuovaProfondita - pedale_precedente;
    if (delta > 0.005f) {
        // Il pedale si abbassa: sollevamento dei feltri dalle corde (Whoosh)
        float imp = std::min(0.022f, 0.055f * delta);
        env_whoosh = std::min(0.025f, env_whoosh + imp);
    } else if (delta < -0.008f && nuovaProfondita < 0.25f) {
        // Il pedale si alza e tocca il fondo: ricaduta dei feltri sulla cassa (Thud)
        float imp = std::min(0.030f, 0.075f * (-delta));
        env_thud = std::min(0.035f, env_thud + imp);
    }
    pedale_precedente = nuovaProfondita;
}

void HammerOrchestrator::ModelloMeccanicaPedale::processa(float& outL, float& outR) {
    if (env_whoosh > 1e-6f) {
        float rL = rumoreUniforme();
        float rR = rumoreUniforme();
        outL += whoosh_L.processa(rL * env_whoosh);
        outR += whoosh_R.processa(rR * env_whoosh);
        env_whoosh *= decay_whoosh;
    }
    if (env_thud > 1e-6f) {
        float rL = rumoreUniforme();
        float rR = rumoreUniforme();
        outL += thud_L.processa(rL * env_thud);
        outR += thud_R.processa(rR * env_thud);
        env_thud *= decay_thud;
    }
}

void HammerOrchestrator::inizializzaTavolaArmonica(int sampleRate) {
    auto progettaPeaking = [sampleRate](float f0, float Q, float gainDB, Biquad& bL, Biquad& bR) {
        float A = std::pow(10.0f, gainDB / 40.0f);
        float w0 = 2.0f * static_cast<float>(M_PI) * f0 / static_cast<float>(sampleRate);
        float alpha = std::sin(w0) / (2.0f * Q);

        float b0 = 1.0f + alpha * A;
        float b1 = -2.0f * std::cos(w0);
        float b2 = 1.0f - alpha * A;
        float a0 = 1.0f + alpha / A;
        float a1 = -2.0f * std::cos(w0);
        float a2 = 1.0f - alpha / A;

        bL.b0 = bR.b0 = b0 / a0;
        bL.b1 = bR.b1 = b1 / a0;
        bL.b2 = bR.b2 = b2 / a0;
        bL.a1 = bR.a1 = a1 / a0;
        bL.a2 = bR.a2 = a2 / a0;
    };

    // 1. Risonatore profondo cassa (125 Hz, Q=1.1, Gain=+1.8 dB)
    progettaPeaking(125.0f, 1.1f, 1.8f, soundboard_bass_L, soundboard_bass_R);

    // 2. Calore corpo ligneo abete (280 Hz, Q=1.0, Gain=+1.4 dB - calore nobile senza gonfiare)
    progettaPeaking(280.0f, 1.0f, 1.4f, soundboard_body_L, soundboard_body_R);

    // 3. Presenza e proiezione del ponte di faggio/acero (2100 Hz, Q=1.2, Gain=+1.3 dB - mordente acustico)
    progettaPeaking(2100.0f, 1.2f, 1.3f, soundboard_presence_L, soundboard_presence_R);

    // 4. Assorbimento acustico naturale dell'aria (8500 Hz, Q=0.65)
    float fc_h = 8500.0f;
    float w_h = 2.0f * static_cast<float>(M_PI) * fc_h / static_cast<float>(sampleRate);
    float alpha_h = std::sin(w_h) / (2.0f * 0.65f);
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
}

void HammerOrchestrator::applicaTavolaArmonica(float& inL, float& inR) {
    // Cascata Biquad: Cassa profonda (125 Hz) -> Corpo (280 Hz) -> Presenza ponte (2100 Hz) -> Assorbimento aria (8500 Hz)
    // Scalatura a 0.78 (-2.15 dB) per mantenere il guadagno RMS integrato a 0 dB unitario,
    // preservando l'headroom ed eliminando la saturazione cronica del limiter!
    constexpr float GAIN_CORREZIONE_TAVOLA = 0.78f;

    inL = soundboard_high_L.processa(
            soundboard_presence_L.processa(
                soundboard_body_L.processa(
                    soundboard_bass_L.processa(inL)))) * GAIN_CORREZIONE_TAVOLA;

    inR = soundboard_high_R.processa(
            soundboard_presence_R.processa(
                soundboard_body_R.processa(
                    soundboard_bass_R.processa(inR)))) * GAIN_CORREZIONE_TAVOLA;
}

void HammerOrchestrator::inizializzaRisonanzaSimpatica(int sampleRate) {
    size_t numNote = dischetto_riferimento->getNumeroNote();
    risonatori_simpatia.resize(numNote);

    // Tempo di risonanza per simpatia delle corde aperte (tau = 2.5 s)
    double tau_sym = 2.5;
    double r = std::exp(-1.0 / (tau_sym * static_cast<double>(sampleRate)));
    double c2 = r * r;

    // Normalizzazione fisica dell'accoppiamento del ponticello in funzione della densità N di corde per ottava
    // Impedisce l'esplosione di energia di riverbero quando N aumenta (es. da 12 a 19 o 24 note)
    int nPerOttava = dischetto_riferimento->getNotePerOttava();
    if (nPerOttava <= 0) nPerOttava = 12;
    double fattoreScalaN = 12.0 / static_cast<double>(nPerOttava);
    double kappa_bridge = 0.030 * fattoreScalaN; // Guadagno di picco normalizzato ad energia costante su qualsiasi scala

    const double nyquist_sym = 0.45 * static_cast<double>(sampleRate);
    for (size_t i = 0; i < numNote; ++i) {
        const NotaData* n = dischetto_riferimento->getNota(i);
        if (n == nullptr || n->frequenza <= 0.0f) continue;

        auto configuraModo = [&](double f_harm, double peso, double& c1_out, double& c2_out, double& g_out) {
            if (f_harm >= nyquist_sym) {
                c1_out = c2_out = g_out = 0.0;
                return;
            }
            double w = 2.0 * M_PI * f_harm / static_cast<double>(sampleRate);
            double norm = (1.0 - r) * std::sqrt(std::max(1e-12, 1.0 - 2.0 * r * std::cos(2.0 * w) + c2));
            c1_out = 2.0 * r * std::cos(w);
            c2_out = c2;
            g_out = norm * kappa_bridge * peso;
        };

        double f0 = static_cast<double>(n->frequenza);
        configuraModo(f0,       1.00, risonatori_simpatia[i].c1_1, risonatori_simpatia[i].c2_1, risonatori_simpatia[i].g_1);
        configuraModo(f0 * 2.0, 0.45, risonatori_simpatia[i].c1_2, risonatori_simpatia[i].c2_2, risonatori_simpatia[i].g_2);
        configuraModo(f0 * 3.0, 0.22, risonatori_simpatia[i].c1_3, risonatori_simpatia[i].c2_3, risonatori_simpatia[i].g_3);

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
        // Una corda colpita non risuona per simpatia con sé stessa (la sua dinamica è già nel buffer).
        // La simpatia del ponticello agisce ESCLUSIVAMENTE sulle ALTRE corde aperte non suonate.
        if (i < tasti_premuti.size() && tasti_premuti[i]) {
            continue;
        }

        // Smorzatore alzato guidato dall'inviluppo continuo anti-click del pedale
        bool smorzatoreAlzato = (pedale_smooth > 0.05f);

        if (!smorzatoreAlzato) {
            // Smorzatore a contatto: il feltro dissipa dolcemente l'energia residua del risonatore
            if (risonatori_simpatia[i].isAttivo()) {
                risonatori_simpatia[i].attenua();
            }
            continue;
        }

        // Condizione fisica 2 — la corda NON deve essere colpita direttamente da un martelletto.
        bool direttamenteAttiva = false;
        for (size_t k = 0; k < num_voci_attive; ++k) {
            if (martelletti[voci_attive_indici[k]].getIndiceNota() == i) {
                direttamenteAttiva = true;
                break;
            }
        }

        if (direttamenteAttiva) {
            risonatori_simpatia[i].dissipa();
            continue;
        }

        // La corda è libera con smorzatore alzato: risuona per simpatia
        float y = risonatori_simpatia[i].processa(inBridge);
        sym_L += y * risonatori_simpatia[i].guadagno_L;
        sym_R += y * risonatori_simpatia[i].guadagno_R;
    }

    // Moltiplicazione continua per pedale_smooth: garantisce continuità C1 ed elimina qualsiasi pop/click
    outL += sym_L * pedale_smooth;
    outR += sym_R * pedale_smooth;
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
            // Smorzamento rapido da impatto feltro reale (14 ms): pulizia istantanea senza ritardi o code sovrapposte
            m.smorzaDaRibattuto(0.014f);
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
    meccanica_pedale.aggiorna(profondita_pedale, sample_rate);

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

    // Smoothing continuo del pedale a un polo (~20 ms a 48 kHz): elimina qualsiasi micro-click
    constexpr float alpha_p = 0.0025f;
    pedale_smooth += alpha_p * (profondita_pedale - pedale_smooth);

    // --- RISONANZA SIMPATICA E INTERAZIONE NON LINEARE DEL PONTICELLO CON ADAA ---
    // ADAA di 1° ordine sulla saturazione del legno del ponte: f(x) = x / sqrt(1 + x^2)
    // Antiderivata analitica esatta: F1(x) = sqrt(1 + x^2)
    float inBridgeLin = (somma_L + somma_R) * 0.5f;
    float x_curr = inBridgeLin * 1.25f;
    float F1_curr = std::sqrt(1.0f + x_curr * x_curr);
    float delta_x = x_curr - adaa_bridge_x1;
    float inBridgeNL;
    if (std::abs(delta_x) > 1e-5f) {
        inBridgeNL = (F1_curr - adaa_bridge_F1) / delta_x;
    } else {
        float x_mid = 0.5f * (x_curr + adaa_bridge_x1);
        inBridgeNL = x_mid / std::sqrt(1.0f + x_mid * x_mid);
    }
    adaa_bridge_x1 = x_curr;
    adaa_bridge_F1 = F1_curr;
    applicaRisonanzaSimpatica(inBridgeNL, somma_L, somma_R);

    // Iniezione del respiro meccanico della pedaliera (Lift Whoosh & Drop Thud)
    meccanica_pedale.processa(somma_L, somma_R);

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