// =============================================================================
// main.cpp — BANCO DI PROVA MUSICALE OFFLINE (sandbox di test)
//
// Legge sampleRate, numeroCanali e bufferSize direttamente da AudioDeviceManager
// e genera "canzone_test_20s.wav" in formato nativo 32-bit IEEE Float.
//
// Esercita su 20 secondi (5 ottave, Do2..Do7):
//   1. Pianissimo (0.25-0.34) + Pedale pieno (1.0) + Risonanza simpatica
//   2. Mezzo-pedale (0.48) + Contrasto Legato (rel=0.15) vs Staccato (rel=0.95)
//   3. Ribattuti rapidi in crescendo (0.35 -> 0.96) + Re-pedaling (Catch pedal)
//   4. Arpeggio su 5 ottave fino al registro senza smorzatori (> 1400 Hz)
// =============================================================================

#include <iostream>
#include <fstream>
#include <vector>
#include <memory>
#include <string>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <iomanip>
#include <stdexcept>

#include "motore_audio/HammerOrchestrator.hpp"
#include "DeviceMenager/AudioDeviceManager.hpp"
#include "Dischetto.hpp"

enum class AzioneBrano {
    NOTE_ON,
    NOTE_OFF,
    PEDALE
};

struct EventoBrano {
    double      tempoSec;
    AzioneBrano azione;
    size_t      nota;
    float       valore;
    const char* etichetta;
};

static void writeU16(std::ofstream& out, uint16_t v) {
    char b[2] = {
        static_cast<char>(v & 0xFF),
        static_cast<char>((v >> 8) & 0xFF)
    };
    out.write(b, 2);
}

static void writeU32(std::ofstream& out, uint32_t v) {
    char b[4] = {
        static_cast<char>(v & 0xFF),
        static_cast<char>((v >> 8) & 0xFF),
        static_cast<char>((v >> 16) & 0xFF),
        static_cast<char>((v >> 24) & 0xFF)
    };
    out.write(b, 4);
}

// Salva il buffer float nativo del motore in WAV 32-bit IEEE Float (AudioFormat = 3)
static bool salvaWavFloat32(const std::string& nomeFile,
                            const std::vector<float>& samples,
                            int sampleRate,
                            int numeroCanali,
                            size_t& outCampioniOltreUno,
                            float& outPiccoMax) {
    std::ofstream out(nomeFile, std::ios::binary);
    if (!out.is_open()) return false;

    const uint16_t audioFormat   = 3; // WAVE_FORMAT_IEEE_FLOAT (32-bit float nativo)
    const uint16_t numChannels   = static_cast<uint16_t>(numeroCanali);
    const uint16_t bitsPerSample = 32;
    const uint16_t blockAlign    = numChannels * (bitsPerSample / 8);
    const uint32_t byteRate      = static_cast<uint32_t>(sampleRate) * blockAlign;
    const uint32_t dataSize      = static_cast<uint32_t>(samples.size() * sizeof(float));
    const uint32_t riffSize      = 36 + dataSize;

    out.write("RIFF", 4);
    writeU32(out, riffSize);
    out.write("WAVE", 4);

    out.write("fmt ", 4);
    writeU32(out, 16);
    writeU16(out, audioFormat);
    writeU16(out, numChannels);
    writeU32(out, static_cast<uint32_t>(sampleRate));
    writeU32(out, byteRate);
    writeU16(out, blockAlign);
    writeU16(out, bitsPerSample);

    out.write("data", 4);
    writeU32(out, dataSize);

    outCampioniOltreUno = 0;
    outPiccoMax = 0.0f;

    for (float x : samples) {
        if (!std::isfinite(x)) continue;
        float absX = std::abs(x);
        if (absX > outPiccoMax) outPiccoMax = absX;
        if (absX > 1.0f) outCampioniOltreUno++;
    }

    out.write(reinterpret_cast<const char*>(samples.data()), static_cast<std::streamsize>(dataSize));
    return out.good();
}

int main() {
    std::cout << "============================================================\n";
    std::cout << "   PIANO ENGINE — GENERAZIONE BRANO DI TEST (20 secondi)\n";
    std::cout << "============================================================\n\n";

    // 1. Lettura parametri reali dall'hardware tramite AudioDeviceManager
    AudioDeviceManager audioManager;
    try {
        if (!audioManager.inizializzaDispositivo()) {
            std::cerr << "ERRORE: impossibile inizializzare il dispositivo audio!\n";
            return -1;
        }
    } catch (const std::exception& e) {
        std::cerr << "ERRORE driver audio: " << e.what() << "\n";
        return -1;
    }

    const int    SAMPLE_RATE  = audioManager.getSampleRate();
    const int    NUM_CANALI   = audioManager.getNumeroCanali();
    const size_t BLOCK_SIZE   = static_cast<size_t>(std::max(1, audioManager.getBufferSize()));

    constexpr int    NUMERO_NOTE   = 61;   // 5 ottave: Do2 (0) .. Do7 (60)
    constexpr float  DURATA_BUFFER = 4.5f; // Secondi per nota nel Dischetto
    constexpr double DURATA_BRANO  = 20.0; // Durata totale WAV in secondi

    std::cout << "[1/3] Hardware rilevato dal driver: " << SAMPLE_RATE << " Hz, "
              << NUM_CANALI << " canali, blocco = " << BLOCK_SIZE << " frame\n";

    // 2. Generazione Dischetto e HammerOrchestrator sui parametri del driver
    std::cout << "[2/3] Generazione Dischetto (" << NUMERO_NOTE << " note @ " << SAMPLE_RATE << " Hz)...\n";
    auto dischetto = std::make_shared<Dischetto>();
    dischetto->simulaGenerazioneDisco(NUMERO_NOTE, SAMPLE_RATE, DURATA_BUFFER);
    const size_t noteDisponibili = dischetto->getNumeroNote();

    auto orchestrator = std::make_shared<HammerOrchestrator>(dischetto, SAMPLE_RATE, NUM_CANALI);
    orchestrator->setGuadagnoMaster(0.85f);

    // Indici cromatici (0 = Do2 / MIDI 36)
    constexpr size_t Do2 = 0,  Sol2 = 7,  Lab2 = 8,  Sib2 = 10;
    constexpr size_t Do3 = 12, Re3 = 14,  Mib3 = 15, Mi3 = 16, Fa3 = 17, Sol3 = 19, Sib3 = 22;
    constexpr size_t Do4 = 24, Re4 = 26,  Mib4 = 27, Mi4 = 28, Fa4 = 29, Sol4 = 31, Lab4 = 32;
    constexpr size_t Do5 = 36, Mi5 = 40,  Sol5 = 43;
    constexpr size_t Do6 = 48, Sol6 = 55, Do7 = 60; // Sol6 e Do7 superano 1400 Hz (senza smorzatori)

    std::vector<EventoBrano> partitura;

    auto addNota = [&](double tOn, double durata, size_t nota, float vel, float relVel = 0.45f, const char* tag = nullptr) {
        partitura.push_back({ tOn, AzioneBrano::NOTE_ON, nota, vel, tag });
        partitura.push_back({ tOn + durata, AzioneBrano::NOTE_OFF, nota, relVel, nullptr });
    };
    auto addPedale = [&](double t, float prof, const char* tag = nullptr) {
        partitura.push_back({ t, AzioneBrano::PEDALE, 0, prof, tag });
    };

    // =========================================================================
    // ATTO I (0.0s - 5.0s): Pianissimo + Pedale pieno (100%) + Risonanza simpatica
    // =========================================================================
    addPedale(0.05, 1.00f, "[0.05s] ATTO I  : Pianissimo + Pedale pieno (100%)");
    addNota(0.10, 2.30, Do2,  0.32f, 0.25f);
    addNota(0.42, 1.95, Sol2, 0.26f, 0.25f);
    addNota(0.74, 1.65, Mib3, 0.28f, 0.25f);
    addNota(1.06, 1.35, Sol3, 0.30f, 0.25f);
    addNota(1.38, 1.05, Do4,  0.34f, 0.20f);
    addNota(1.75, 0.70, Re4,  0.31f, 0.20f);
    addNota(2.10, 0.40, Mib4, 0.33f, 0.20f);

    // Cambio armonico su Lab maggiore con stacco e riattacco pedale
    addPedale(2.55, 0.00f);
    addPedale(2.62, 1.00f);
    addNota(2.65, 2.15, Lab2, 0.32f, 0.25f);
    addNota(2.97, 1.80, Mib3, 0.26f, 0.25f);
    addNota(3.29, 1.50, Lab4, 0.29f, 0.25f);
    addNota(3.61, 1.20, Do4,  0.34f, 0.20f);
    addNota(3.98, 0.80, Mib4, 0.35f, 0.20f);
    addNota(4.38, 0.45, Re4,  0.29f, 0.20f);

    // =========================================================================
    // ATTO II (5.0s - 10.0s): Mezzo-pedale (48%) + Legato vs Staccato
    // =========================================================================
    addPedale(4.95, 0.48f, "[4.95s] ATTO II : Mezzo-pedale (48%) + Legato vs Staccato");
    // Frase legata (releaseVelocity basso = 0.15)
    addNota(5.05, 2.20, Do3,  0.52f, 0.15f);
    addNota(5.20, 0.65, Sol3, 0.56f, 0.15f);
    addNota(5.78, 0.65, Sib3, 0.60f, 0.15f);
    addNota(6.36, 0.65, Mib4, 0.64f, 0.15f);
    addNota(6.94, 0.55, Fa4,  0.62f, 0.15f);

    // Frase staccata (durata breve + releaseVelocity alto = 0.95)
    addNota(7.55, 0.12, Sol4, 0.68f, 0.95f);
    addNota(7.85, 0.12, Fa4,  0.64f, 0.95f);
    addNota(8.15, 0.12, Mib4, 0.66f, 0.95f);
    addNota(8.45, 0.12, Re4,  0.62f, 0.95f);
    addNota(8.75, 0.12, Do4,  0.65f, 0.95f);
    addNota(9.05, 0.12, Sib3, 0.60f, 0.95f);
    addNota(9.35, 0.35, Sol3, 0.58f, 0.85f);

    // =========================================================================
    // ATTO III (10.0s - 14.5s): Ribattuti in Crescendo + Re-Pedaling (Catch)
    // =========================================================================
    addPedale(9.95, 0.00f, "[9.95s] ATTO III: Ribattuti in crescendo + Re-Pedaling");
    const float velRibattuti[6] = { 0.36f, 0.48f, 0.60f, 0.74f, 0.86f, 0.96f };
    for (int r = 0; r < 6; ++r) {
        double t = 10.05 + r * 0.32;
        addNota(t, 0.26, Sol4, velRibattuti[r], 0.50f);
    }

    // Accordo Fortissimo a pedale alzato -> rilascio tasti -> catch pedal dopo 110 ms
    addNota(12.10, 0.42, Do2,  0.96f, 0.70f);
    addNota(12.10, 0.42, Sol2, 0.94f, 0.70f);
    addNota(12.10, 0.42, Do3,  0.95f, 0.70f);
    addNota(12.10, 0.42, Mi3,  0.93f, 0.70f);
    addNota(12.10, 0.42, Sol3, 0.95f, 0.70f);
    addNota(12.10, 0.42, Do4,  0.96f, 0.70f);
    addPedale(12.63, 1.00f, "[12.63s]         Re-pedaling (cattura coda accordo)");

    // =========================================================================
    // ATTO IV (14.5s - 20.0s): Arpeggio su 5 ottave fino agli acuti senza feltri
    // =========================================================================
    addPedale(14.40, 0.00f);
    addPedale(14.48, 1.00f, "[14.48s] ATTO IV : Arpeggio 5 ottave (> 1400 Hz senza feltri)");

    const size_t scalaFinale[] = {
        Do2, Sol2, Do3, Mi3, Sol3, Do4, Mi4, Sol4, Do5, Mi5, Sol5, Do6, Sol6, Do7
    };
    const float velFinale[] = {
        0.78f, 0.82f, 0.86f, 0.90f, 0.94f, 0.92f, 0.86f,
        0.78f, 0.68f, 0.58f, 0.48f, 0.38f, 0.32f, 0.26f
    };
    const size_t nNoteFinali = sizeof(scalaFinale) / sizeof(scalaFinale[0]);

    for (size_t i = 0; i < nNoteFinali; ++i) {
        double tOn = 14.55 + static_cast<double>(i) * 0.19;
        double dur = 17.60 - tOn;
        addNota(tOn, std::max(0.25, dur), scalaFinale[i], velFinale[i], 0.25f);
    }

    // Rilascio pedale a 18.2s: Sol6 e Do7 (> 1400 Hz) continuano a risuonare libere
    addPedale(18.20, 0.00f, "[18.20s]         Pedale rilasciato (acuti > 1400 Hz liberi)");

    // Ordina cronologicamente la partitura
    std::sort(partitura.begin(), partitura.end(),
              [](const EventoBrano& a, const EventoBrano& b) { return a.tempoSec < b.tempoSec; });

    // 3. Rendering offline block-by-block con il BLOCK_SIZE del driver
    std::cout << "[3/3] Rendering offline in corso...\n\n";

    const size_t totalFrames = static_cast<size_t>(DURATA_BRANO * static_cast<double>(SAMPLE_RATE));
    std::vector<float> audioTotale;
    audioTotale.reserve(totalFrames * static_cast<size_t>(NUM_CANALI));
    std::vector<float> blocco(BLOCK_SIZE * static_cast<size_t>(NUM_CANALI), 0.0f);

    size_t idxEvento = 0;
    size_t frameCorrente = 0;

    while (frameCorrente < totalFrames) {
        double tempoCorrente = static_cast<double>(frameCorrente) / static_cast<double>(SAMPLE_RATE);

        while (idxEvento < partitura.size() && partitura[idxEvento].tempoSec <= tempoCorrente) {
            const auto& ev = partitura[idxEvento];
            if (ev.etichetta) {
                std::cout << "  " << ev.etichetta << "\n";
            }

            if (ev.azione == AzioneBrano::PEDALE) {
                orchestrator->sustainPedal(ev.valore);
            } else if (ev.nota < noteDisponibili) {
                if (ev.azione == AzioneBrano::NOTE_ON) {
                    orchestrator->noteOn(ev.nota, ev.valore);
                } else {
                    orchestrator->noteOff(ev.nota, ev.valore);
                }
            }
            idxEvento++;
        }

        size_t framesDaProcessare = std::min(BLOCK_SIZE, totalFrames - frameCorrente);
        orchestrator->processaBlocco(blocco.data(), framesDaProcessare);

        audioTotale.insert(audioTotale.end(),
                           blocco.begin(),
                           blocco.begin() + framesDaProcessare * static_cast<size_t>(NUM_CANALI));

        frameCorrente += framesDaProcessare;
    }

    size_t oltreUno = 0;
    float piccoMax = 0.0f;
    const std::string nomeWav = "canzone_test_20s.wav";

    if (!salvaWavFloat32(nomeWav, audioTotale, SAMPLE_RATE, NUM_CANALI, oltreUno, piccoMax)) {
        std::cerr << "\nERRORE: impossibile scrivere " << nomeWav << "\n";
        return -1;
    }

    std::cout << "\nFile generato: " << nomeWav
              << " (" << SAMPLE_RATE << " Hz, " << NUM_CANALI << " ch, 32-bit float)\n";
    std::cout << "  -> Picco massimo rilevato : " << std::fixed << std::setprecision(4) << piccoMax << "\n";
    std::cout << "  -> Campioni oltre 1.0     : " << oltreUno << "\n";
    return 0;
}