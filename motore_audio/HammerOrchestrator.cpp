#include "HammerOrchestrator.hpp"
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <utility>


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
}

bool HammerOrchestrator::noteOn(size_t indiceNota, float velocity) {
    return coda_eventi.push({ TipoEventoAudio::NOTE_ON, indiceNota, velocity });
}

bool HammerOrchestrator::noteOff(size_t indiceNota) {
    return coda_eventi.push({ TipoEventoAudio::NOTE_OFF, indiceNota, 0.0f });
}

bool HammerOrchestrator::rilasciaTutto() {
    return coda_eventi.push({ TipoEventoAudio::RILASCIA_TUTTO, 0, 0.0f });
}

bool HammerOrchestrator::sustainPedal(bool premuto) {
    return coda_eventi.push({ TipoEventoAudio::SUSTAIN_PEDAL, 0, premuto ? 1.0f : 0.0f });
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

    size_t idx = 0;
    if(free_top > 0) {
        idx = free_stack[--free_top];
    } else {
        idx = trovaMartellettoDaRubare();
    }

    martelletti[idx].colpisci(indiceNota,nota, velocity);
    aggiungiVoceAttiva(idx);
}

void HammerOrchestrator::eseguiNoteOffInterno(size_t indiceNota) {
    if (indiceNota >= tasti_premuti.size()) return;

    tasti_premuti[indiceNota] = false;

    for (size_t i = 0; i < MAX_VOCI; ++i) {
        if (!martelletti[i].eLibero() && martelletti[i].getIndiceNota() == indiceNota) {
            if (sustain_pedal_down) {
                // Se il pedale è premuto, la nota passa in SUSTAIN invece di morire
                martelletti[i].setStato(Martelletto::StatoMartelletto::IN_SUSTAIN);
            } else {
                martelletti[i].rilascia();
            }
        }
    }
}

void HammerOrchestrator::eseguiSustainPedalInterno(bool premuto) {
    sustain_pedal_down = premuto;

    if (sustain_pedal_down) {
        // PEDALE PREMUTO: Re-pedaling (Catch pedal su note in smorzamento)
        for (size_t i = 0; i < MAX_VOCI; ++i) {
            Martelletto& m = martelletti[i];
            if (!m.eLibero()) {
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
        // PEDALE RILASCIATO: Abbassa gli smorzatori su tutte le note non tenute con le dita
        for (size_t i = 0; i < MAX_VOCI; ++i) {
            Martelletto& m = martelletti[i];
            if (!m.eLibero()) {
                size_t noteIdx = m.getIndiceNota();
                if (m.getStato() == Martelletto::StatoMartelletto::IN_SUSTAIN ||
                   (m.getStato() == Martelletto::StatoMartelletto::IN_PERCUSSIONE && !tasti_premuti[noteIdx])) {
                    m.rilascia();
                }
            }
        }
    }
}

void HammerOrchestrator::eseguiRilasciaTuttoInterno() {
    for (size_t i = 0; i < MAX_VOCI; ++i) {
        martelletti[i].rilascia(0.05f);
    }
    std::fill(tasti_premuti.begin(), tasti_premuti.end(), 0);
    sustain_pedal_down = false;
}

float HammerOrchestrator::suonaSingoloCampione() {
    float somma = 0.0f;

    // Scorre SOLO le voci realmente attive
    for (size_t k = 0; k < num_voci_attive; ) {
        size_t idx = voci_attive_indici[k];
        Martelletto& m = martelletti[idx];

        float campione = m.suona();

        if (m.eLibero()) {
            free_stack[free_top++] = idx;
            voci_attive_indici[k] = voci_attive_indici[num_voci_attive - 1]; // Rimozione in O(1)
            num_voci_attive--;
        } else {
            somma += campione;
            k++;
        }
    }

    somma *= guadagno_master.load(std::memory_order_relaxed);
    somma = softClip(somma);

    return somma;
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

    somma_L = softClip(somma_L);
    somma_R = softClip(somma_R);

    outL = somma_L;
    outR = somma_R;
}

void HammerOrchestrator::processaBlocco(float* bufferOutput, size_t numeroFrame) {
    EventoAudio ev;
    while (coda_eventi.pop(ev)) {
        switch (ev.tipo) {
            case TipoEventoAudio::NOTE_ON:
                eseguiNoteOnInterno(ev.indiceNota, ev.velocity);
                break;
            case TipoEventoAudio::NOTE_OFF:
                eseguiNoteOffInterno(ev.indiceNota);
                break;
            case TipoEventoAudio::SUSTAIN_PEDAL:
                eseguiSustainPedalInterno(ev.velocity > 0.5f); // <-- AGGIUNTO
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