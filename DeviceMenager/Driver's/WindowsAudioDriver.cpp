#include "WindowsAudioDriver.hpp"
#include "AudioDeviceManager.hpp"
#include <cstring>

WindowsAudioDriver::WindowsAudioDriver(AudioDeviceManager* manager) : manager(manager) {}

WindowsAudioDriver::~WindowsAudioDriver() {
    ferma();
    if (audioEvent) { CloseHandle(audioEvent); audioEvent = NULL; }
    if (cookieMta)  { CoDecrementMTAUsage(cookieMta); cookieMta = nullptr; }
}

bool WindowsAudioDriver::inizializza() {
    if (FAILED(CoIncrementMTAUsage(&cookieMta))) return false;

    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) return false;

    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(hr)) return false;

    hr = device->Activate(
        __uuidof(IAudioClient),
        CLSCTX_ALL,
        nullptr,
        reinterpret_cast<void**>(audioClient.GetAddressOf())
    );
    if (FAILED(hr)) return false;

    // 1. Chiediamo al driver il formato nativo attuale dell'hardware
    WAVEFORMATEX* pwfx = nullptr;
    hr = audioClient->GetMixFormat(&pwfx);
    if (FAILED(hr)) return false;

    sampleRate = pwfx->nSamplesPerSec;   // Frequenza reale (es. 48000, 96000, 192000 Hz)
    numeroCanali = (pwfx->nChannels == 1) ? 1 : 2;

    CoTaskMemFree(pwfx);

    WAVEFORMATEXTENSIBLE wfe;
    std::memset(&wfe, 0, sizeof(wfe));
    wfe.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wfe.Format.nChannels = static_cast<WORD>(numeroCanali);
    wfe.Format.nSamplesPerSec = sampleRate;
    wfe.Format.wBitsPerSample = 32;
    wfe.Format.nBlockAlign = (wfe.Format.nChannels * wfe.Format.wBitsPerSample) / 8;
    wfe.Format.nAvgBytesPerSec = wfe.Format.nSamplesPerSec * wfe.Format.nBlockAlign;
    wfe.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    wfe.Samples.wValidBitsPerSample = 32;
    wfe.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

    // 2. CHIEDIAMO AL DRIVER IL LIMITE FISICO MINIMO DI LATENZA
    REFERENCE_TIME defaultPeriod = 0;
    REFERENCE_TIME minimumPeriod = 0;
    
    // GetDevicePeriod ci restituisce il minimo assoluto che la scheda puo' fare
    hr = audioClient->GetDevicePeriod(&defaultPeriod, &minimumPeriod);
    if (FAILED(hr)) return false;

    DWORD streamFlags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | 
                        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | 
                        AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;

    // 3. Inizializziamo WASAPI usando minimumPeriod (Massima velocita' hardware)
    hr = audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        streamFlags,
        minimumPeriod, // <-- Chiediamo il buffer piu' piccolo in assoluto!
        0,
        &wfe.Format,
        NULL
    );

    if (FAILED(hr)) return false;

    // 4. Leggiamo quanti frame effettivi ha allocato il driver
    UINT32 bufferFrameCount = 0;
    hr = audioClient->GetBufferSize(&bufferFrameCount);
    if (FAILED(hr)) return false;
    
    bufferSize = static_cast<int>(bufferFrameCount);

    audioEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!audioEvent) return false;

    hr = audioClient->SetEventHandle(audioEvent);
    if (FAILED(hr)) return false;

    hr = audioClient->GetService(IID_PPV_ARGS(&renderClient));
    return SUCCEEDED(hr);
}

void WindowsAudioDriver::audioThreadFunction() {
    while (threadRunning.load(std::memory_order_relaxed)) {
        DWORD waitResult = WaitForSingleObject(audioEvent, 2000);
        if (waitResult != WAIT_OBJECT_0 || !threadRunning.load(std::memory_order_relaxed)) break;

        UINT32 padding = 0;
        if (FAILED(audioClient->GetCurrentPadding(&padding))) continue;

        UINT32 framesToFill = bufferSize - padding;
        if (framesToFill == 0) continue;

        BYTE* pData = nullptr;
        if (FAILED(renderClient->GetBuffer(framesToFill, &pData))) continue;

        float* floatBuffer = reinterpret_cast<float*>(pData);
        if (manager) {
            manager->onAudioDeviceReady(floatBuffer, framesToFill);
        } else {
            std::memset(floatBuffer, 0, framesToFill * numeroCanali * sizeof(float));
        }

        renderClient->ReleaseBuffer(framesToFill, 0);
    }
    attivo.store(false);
}

bool WindowsAudioDriver::avvia() {
    if (attivo.load()) return true;

    HRESULT hr = audioClient->Start();
    if (FAILED(hr)) return false;

    attivo.store(true);
    threadRunning.store(true);
    if (renderThread.joinable()) renderThread.join();
    renderThread = std::thread([this]() { audioThreadFunction(); });
    return true;
}

bool WindowsAudioDriver::ferma() {
    threadRunning.store(false);
    attivo.store(false);

    if (audioEvent) SetEvent(audioEvent);          // sveglia il thread
    if (renderThread.joinable()) renderThread.join();
    if (audioEvent) ResetEvent(audioEvent);        // niente risveglio spurio al riavvio

    if (audioClient) {
        audioClient->Stop();
        audioClient->Reset();                      // svuota il buffer: si può richiamare avvia()
    }
    return true;
}