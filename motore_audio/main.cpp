#include <iostream>
#include <vector>
#include <fstream>
#include <cstdint>
#include <algorithm>

#include "Dischetto.hpp"
#include "HammerOrchestrator.hpp"

// Helper per salvare il file WAV stereo a 16-bit
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

// Struttura dell'evento spartito
struct NotaSpartito {
    size_t indiceNota;    // 0=Do, 1=Re, 2=Mi, 3=Fa, 4=Sol, 5=La, 6=Si
    float durataSecondi;  // Se 0.0f, viene suonata insieme alla nota successiva (Accordo)
    float velocity;       // Dinamica (0.1 -> 1.0)
};

int main() {
    std::cout << "=== GENERAZIONE AUDIO: INNO ALLA GIOIA (BEETHOVEN) ===" << std::endl;

    const int sampleRate = 44100;
    const size_t framesPerBlock = 256;

    // 1. Inizializzazione Dischetto ed Orchestrator
    Dischetto dischetto;
    dischetto.simulaGenerazioneDisco(7, sampleRate, 3.0f);
    HammerOrchestrator orchestrator(&dischetto, sampleRate, 2); // Output Stereo

    // Mappatura ottava standard: 0=Do, 1=Re, 2=Mi, 3=Fa, 4=Sol, 5=La, 6=Si
    std::vector<NotaSpartito> spartito = {
        // --- BATTUTA 1: Mi Mi Fa Sol ---
        {0, 0.0f, 0.35f}, {2, 0.40f, 0.60f},  // Bass Do + Melodia Mi
        {2, 0.40f, 0.55f},
        {3, 0.40f, 0.65f},
        {4, 0.40f, 0.75f},

        // --- BATTUTA 2: Sol Fa Mi Re ---
        {4, 0.0f, 0.40f}, {4, 0.40f, 0.70f},  // Bass Sol + Melodia Sol
        {3, 0.40f, 0.60f},
        {2, 0.40f, 0.55f},
        {1, 0.40f, 0.50f},

        // --- BATTUTA 3: Do Do Re Mi ---
        {0, 0.0f, 0.45f}, {0, 0.40f, 0.50f},  // Bass Do + Melodia Do
        {0, 0.40f, 0.50f},
        {1, 0.40f, 0.60f},
        {2, 0.40f, 0.65f},

        // --- BATTUTA 4: Mi. Re Re ---
        {2, 0.60f, 0.60f},
        {1, 0.20f, 0.45f},
        {4, 0.0f, 0.40f}, {1, 0.80f, 0.55f},  // Accordo finale di frase (Sol + Re)

        // --- BATTUTA 5 (Ripresa): Mi Mi Fa Sol ---
        {0, 0.0f, 0.40f}, {2, 0.40f, 0.65f},
        {2, 0.40f, 0.60f},
        {3, 0.40f, 0.70f},
        {4, 0.40f, 0.80f},

        // --- BATTUTA 6: Sol Fa Mi Re ---
        {4, 0.0f, 0.45f}, {4, 0.40f, 0.75f},
        {3, 0.40f, 0.65f},
        {2, 0.40f, 0.60f},
        {1, 0.40f, 0.55f},

        // --- BATTUTA 7: Do Do Re Mi ---
        {0, 0.0f, 0.45f}, {0, 0.40f, 0.55f},
        {0, 0.40f, 0.50f},
        {1, 0.40f, 0.60f},
        {2, 0.40f, 0.65f},

        // --- BATTUTA 8: Re. Do Do (Gran Finale) ---
        {1, 0.60f, 0.55f},
        {0, 0.20f, 0.45f},
        // Accordo finale Pieno (Do + Mi + Sol)
        {0, 0.0f, 0.75f}, {2, 0.0f, 0.75f}, {4, 1.50f, 0.80f}
    };

    std::vector<float> tracciaAudioCompleta;
    std::vector<float> bufferBlocco(framesPerBlock * 2); // 2 Canali Stereo

    // 2. Sintesi nota per nota
    for (const auto& nota : spartito) {
        // Invia l'evento NOTE_ON alla coda lock-free
        orchestrator.noteOn(nota.indiceNota, nota.velocity);

        // Se durataSecondi == 0.0f, significa che fa parte di un accordo contemporaneo
        if (nota.durataSecondi > 0.0f) {
            size_t blocchiNota = static_cast<size_t>((nota.durataSecondi * sampleRate) / framesPerBlock);

            for (size_t b = 0; b < blocchiNota; ++b) {
                // Rilascio del tasto (NOTE_OFF) a metà durata per lo staccato
                if (b == blocchiNota / 2) {
                    orchestrator.noteOff(nota.indiceNota);
                }

                orchestrator.processaBlocco(bufferBlocco.data(), framesPerBlock);
                tracciaAudioCompleta.insert(tracciaAudioCompleta.end(), bufferBlocco.begin(), bufferBlocco.end());
            }
        }
    }

    // 3. Coda di 1.5 secondi per far sfumare la risonanza finale dell'ultimo accordo
    orchestrator.rilasciaTutto();
    size_t blocchiCoda = static_cast<size_t>((1.5f * sampleRate) / framesPerBlock);
    for (size_t b = 0; b < blocchiCoda; ++b) {
        orchestrator.processaBlocco(bufferBlocco.data(), framesPerBlock);
        tracciaAudioCompleta.insert(tracciaAudioCompleta.end(), bufferBlocco.begin(), bufferBlocco.end());
    }

    // 4. Scrittura su disco
    const std::string outputFile = "inno_alla_gioia_stereo.wav";
    salvaWAVStereo(outputFile, tracciaAudioCompleta, sampleRate);
    std::cout << "File WAV generato con successo: '" << outputFile << "'" << std::endl;

    return 0;
}