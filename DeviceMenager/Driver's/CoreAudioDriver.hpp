#pragma once
#ifndef COREAUDIODRIVER_HPP
#define COREAUDIODRIVER_HPP

#include "IAudioDriver.hpp"
#include <atomic>

class AudioDeviceManager;

class CoreAudioDriver : public IAudioDriver {
public:
    explicit CoreAudioDriver(AudioDeviceManager* manager);
    ~CoreAudioDriver() override;

    bool inizializza() override;
    bool avvia() override;
    bool ferma() override;
    bool isAttivo() const override {return attivo.load(std::memory_order_relaxed); }

    int getSampleRateReale() const override { return sampleRate; }
    int getBufferSizeReale() const override { return bufferSize; }
    int getNumeroCanaliReale() const override { return numeroCanali; }

private:
    AudioDeviceManager* manager = nullptr;
    std::atomic<bool> attivo{false};
    int sampleRate = 0;
    int bufferSize = 0;
    int numeroCanali = 0;

    // Qui inseriremo i riferimenti ad AudioComponentInstance / AudioUnit di Apple
};

#endif // COREAUDIODRIVER_HPP