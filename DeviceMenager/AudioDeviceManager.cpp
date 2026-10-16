#include "AudioDeviceManager.hpp"
#include "IAudioDriver.hpp"

#include <stdexcept>
#include <cstring>

#if defined(_WIN32)
    #include "WindowsAudioDriver.hpp"
#elif defined(__ANDROID__)
    #include "OboeAudioDriver.hpp"
#elif defined(__APPLE__)
    #include "CoreAudioDriver.hpp"
#endif

AudioDeviceManager::AudioDeviceManager(HammerOrchestrator* orchestratore)
    : orchestrator(orchestratore) {

    #if defined(_WIN32)
        // Su Windows crea il driver Windows
        driverNativo = std::make_unique<WindowsAudioDriver>(this);
    #elif defined(__ANDROID__)
        // Su Android crea il driver Oboe
        driverNativo = std::make_unique<OboeAudioDriver>(this);
    #elif defined(__APPLE__)
        // Su iOS/macOS crea il driver CoreAudio
        driverNativo = std::make_unique<CoreAudioDriver>(this);
    #endif

}

AudioDeviceManager::~AudioDeviceManager() {
    try { fermaStream(); } catch (...) {
        auto logError("Errore durante la chiusura dello stream audio nel distruttore di AudioDeviceManager.");
    }
}

bool AudioDeviceManager::inizializzaDispositivo() {
    if (!driverNativo) {
        throw std::runtime_error("Driver audio nativo non disponibile per questa piattaforma.");
    }

    if (!driverNativo->inizializza()) {
        return false;
    }

    // Legge i parametri reali dal driver
    sampleRate = driverNativo->getSampleRateReale();
    bufferSize = driverNativo->getBufferSizeReale();
    numeroCanali = driverNativo->getNumeroCanaliReale();

    return true;
}

bool AudioDeviceManager::avviaStream() {
    if(driverNativo && driverNativo->avvia()) {
        streamAttivo.store(true, std::memory_order_release);
        return true;
    }
    return false;
}

bool AudioDeviceManager::fermaStream() {
    if(driverNativo && streamAttivo.load(std::memory_order_acquire)) {
        bool esito = driverNativo->ferma();
        if (!esito) {
            throw std::runtime_error("Errore durante la chiusura del driver audio nativo.");
        }
        streamAttivo.store(false, std::memory_order_release);
        return true;
    }
    return false;
}

void AudioDeviceManager::onAudioDeviceReady(float* outputBuffer, size_t numeroFrame) {
    HammerOrchestrator* orch = orchestrator.load(std::memory_order_acquire);
    if (orch && streamAttivo.load(std::memory_order_acquire)) {
        orch->processaBlocco(outputBuffer, numeroFrame);
    } else {
        // Se non c'è un orchestratore, riempi il buffer con zeri (silenzio)
        std::memset(outputBuffer, 0, numeroFrame * static_cast<size_t>(numeroCanali) * sizeof(float));;
    }
}