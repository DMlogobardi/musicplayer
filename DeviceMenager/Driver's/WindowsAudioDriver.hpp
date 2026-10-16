#pragma once
#ifndef WINDOWSAUDIODRIVER_HPP
#define WINDOWSAUDIODRIVER_HPP

#include "IAudioDriver.hpp"
#include <windows.h>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <wrl/client.h>
#include <thread>
#include <atomic>

class AudioDeviceManager;

class WindowsAudioDriver : public IAudioDriver {
    public:
        explicit WindowsAudioDriver(AudioDeviceManager* manager);
        ~WindowsAudioDriver() override;

        bool inizializza() override;
        bool avvia() override;
        bool ferma() override;
        bool isAttivo() const override { return attivo.load(std::memory_order_relaxed); }

        int getSampleRateReale() const override { return sampleRate; }
        int getBufferSizeReale() const override { return bufferSize; }
        int getNumeroCanaliReale() const override { return numeroCanali; }

    private:
        void audioThreadFunction();

        AudioDeviceManager* manager = nullptr;
        std::atomic<bool> attivo{false};
        std::atomic<bool> threadRunning{false};

        int sampleRate = 44100;
        int bufferSize = 512;
        int numeroCanali = 2;

        Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
        Microsoft::WRL::ComPtr<IMMDevice> device;
        Microsoft::WRL::ComPtr<IAudioClient> audioClient;
        Microsoft::WRL::ComPtr<IAudioRenderClient> renderClient;
        CO_MTA_USAGE_COOKIE cookieMta = nullptr;
        HANDLE audioEvent = NULL;
        std::thread renderThread;
};

#endif // WINDOWSAUDIODRIVER_HPP