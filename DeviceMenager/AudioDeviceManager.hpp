#pragma once
#ifndef AUDIODEVICEMANAGER_HPP
#define AUDIODEVICEMANAGER_HPP

#include "HammerOrchestrator.hpp"
#include "IAudioDriver.hpp"
#include <memory>
#include <atomic>
#include <thread>

class AudioDeviceManager {
public:
    explicit AudioDeviceManager(HammerOrchestrator* orchestratore = nullptr);
    ~AudioDeviceManager();

    bool inizializzaDispositivo();

    bool avviaStream();
    bool fermaStream();
    bool isStreamAttivo() const { return streamAttivo.load(std::memory_order_relaxed); }

    // Parametri letti e scoperti dal SO
    int getSampleRate() const { return sampleRate; }
    int getBufferSize() const { return bufferSize; }
    int getNumeroCanali() const { return numeroCanali; }

    unsigned int getThreadHardware() const {
        unsigned int hw = std::thread::hardware_concurrency();
        return (hw > 0) ? hw : 1;
    }

    unsigned int getThreadWorkerConsigliati() const {
        unsigned int hw = getThreadHardware();
        if (hw <= 2) return 1;
        return std::min(hw / 2, 4u); // Usa al massimo metà CPU (tetto 4) per non bloccare SO/UI
    }

    void setOrchestrator(HammerOrchestrator* nuovoOrchestrator) {
        orchestrator.store(nuovoOrchestrator, std::memory_order_release);
    }

    /**
     * Callback ad altissima priorità richiamata dal thread audio nativo del driver.
     * DEVE essere priva di lock, allocazioni o chiamate di sistema bloccanti.
     */
    void onAudioDeviceReady(float* outputBuffer, size_t numeroFrame);

private:
    std::atomic<HammerOrchestrator*> orchestrator{nullptr};
    
    // Valori dinamici ottenuti dal SO sottostante
    int sampleRate = 0;
    int bufferSize = 0;
    int numeroCanali = 0;

    std::atomic<bool> streamAttivo{false};
    std::unique_ptr<IAudioDriver> driverNativo;
};

#endif // AUDIODEVICEMANAGER_HPP