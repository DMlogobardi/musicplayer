#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include "Martelletto.hpp"

// Funzione C++ standard per scrivere un file .WAV ascoltabile su qualsiasi dispositivo
void salvaFileWav(const std::string& nomeFile, const std::vector<float>& campioni, int sampleRate = 44100) {
    std::ofstream file(nomeFile, std::ios::binary);

    int32_t dataSize = static_cast<int32_t>(campioni.size() * sizeof(int16_t));
    int32_t chunkSize = 36 + dataSize;
    int16_t numChannels = 1; // Mono
    int32_t sr = sampleRate;
    int32_t byteRate = sr * numChannels * sizeof(int16_t);
    int16_t blockAlign = numChannels * sizeof(int16_t);
    int16_t bitsPerSample = 16;

    // Header WAV standard RIFF
    file.write("RIFF", 4);
    file.write(reinterpret_cast<const char*>(&chunkSize), 4);
    file.write("WAVE", 4);
    file.write("fmt ", 4);
    int32_t subchunk1Size = 16;
    file.write(reinterpret_cast<const char*>(&subchunk1Size), 4);
    int16_t audioFormat = 1; // PCM
    file.write(reinterpret_cast<const char*>(&audioFormat), 2);
    file.write(reinterpret_cast<const char*>(&numChannels), 2);
    file.write(reinterpret_cast<const char*>(&sr), 4);
    file.write(reinterpret_cast<const char*>(&byteRate), 4);
    file.write(reinterpret_cast<const char*>(&blockAlign), 2);
    file.write(reinterpret_cast<const char*>(&bitsPerSample), 2);
    file.write("data", 4);
    file.write(reinterpret_cast<const char*>(&dataSize), 4);

    // Conversione dei campioni da float (-1.0 -> 1.0) a interi PCM a 16 bit
    for (float s : campioni) {
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        int16_t pcmSample = static_cast<int16_t>(s * 32767.0f);
        file.write(reinterpret_cast<const char*>(&pcmSample), sizeof(int16_t));
    }

    file.close();
}

int main() {
    Martelletto m;
    m.setId(1);

    int sampleRate = 44100;
    int durataSecondi = 3;
    int totaleCampioni = sampleRate * durataSecondi;

    std::vector<float> bufferAudio;
    bufferAudio.reserve(totaleCampioni);

    std::cout << "1. Colpisco la nota La4 (440 Hz)..." << std::endl;
    m.colpisci(440.0f, 1.0f); // Tasto premuto

    for (int i = 0; i < totaleCampioni; ++i) {
        // Dopo 1.5 secondi simuliamo il stacco del dito dal touch!
        if (i == static_cast<int>(sampleRate * 1.5)) {
            std::cout << "2. Tasto rilasciato: entra in funzione lo smorzatore!" << std::endl;
            m.rilascia();
        }

        // Il driver simula la chiamata a suona()
        float campione = m.suona(sampleRate);
        bufferAudio.push_back(campione);
    }

    salvaFileWav("nota_martelletto.wav", bufferAudio, sampleRate);
    std::cout << "3. File 'nota_martelletto.wav' generato con successo! Apri il file per ascoltarlo." << std::endl;

    return 0;
}