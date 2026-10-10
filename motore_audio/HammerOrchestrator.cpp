#include "HammerOrchestrator.hpp"
#include <stdexcept>
#include <cmath>

HammerOrchestrator::HammerOrchestrator(const Dischetto* dischetto, int sampleRateStream, int numeroCanali)
    : dischetto_riferimento(dischetto), sample_rate(sampleRateStream), numeroCanali(numeroCanali), free_top(MAX_VOCI) {
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
    
        // Inizializziamo tutti i martelletti e carichiamo la free_stack con tutti gli indici: [0, 1, ..., 31]
        for (size_t i = 0; i < MAX_VOCI; ++i) {
            martelletti[i].setId(static_cast<int>(i));
            martelletti[i].configura(sampleRateStream);
            free_stack[i] = i;
        }
}

void HammerOrchestrator::noteOn(size_t indiceNota, float velocity) {
    coda_eventi.push({ TipoEventoAudio::NOTE_ON, indiceNota, velocity });
}

void HammerOrchestrator::noteOff(size_t indiceNota) {
    coda_eventi.push({ TipoEventoAudio::NOTE_OFF, indiceNota, 0.0f });
}

void HammerOrchestrator::rilasciaTutto() {
    coda_eventi.push({ TipoEventoAudio::RILASCIA_TUTTO, 0, 0.0f });
}

size_t HammerOrchestrator::trovaMartellettoDaRubare() {
    size_t indice_scelto = 0;
    float min_volume = 2.0f;

    // 1. Cerca tra le note in smorzamento (tasto già rilasciato dall'utente) quella col volume più basso
    for (size_t i = 0; i < MAX_VOCI; ++i) {
        if (martelletti[i].getStato() == Martelletto::StatoMartelletto::IN_SMORZAMENTO) {
            if (martelletti[i].getVolume() < min_volume) {
                min_volume = martelletti[i].getVolume();
                indice_scelto = i;
            }
        }
    }

    if (min_volume < 2.0f) {
        return indice_scelto;
    }

    // 2. Extrema Ratio: Se tutti e 32 i martelletti sono tenuti premuti, ruba la voce col volume più basso
    min_volume = 2.0f;
    for (size_t i = 0; i < MAX_VOCI; ++i) {
        if (martelletti[i].getVolume() < min_volume) {
            min_volume = martelletti[i].getVolume();
            indice_scelto = i;
        }
    }

    return indice_scelto;
}


void HammerOrchestrator::eseguiNoteOnInterno(size_t indiceNota, float velocity) {
    if (dischetto_riferimento == nullptr) return;

    const NotaData* nota = dischetto_riferimento->getNota(indiceNota);
    if (nota == nullptr || velocity <= 0.0f) return;

    size_t idx = 0;
    if(free_top > 0) {
        idx = free_stack[--free_top];
    } else {
        idx = trovaMartellettoDaRubare();
    }

    martelletti[idx].colpisci(nota, velocity);
}

void HammerOrchestrator::eseguiNoteOffInterno(size_t indiceNota) {
    if (dischetto_riferimento == nullptr) return;

    const NotaData* nota = dischetto_riferimento->getNota(indiceNota);
    if (nota == nullptr) return;

    for(size_t i = 0; i < MAX_VOCI; ++i) {
        if (martelletti[i].getNota() == nota && martelletti[i].getStato() == Martelletto::StatoMartelletto::IN_PERCUSSIONE) {
            martelletti[i].rilascia();
        }
    }
}

void HammerOrchestrator::eseguiRilasciaTuttoInterno() {
    for (size_t i = 0; i < MAX_VOCI; ++i) {
        martelletti[i].rilascia();
    }
}

float HammerOrchestrator::suonaSingoloCampione() {
    float somma = 0.0f;

    for (size_t i = 0; i < MAX_VOCI; ++i) {
        Martelletto& m = martelletti[i];

        if (!m.eLibero()) {
            float campione = m.suona();

            // Rilevamento automatico spegnimento:
            // Se m.suona() ha azzerato il volume e impostato lo stato a IDLE,
            // rimettiamo l'indice nella free_stack in O(1)!
            if (m.eLibero()) {
                free_stack[free_top++] = i;
            } else {
                somma += campione;
            }
        }
    }

    somma *= guadagno_master.load(std::memory_order_relaxed);

    somma = std::tanh(somma); // Soft clipping per evitare distorsioni digitali indesiderate

    return somma;
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
            float campione = suonaSingoloCampione();
            bufferOutput[i * 2]     = campione; // Canale Sinistro (L)
            bufferOutput[i * 2 + 1] = campione; // Canale Destro (R)
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