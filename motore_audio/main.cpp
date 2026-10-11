#include <iostream>
#include <vector>
#include <fstream>
#include <cstdint>
#include <algorithm>

#include "Dischetto.hpp"
#include "HammerOrchestrator.hpp"

// Salva il file WAV stereo a 16-bit PCM
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

// Convertitore da Nota MIDI standard a Indice Dischetto (C2 = MIDI 36 = Indice 0)
constexpr size_t MIDI_BASE = 36;
inline size_t m(int midiNote) {
    return static_cast<size_t>(midiNote - MIDI_BASE);
}

int main() {
    std::cout << "=== TEST MOTORE AUDIO: J.S. BACH - PRELUDIO IN DO MAGGIORE (BWV 846) ===" << std::endl;

    const int sampleRate = 44100;
    const size_t framesPerBlock = 256;

    // 1. Inizializzazione Dischetto (49 note: C2 -> C6)
    Dischetto dischetto;
    dischetto.simulaGenerazioneDisco(49, sampleRate, 4.0f);
    HammerOrchestrator orchestrator(&dischetto, sampleRate, 2);

    std::vector<float> tracciaAudioCompleta;
    std::vector<float> bufferBlocco(framesPerBlock * 2);

    auto renderizzaTempo = [&](float durataSec) {
        size_t blocchi = static_cast<size_t>((durataSec * sampleRate) / framesPerBlock);
        for (size_t b = 0; b < blocchi; ++b) {
            orchestrator.processaBlocco(bufferBlocco.data(), framesPerBlock);
            tracciaAudioCompleta.insert(tracciaAudioCompleta.end(), bufferBlocco.begin(), bufferBlocco.end());
        }
    };

    auto suonaBattutaArpeggiata = [&](int basso, int tenore, const std::vector<int>& arpeggio, float velBasso, float velMelodia) {
        // Re-pedaling di inizio battuta: rilascio e ri-pressione rapida del pedale
        orchestrator.sustainPedal(false);
        renderizzaTempo(0.015f); // Stacco acustico di 15ms
        orchestrator.sustainPedal(true);

        // Nota di Basso
        orchestrator.noteOn(m(basso), velBasso);
        renderizzaTempo(0.18f);
        orchestrator.noteOff(m(basso));

        // Nota Tenore
        orchestrator.noteOn(m(tenore), velBasso * 0.9f);
        renderizzaTempo(0.18f);
        orchestrator.noteOff(m(tenore));

        // Arpeggio a 16esimi ripetuto 2 volte nella battuta
        for (int rep = 0; rep < 2; ++rep) {
            for (int notaMidi : arpeggio) {
                orchestrator.noteOn(m(notaMidi), velMelodia);
                renderizzaTempo(0.15f);
                orchestrator.noteOff(m(notaMidi));
                renderizzaTempo(0.02f);
            }
        }
    };

    // Note MIDI utili per il Preludio
    constexpr int C2 = 36, B2 = 47;
    constexpr int C3 = 48, D3 = 50, E3 = 52, F3 = 53, G3 = 55, A3 = 57, B3 = 59;
    constexpr int C4 = 60, D4 = 62, E4 = 64, F4 = 65, Fs4 = 66, G4 = 67, A4 = 69, B4 = 71;
    constexpr int C5 = 72, D5 = 74, E5 = 76;

    std::cout << "[1/3] Sintesi delle battute in corso con gestione Pedale Sustain..." << std::endl;

    // --- BATTUTA 1: Do Maggiore ---
    suonaBattutaArpeggiata(C3, E3, {G3, C4, E4, G3, C4, E4}, 0.75f, 0.55f);

    // --- BATTUTA 2: Re minore / Do ---
    suonaBattutaArpeggiata(C3, D3, {A3, D4, F4, A3, D4, F4}, 0.75f, 0.55f);

    // --- BATTUTA 3: Sol 7 / Si ---
    suonaBattutaArpeggiata(B2, D3, {G3, D4, F4, G3, D4, F4}, 0.75f, 0.55f);

    // --- BATTUTA 4: Do Maggiore ---
    suonaBattutaArpeggiata(C3, E3, {G3, C4, E4, G3, C4, E4}, 0.75f, 0.55f);

    // --- BATTUTA 5: La minore / Do ---
    suonaBattutaArpeggiata(C3, E3, {A3, E4, A4, A3, E4, A4}, 0.75f, 0.55f);

    // --- BATTUTA 6: Re 7 / Fsi ---
    suonaBattutaArpeggiata(C3, D3, {Fs4, A4, C5, Fs4, A4, C5}, 0.75f, 0.55f);

    // --- BATTUTA 7: Sol Maggiore ---
    suonaBattutaArpeggiata(B2, D3, {G3, D4, G4, G3, D4, G4}, 0.75f, 0.55f);

    // --- BATTUTA FINALE: Accordo Pieno Sostenuto ---
    std::cout << "[2/3] Generazione dell'accordo finale con risonanza d'ambiente..." << std::endl;

    orchestrator.sustainPedal(false);
    renderizzaTempo(0.02f);
    orchestrator.sustainPedal(true);

    // Attacco simultaneo di tutte le note dell'accordo finale
    orchestrator.noteOn(m(C2), 0.85f);
    orchestrator.noteOn(m(C3), 0.80f);
    orchestrator.noteOn(m(G3), 0.75f);
    orchestrator.noteOn(m(C4), 0.75f);
    orchestrator.noteOn(m(E4), 0.75f);
    orchestrator.noteOn(m(G4), 0.75f);
    orchestrator.noteOn(m(C5), 0.80f);

    // Teniamo le dita sui tasti per 1 secondo
    renderizzaTempo(1.0f);

    // Rilasciamo i tasti fisici
    orchestrator.noteOff(m(C2));
    orchestrator.noteOff(m(C3));
    orchestrator.noteOff(m(G3));
    orchestrator.noteOff(m(C4));
    orchestrator.noteOff(m(E4));
    orchestrator.noteOff(m(G4));
    orchestrator.noteOff(m(C5));

    // Lasciamo sfumare la risonanza con il pedale ancora abbassato per altri 3.5 secondi
    renderizzaTempo(3.5f);

    // Rilascio finale del pedale e coda audio
    orchestrator.sustainPedal(false);
    renderizzaTempo(1.0f);

    // 4. Scrittura su file WAV
    std::cout << "[3/3] Scrittura file WAV stereo..." << std::endl;
    const std::string outputFile = "bach_prelude_c_major.wav";
    salvaWAVStereo(outputFile, tracciaAudioCompleta, sampleRate);

    std::cout << "SUCCESS: File WAV generato con successo: '" << outputFile << "'" << std::endl;
    std::cout << "Campioni totali generati: " << tracciaAudioCompleta.size() / 2 << " frames (" 
              << (tracciaAudioCompleta.size() / 2) / static_cast<float>(sampleRate) << " secondi)." << std::endl;

    return 0;
}