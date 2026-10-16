#pragma once
#ifndef OBOEAUDIODRIVER_HPP
#define OBOEAUDIODRIVER_HPP

#if defined(__ANDROID__)

#include "IAudioDriver.hpp"
#include <oboe/Oboe.h>
#include <atomic>
#include <memory>

class AudioDeviceManager;

class OboeAudioDriver : public IAudioDriver, 
                        public oboe::AudioStreamDataCallback,
                        public oboe::AudioStreamErrorCallback {
public:
    explicit OboeAudioDriver(AudioDeviceManager* manager);
    ~OboeAudioDriver() override;

    bool inizializza() override;
    bool avvia() override;
    bool ferma() override;
    bool isAttivo() const override { return attivo.load(std::memory_order_relaxed); }

    int getSampleRateReale() const override { return sampleRate; }
    int getBufferSizeReale() const override { return bufferSize; }
    int getNumeroCanaliReale() const override { return numeroCanali; }

    oboe::DataCallbackResult onAudioReady(
        oboe::AudioStream* oboeStream,
        void* audioData,
        int32_t numFrames
    ) override;

    void onErrorAfterClose(
        oboe::AudioStream* oboeStream,
        oboe::Result error
    ) override;

private:
    AudioDeviceManager* manager = nullptr;
    std::atomic<bool> attivo{false};
    int sampleRate = 0;
    int bufferSize = 0;
    int numeroCanali = 0;
};

#endif // __ANDROID__

#endif // OBOEAUDIODRIVER_HPP