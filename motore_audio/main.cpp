#include <iostream>
#include <vector>
#include <fstream>
#include <cstdint>
#include <algorithm>

#include "Dischetto.hpp"
#include "HammerOrchestrator.hpp"

// Helper per salvare il file WAV
void salvaWAVStereo(const std::string& filename, const std::vector<float>& bufferStereo, int sampleRate) {
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) return;

    int32_t dataSize = static_cast<int32_t>(bufferStereo.size() * sizeof(int16_t));
    int32_t chunkSize = 36 + dataSize;
    int16_t numChannels = 2; 
    int32_t byteRate = sampleRate * numChannels * sizeof(int16_t);
    int16_t blockAlign = numChannels * sizeof(int16_t);
    int16_t bitsPerSample = 16;

    file.write("RIFF", 4);
    file.write(reinterpret_cast<const char*>(&chunkSize), 4);
    file.write("WAVE", 4);
    file.write("fmt ", 4);

    int32_t subchunk1Size = 16;
    int16_t audioFormat = 1;
    file.write(reinterpret_cast<const char*>(&subchunk1Size), 4);
    file.write(reinterpret_cast<const char*>(&audioFormat), 2);
    file.write(reinterpret_cast<const char*>(&numChannels), 2);
    file.write(reinterpret_cast<const char*>(&sampleRate), 4);
    file.write(reinterpret_cast<const char*>(&byteRate), 4);
    file.write(reinterpret_cast<const char*>(&blockAlign), 2);
    file.write(reinterpret_cast<const char*>(&bitsPerSample), 2);

    file.write("data", 4);
    file.write(reinterpret_cast<const char*>(&dataSize), 4);

    for (float floatSample : bufferStereo) {
        float clamped = std::max(-1.0f, std::min(1.0f, floatSample));
        int16_t pcmSample = static_cast<int16_t>(clamped * 32767.0f);
        file.write(reinterpret_cast<const char*>(&pcmSample), sizeof(int16_t));
    }
}

// Struttura per definire una nota dello spartito
struct NotaSpartito {
    size_t indiceNota;    // 0=Do, 1=Re, 2=Mi, 3=Fa, 4=Sol, 5=La
    float durataSecondi;  // Durata in secondi
    float velocity;       // Forza del tocco (0.0 -> 1.0)
};

int main() {
    std::cout << "=== TEST RIUSCITO: GENERAZIONE CANZONCINA ===" << std::endl;

    const int sampleRate = 44100;
    const size_t framesPerBlock = 256;

    // 1. Inizializzazione
    Dischetto dischetto;
    dischetto.simulaGenerazioneDisco(7, sampleRate, 3.0f);
    HammerOrchestrator orchestrator(&dischetto, sampleRate);

    // 2. Spartito di "Fra Martino Campanaro"
    // Indici: Do=0, Re=1, Mi=2, Fa=3, Sol=4, La=5
    std::vector<NotaSpartito> melodia = {
    // --- Fra Mar-ti-no ---
    {0, 0.4f, 0.45f}, {1, 0.4f, 0.45f}, {2, 0.4f, 0.45f}, {0, 0.4f, 0.45f},
    {0, 0.4f, 0.45f}, {1, 0.4f, 0.45f}, {2, 0.4f, 0.45f}, {0, 0.4f, 0.45f},

    // --- Cam-pa-na-ro ---
    {2, 0.4f, 0.50f}, {3, 0.4f, 0.50f}, {4, 0.8f, 0.55f},
    {2, 0.4f, 0.50f}, {3, 0.4f, 0.50f}, {4, 0.8f, 0.55f},

    // --- Dor-mi tu? ---
    {4, 0.25f, 0.50f}, {5, 0.25f, 0.50f}, {4, 0.25f, 0.45f}, {3, 0.25f, 0.45f}, {2, 0.4f, 0.45f}, {0, 0.4f, 0.45f},
    {4, 0.25f, 0.50f}, {5, 0.25f, 0.50f}, {4, 0.25f, 0.45f}, {3, 0.25f, 0.45f}, {2, 0.4f, 0.45f}, {0, 0.4f, 0.45f},

    // --- Din don dan (ACCORDI ARMONICI) ---
    // "Din": Accordo Do + Mi
    {0, 0.0f, 0.45f}, {2, 0.5f, 0.45f}, 

    // "Don": Accordo Re + Sol
    {1, 0.0f, 0.45f}, {4, 0.5f, 0.45f}, 

    // "Dan": Gran Finale con Accordo Pieno Do + Mi + Sol
    {0, 0.0f, 0.55f}, {2, 0.0f, 0.55f}, {4, 1.0f, 0.55f},

    // Ripetizione finale
    {0, 0.0f, 0.45f}, {2, 0.5f, 0.45f}, 
    {1, 0.0f, 0.45f}, {4, 0.5f, 0.45f}, 
    {0, 0.0f, 0.60f}, {2, 0.0f, 0.60f}, {4, 1.2f, 0.60f}
};

    std::vector<float> tracciaAudioCompleta;
    std::vector<float> bufferBlocco(framesPerBlock * 2); // Stereo

    // 3. Riproduzione nota per nota nello spartito
    for (const auto& nota : melodia) {
        // Invia l'evento NOTE_ON alla coda atomica
        orchestrator.noteOn(nota.indiceNota, nota.velocity);

        size_t blocchiNota = static_cast<size_t>((nota.durataSecondi * sampleRate) / framesPerBlock);

        for (size_t b = 0; b < blocchiNota; ++b) {
            // A metà durata rilasciamo il tasto (NOTE_OFF) per fare lo staccato ed evitare che le note si sovrappongano troppo
            if (b == blocchiNota / 2) {
                orchestrator.noteOff(nota.indiceNota);
            }

            orchestrator.processaBlocco(bufferBlocco.data(), framesPerBlock);
            tracciaAudioCompleta.insert(tracciaAudioCompleta.end(), bufferBlocco.begin(), bufferBlocco.end());
        }
    }

    // Un secondo di silenzio finale per far decadere l'ultima nota
    orchestrator.rilasciaTutto();
    size_t blocchiCoda = sampleRate / framesPerBlock;
    for (size_t b = 0; b < blocchiCoda; ++b) {
        orchestrator.processaBlocco(bufferBlocco.data(), framesPerBlock);
        tracciaAudioCompleta.insert(tracciaAudioCompleta.end(), bufferBlocco.begin(), bufferBlocco.end());
    }

    // 4. Salva il file WAV
    salvaWAVStereo("canzoncina_fra_martino.wav", tracciaAudioCompleta, sampleRate);
    std::cout << "File generato con successo: 'canzoncina_fra_martino.wav'" << std::endl;

    return 0;
}