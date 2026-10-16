#include "OboeAudioDriver.hpp"
#include "AudioDeviceManager.hpp"
#include <cstring>

OboeAudioDriver::OboeAudioDriver(AudioDeviceManager* manager) : manager(manager) {}

OboeAudioDriver::~OboeAudioDriver() {
    ferma();
}

bool OboeAudioDriver::inizializza() {
    ferma();

    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Output)
           ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
           ->setFormat(oboe::AudioFormat::Float)
           ->setChannelCount(2)
           ->setDataCallback(this)
           ->setErrorCallback(this);

    // Tentativo 1: Modalità esclusiva per la latenza minima in assoluto
    builder.setSharingMode(oboe::SharingMode::Exclusive);
    oboe::Result result = builder.openStream(stream);

    // Tentativo 2: Fallback in modalità condivisa se Exclusive non è supportata dall'hardware
    if (result != oboe::Result::OK) {
        builder.setSharingMode(oboe::SharingMode::Shared);
        result = builder.openStream(stream);
    }

    if (result != oboe::Result::OK || !stream) {
        return false;
    }

    // Configura la dimensione del buffer pari al burst hardware per evitare sgranature (underrun)
    int32_t burstFrames = stream->getFramesPerBurst();
    stream->setBufferSizeInFrames(burstFrames);

    // Salva i parametri reali negoziali con l'hardware
    sampleRate = stream->getSampleRate();
    numeroCanali = stream->getChannelCount();
    bufferSize = stream->getBufferSizeInFrames();

    return true;
}

bool OboeAudioDriver::avvia() {
    if (!stream || attivo.load()) return false;

    oboe::Result result = stream->requestStart();
    if (result == oboe::Result::OK) {
        attivo.store(true);
        return true;
    }
    return false;
}

bool OboeAudioDriver::ferma() {
    attivo.store(false);
    if (stream) {
        stream->requestStop();
        stream->close();
        stream.reset();
    }
    return true;
}

oboe::DataCallbackResult OboeAudioDriver::onAudioReady(
    oboe::AudioStream* /*oboeStream*/,
    void* audioData,
    int32_t numFrames
) {
    float* floatBuffer = static_cast<float*>(audioData);

    if (manager) {
        manager->onAudioDeviceReady(floatBuffer, static_cast<size_t>(numFrames));
    } else {
        std::memset(floatBuffer, 0, numFrames * numeroCanali * sizeof(float));
    }

    return oboe::DataCallbackResult::Continue;
}

void OboeAudioDriver::onErrorAfterClose(oboe::AudioStream* /*oboeStream*/, oboe::Result error) {
    if (error == oboe::Result::ErrorDisconnected) {
        attivo.store(false);
        // Notifica o ri-inizializzazione opzionale se le cuffie o la scheda audio vengono staccate
    }
}