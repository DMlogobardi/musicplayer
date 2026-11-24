#ifndef BUILDING_PIANO_ENGINE_DLL
#define BUILDING_PIANO_ENGINE_DLL
#endif

#include "piano_engine_c_api.h"

#include "DeviceMenager/AudioDeviceManager.hpp"
#include "motore_generativo/Masterizzatore.hpp"
#include "Dischetto.hpp"
#include "motore_audio/HammerOrchestrator.hpp"

#include <memory>
#include <string>
#include <mutex>
#include <stdexcept>
#include <algorithm>

// =============================================================================
// GESTIONE ERRORI ED ECCEZIONI C++ (Thread-Local)
// =============================================================================
static thread_local std::string g_lastError;

static void piano_set_error(const std::string& err) {
    g_lastError = err;
}

const char* piano_get_last_error(void) {
    return g_lastError.c_str();
}

void piano_clear_last_error(void) {
    g_lastError.clear();
}

// =============================================================================
// GESTIONE HARDWARE AUDIO INTERNA (Intermediario trasparente)
// =============================================================================
static std::mutex g_audioMutex;
static std::unique_ptr<AudioDeviceManager> g_deviceManager;

static AudioDeviceManager& getInternalDeviceManager() {
    std::lock_guard<std::mutex> lock(g_audioMutex);
    if (!g_deviceManager) {
        g_deviceManager = std::make_unique<AudioDeviceManager>();
        try {
            g_deviceManager->inizializzaDispositivo();
        } catch (const std::exception& e) {
            piano_set_error(std::string("Errore inizializzazione driver audio: ") + e.what());
        }
    }
    return *g_deviceManager;
}

// =============================================================================
// STRUTTURE HANDLE CONCRETE (Opaque Pointers)
// =============================================================================
struct MasterizzatoreHandle_ {
    std::unique_ptr<Masterizzatore> masterizzatore;
};

struct DischettoHandle_ {
    std::shared_ptr<Dischetto> dischetto;
};

struct HammerOrchestratorHandle_ {
    std::shared_ptr<HammerOrchestrator> orchestrator;
    AudioDeviceManager* deviceManager = nullptr;
};

// =============================================================================
// 2. MOTORE GENERATIVO OFFLINE (Masterizzatore)
// =============================================================================
MasterizzatoreHandle masterizzatore_create(void) {
    try {
        piano_clear_last_error();
        auto handle = new MasterizzatoreHandle_();
        handle->masterizzatore = std::make_unique<Masterizzatore>(getInternalDeviceManager());
        return handle;
    } catch (const std::exception& e) {
        piano_set_error(std::string("Eccezione durante la creazione del Masterizzatore: ") + e.what());
        return nullptr;
    } catch (...) {
        piano_set_error("Eccezione sconosciuta durante la creazione del Masterizzatore.");
        return nullptr;
    }
}

void masterizzatore_destroy(MasterizzatoreHandle handle) {
    if (handle) {
        delete handle;
    }
}

DischettoHandle masterizzatore_masterizza(
    MasterizzatoreHandle handle,
    const char* nomeDisco,
    int32_t notePerOttava,
    int32_t ottavePerLato,
    float frequenzaAncora
) {
    if (!handle || !handle->masterizzatore) {
        piano_set_error("MasterizzatoreHandle non valido o nullo.");
        return nullptr;
    }

    try {
        piano_clear_last_error();
        Masterizzatore::ConfigurazioneMasterizzazione config;
        if (nomeDisco && *nomeDisco) {
            config.nomeDischetto = nomeDisco;
        }
        config.notePerOttava = (notePerOttava > 0) ? notePerOttava : 12;
        config.ottavePerLato = (ottavePerLato >= 0) ? ottavePerLato : 3;
        config.frequenzaAncora = (frequenzaAncora > 0.0f) ? frequenzaAncora : 261.6256f;

        auto disc = handle->masterizzatore->masterizza(config);
        if (!disc) {
            piano_set_error("La sintesi fisica del Dischetto ha restituito un puntatore nullo.");
            return nullptr;
        }

        auto out = new DischettoHandle_();
        out->dischetto = disc;
        return out;
    } catch (const std::exception& e) {
        piano_set_error(std::string("Eccezione durante masterizza: ") + e.what());
        return nullptr;
    } catch (...) {
        piano_set_error("Eccezione sconosciuta durante masterizza.");
        return nullptr;
    }
}

// =============================================================================
// 3. SOUNDBANK FISICA PRE-CALCOLATA (Dischetto)
// =============================================================================
void dischetto_destroy(DischettoHandle handle) {
    if (handle) {
        delete handle;
    }
}

int32_t dischetto_get_numero_note(DischettoHandle handle) {
    if (!handle || !handle->dischetto) return 0;
    return static_cast<int32_t>(handle->dischetto->getNumeroNote());
}

int32_t dischetto_get_note_per_ottava(DischettoHandle handle) {
    if (!handle || !handle->dischetto) return 0;
    return handle->dischetto->getNotePerOttava();
}

int32_t dischetto_get_indice_ancora(DischettoHandle handle) {
    if (!handle || !handle->dischetto) return 0;
    return static_cast<int32_t>(handle->dischetto->getIndiceAncora());
}

int32_t dischetto_get_ottave_per_lato(DischettoHandle handle) {
    if (!handle || !handle->dischetto) return 0;
    return handle->dischetto->getOttavePerLato();
}

int32_t dischetto_get_sample_rate(DischettoHandle handle) {
    if (!handle || !handle->dischetto) return 0;
    return handle->dischetto->getSampleRate();
}

const char* dischetto_get_nome(DischettoHandle handle) {
    if (!handle || !handle->dischetto) return "";
    return handle->dischetto->getNomeDisco().c_str();
}

float dischetto_get_nota_frequenza(DischettoHandle handle, int32_t index) {
    if (!handle || !handle->dischetto || index < 0) return 0.0f;
    const auto* nota = handle->dischetto->getNota(static_cast<size_t>(index));
    return nota ? nota->frequenza : 0.0f;
}

const char* dischetto_get_nota_nome(DischettoHandle handle, int32_t index) {
    if (!handle || !handle->dischetto || index < 0) return "";
    const auto* nota = handle->dischetto->getNota(static_cast<size_t>(index));
    return nota ? nota->nome.c_str() : "";
}

// =============================================================================
// 4. MOTORE ESECUTIVO REAL-TIME (HammerOrchestrator)
// =============================================================================
HammerOrchestratorHandle orchestrator_create(DischettoHandle dischettoHandle) {
    if (!dischettoHandle || !dischettoHandle->dischetto) {
        piano_set_error("Dischetto non valido o nullo fornito all'Orchestratore.");
        return nullptr;
    }

    try {
        piano_clear_last_error();
        auto& dev = getInternalDeviceManager();
        int sr = dev.getSampleRate();
        int canali = dev.getNumeroCanali();
        if (sr <= 0) sr = dischettoHandle->dischetto->getSampleRate();
        if (canali <= 0) canali = 2;

        auto orch = std::make_shared<HammerOrchestrator>(dischettoHandle->dischetto, sr, canali);
        orch->setGuadagnoMaster(0.50f); // Headroom naturale a -6 dB

        auto handle = new HammerOrchestratorHandle_();
        handle->orchestrator = orch;
        handle->deviceManager = &dev;

        dev.setOrchestrator(orch.get());
        if (!dev.isStreamAttivo()) {
            dev.avviaStream();
        }

        return handle;
    } catch (const std::exception& e) {
        piano_set_error(std::string("Eccezione durante la creazione dell'HammerOrchestrator: ") + e.what());
        return nullptr;
    } catch (...) {
        piano_set_error("Eccezione sconosciuta durante la creazione dell'HammerOrchestrator.");
        return nullptr;
    }
}

void orchestrator_destroy(HammerOrchestratorHandle handle) {
    if (!handle) return;
    try {
        if (handle->deviceManager) {
            handle->deviceManager->setOrchestrator(nullptr);
        }
    } catch (...) {}
    delete handle;
}

bool orchestrator_cambia_dischetto(HammerOrchestratorHandle handle, DischettoHandle nuovoDischetto) {
    if (!handle || !handle->orchestrator || !nuovoDischetto || !nuovoDischetto->dischetto) {
        piano_set_error("Handle non valido fornito a orchestrator_cambia_dischetto.");
        return false;
    }

    try {
        piano_clear_last_error();
        auto& dev = getInternalDeviceManager();
        int sr = dev.getSampleRate();
        int canali = dev.getNumeroCanali();
        if (sr <= 0) sr = nuovoDischetto->dischetto->getSampleRate();
        if (canali <= 0) canali = 2;

        float gain = handle->orchestrator->getGuadagnoMaster();
        auto nuovoOrch = std::make_shared<HammerOrchestrator>(nuovoDischetto->dischetto, sr, canali);
        nuovoOrch->setGuadagnoMaster(gain);

        dev.setOrchestrator(nuovoOrch.get());
        handle->orchestrator = nuovoOrch;
        return true;
    } catch (const std::exception& e) {
        piano_set_error(std::string("Eccezione durante il cambio Dischetto: ") + e.what());
        return false;
    } catch (...) {
        piano_set_error("Eccezione sconosciuta durante il cambio Dischetto.");
        return false;
    }
}

void orchestrator_note_on(HammerOrchestratorHandle handle, int32_t noteIndex, float velocity) {
    if (handle && handle->orchestrator && noteIndex >= 0) {
        handle->orchestrator->noteOn(static_cast<size_t>(noteIndex), velocity);
    }
}

void orchestrator_note_off(HammerOrchestratorHandle handle, int32_t noteIndex, float releaseVelocity) {
    if (handle && handle->orchestrator && noteIndex >= 0) {
        handle->orchestrator->noteOff(static_cast<size_t>(noteIndex), releaseVelocity);
    }
}

void orchestrator_sustain_pedal(HammerOrchestratorHandle handle, float depth) {
    if (handle && handle->orchestrator) {
        handle->orchestrator->sustainPedal(depth);
    }
}

void orchestrator_rilascia_tutto(HammerOrchestratorHandle handle) {
    if (handle && handle->orchestrator) {
        handle->orchestrator->rilasciaTutto();
    }
}

void orchestrator_set_guadagno_master(HammerOrchestratorHandle handle, float gain) {
    if (handle && handle->orchestrator) {
        handle->orchestrator->setGuadagnoMaster(gain);
    }
}

float orchestrator_get_guadagno_master(HammerOrchestratorHandle handle) {
    if (handle && handle->orchestrator) {
        return handle->orchestrator->getGuadagnoMaster();
    }
    return 0.0f;
}

int32_t orchestrator_get_voci_attive(HammerOrchestratorHandle handle) {
    if (handle && handle->orchestrator) {
        return static_cast<int32_t>(handle->orchestrator->getVociAttive());
    }
    return 0;
}

int32_t orchestrator_get_numero_voci(HammerOrchestratorHandle handle) {
    if (handle && handle->orchestrator) {
        return static_cast<int32_t>(handle->orchestrator->getNumeroVoci());
    }
    return 128;
}

bool orchestrator_avvia_audio(HammerOrchestratorHandle handle) {
    if (handle && handle->deviceManager) {
        return handle->deviceManager->avviaStream();
    }
    return false;
}

void orchestrator_ferma_audio(HammerOrchestratorHandle handle) {
    if (handle && handle->deviceManager) {
        handle->deviceManager->fermaStream();
    }
}

bool orchestrator_is_audio_attivo(HammerOrchestratorHandle handle) {
    if (handle && handle->deviceManager) {
        return handle->deviceManager->isStreamAttivo();
    }
    return false;
}
