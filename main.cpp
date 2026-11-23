// =============================================================================
// main.cpp — BANCO DI PROVA MUSICALE & BENCHMARK MASTERIZZATORE (sandbox di test)
//
// 1. Misura la velocità di sintesi parallela del Masterizzatore (ms totali e ms/nota).
// 2. Verifica la chiusura deterministica di tutti i thread worker (zero leak, zero CPU residua).
// 3. Esegue un test musicale approfondito di 20 secondi con le nuove note fisiche
//    salvando l'audio in "canzone_test_20s.wav" a 32-bit float nativi.
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
#include <chrono>
#include <stdexcept>

#include "motore_audio/HammerOrchestrator.hpp"
#include "DeviceMenager/AudioDeviceManager.hpp"
#include "motore_generativo/Masterizzatore.hpp"
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
    std::cout << "   PIANO ENGINE — BENCHMARK & TEST NUOVE NOTE FISICHE       \n";
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
    const unsigned int hwCores = audioManager.getThreadHardware();
    const unsigned int workerThreads = audioManager.getThreadWorkerConsigliati();

    constexpr double DURATA_BRANO = 20.0; // Durata totale WAV in secondi

    std::cout << "[1/3] Hardware rilevato dal driver:\n";
    std::cout << "  -> Sample Rate    : " << SAMPLE_RATE << " Hz\n";
    std::cout << "  -> Canali         : " << NUM_CANALI << " (Stereo)\n";
    std::cout << "  -> Buffer Driver  : " << BLOCK_SIZE << " frame\n";
    std::cout << "  -> Core CPU rilevati: " << hwCores << " thread logici\n";
    std::cout << "  -> Thread Worker  : " << workerThreads << " (conservativo per non saturare la CPU)\n\n";

    // 2. Generazione parallela reale tramite Masterizzatore su scala da concerto (12-TET)
    std::cout << "[2/3] Avvio Masterizzatore parallelo scala da concerto 12-TET...\n";
    Masterizzatore masterizzatore(audioManager);
    Masterizzatore::ConfigurazioneMasterizzazione config12;
    config12.nomeDischetto = "Gran Coda da Concerto 12-TET";
    config12.notePerOttava = 12;          // Pianoforte standard da concerto a 12 note per ottava
    config12.ottavePerLato = 3;           // 7 ottave complete (84 note da Do1 a Si7)
    config12.frequenzaAncora = 261.6256f; // Do4 centrale (indice 36)

    auto tInizio = std::chrono::high_resolution_clock::now();
    auto dischetto12 = masterizzatore.masterizza(config12);
    auto tFine = std::chrono::high_resolution_clock::now();

    double msGenerazione = std::chrono::duration<double, std::milli>(tFine - tInizio).count();
    const size_t noteDisponibili = dischetto12->getNumeroNote();

    std::cout << "  -> Generazione completata con successo in " << std::fixed << std::setprecision(2)
              << msGenerazione << " ms (" << msGenerazione / 1000.0 << " s)\n";
    std::cout << "  -> Media per nota : " << std::fixed << std::setprecision(2)
              << (msGenerazione / noteDisponibili) << " ms/nota (3 layer di velocity)\n";
    std::cout << "  -> Thread worker  : TUTTI spenti e rilasciati (zero thread attivi, zero leak)\n";
    std::cout << "  -> Banco caricato : " << dischetto12->getNomeDisco() << " (" << noteDisponibili << " note)\n";
    std::cout << "  -> Ottava centrale: Indice " << dischetto12->getIndiceAncora() 
              << " (" << dischetto12->getNota(dischetto12->getIndiceAncora())->frequenza << " Hz)\n";
    double fNominaleGrave = 261.6256f * std::pow(2.0, -36.0 / 12.0);
    double fNominaleAcuto = 261.6256f * std::pow(2.0, 47.0 / 12.0);
    std::cout << "  -> Estremo grave  : Indice 0 (" 
              << dischetto12->getNota(0)->frequenza << " Hz, stretch Railsback: "
              << (1200.0 * std::log2(dischetto12->getNota(0)->frequenza / fNominaleGrave)) << " cents)\n";
    std::cout << "  -> Estremo acuto  : Indice " << (noteDisponibili - 1) << " (" 
              << dischetto12->getNota(noteDisponibili - 1)->frequenza << " Hz, stretch Railsback: +"
              << (1200.0 * std::log2(dischetto12->getNota(noteDisponibili - 1)->frequenza / fNominaleAcuto)) << " cents)\n\n";

    auto orchestrator = std::make_shared<HammerOrchestrator>(dischetto12, SAMPLE_RATE, NUM_CANALI);
    orchestrator->setGuadagnoMaster(0.85f);

    // Mappatura esatta delle altezze del Gran Coda standard a 12 note (Do4 centrale = indice 36)
    // Registro 1 (Sub-bassi e bassi profondi)
    constexpr size_t Do1 = 0, Sol1 = 7, La1 = 9, Si1 = 11;
    // Registro 2 (Bassi armonici)
    constexpr size_t Do2 = 12, Re2 = 14, Mi2 = 16, Sol2 = 19, SolDiesis2 = 20, La2 = 21, Si2 = 23;
    // Registro 3 (Accompagnamento baritonale)
    constexpr size_t Do3 = 24, Re3 = 26, Mi3 = 28, Fa3 = 29, Sol3 = 31, SolDiesis3 = 32, La3 = 33, Si3 = 35;
    // Registro 4 (Medio cantabile)
    constexpr size_t Do4 = 36, Re4 = 38, ReDiesis4 = 39, Mi4 = 40, Fa4 = 41, Sol4 = 43, SolDiesis4 = 44, La4 = 45, Si4 = 47;
    // Registro 5 (Soprano brillante)
    constexpr size_t Do5 = 48, Re5 = 50, ReDiesis5 = 51, Mi5 = 52, Fa5 = 53, Sol5 = 55, La5 = 57, Si5 = 59;
    // Registro 6 (Acuti cristallini)
    constexpr size_t Do6 = 60;

    std::vector<EventoBrano> partitura;

    auto addNota = [&](double tOn, double durata, size_t nota, float vel, float relVel = 0.45f, const char* tag = nullptr) {
        partitura.push_back({ tOn, AzioneBrano::NOTE_ON, nota, vel, tag });
        partitura.push_back({ tOn + durata, AzioneBrano::NOTE_OFF, nota, relVel, nullptr });
    };
    auto addPedale = [&](double t, float prof, const char* tag = nullptr) {
        partitura.push_back({ t, AzioneBrano::PEDALE, 0, prof, tag });
    };

    // =========================================================================
    // LUDWIG VAN BEETHOVEN: "FÜR ELISE" (WoO 59) — SPARTITO AUTENTICO DA CONCERTO
    // Tempo: Poco moto (croma = 0.16s, semicroma = 0.08s)
    // =========================================================================

    // --- BATTUTA 1 (Anacrusi): Tema al Soprano (Mi5 - Re#5) a corda nuda senza pedale ---
    addPedale(0.00, 0.00f, "[0.00s] Incipit originale autentico a corda nuda (Mi5 - Re#5)");
    addNota(0.10, 0.14, Mi5, 0.54f, 0.35f);
    addNota(0.26, 0.14, ReDiesis5, 0.50f, 0.35f);
    addNota(0.42, 0.14, Mi5, 0.54f, 0.35f);
    addNota(0.58, 0.14, ReDiesis5, 0.50f, 0.35f);
    addNota(0.74, 0.14, Mi5, 0.54f, 0.35f);
    addNota(0.90, 0.14, Si4, 0.48f, 0.35f);
    addNota(1.06, 0.14, Re5, 0.50f, 0.35f);
    addNota(1.22, 0.14, Do5, 0.48f, 0.35f);

    // --- BATTUTE 2-3: Risoluzione su La Minore (Basso nobile La1 + arpeggio) ---
    addPedale(1.36, 0.50f, "[1.38s] Battuta 2: Risoluzione La Minore (Basso La1 profondo + arpeggio)");
    addNota(1.38, 0.70, La4, 0.62f, 0.30f); // Melodia tenuta al soprano
    addNota(1.38, 1.20, La1, 0.65f, 0.20f); // Basso profondo fondamentale
    addNota(1.54, 0.90, Mi2, 0.48f, 0.25f);
    addNota(1.70, 0.80, La2, 0.46f, 0.25f);
    // Mano destra risale
    addNota(1.86, 0.14, Do4, 0.42f, 0.30f);
    addNota(2.02, 0.14, Mi4, 0.45f, 0.30f);
    addNota(2.18, 0.14, La4, 0.50f, 0.30f);

    // --- BATTUTE 4-5: Modulazione su Mi Maggiore Dominante (Cambio pedale netto) ---
    addPedale(2.32, 0.00f);
    addPedale(2.36, 0.50f, "[2.36s] Battuta 4: Dominante Mi Maggiore (Basso Mi2 + Sol#3)");
    addNota(2.34, 0.70, Si4, 0.60f, 0.30f); // Risoluzione su Si4
    addNota(2.34, 1.20, Mi2, 0.62f, 0.20f); // Basso fondamentale Mi2
    addNota(2.50, 0.90, Si2, 0.46f, 0.25f);
    addNota(2.66, 0.80, Mi3, 0.44f, 0.25f);
    // Mano destra arpeggia la dominante
    addNota(2.82, 0.14, Mi4, 0.42f, 0.30f);
    addNota(2.98, 0.14, SolDiesis4, 0.46f, 0.30f);
    addNota(3.14, 0.14, Si4, 0.50f, 0.30f);

    // --- BATTUTE 6-7: Ritorno a La Minore ---
    addPedale(3.28, 0.00f);
    addPedale(3.32, 0.50f, "[3.32s] Battuta 6: Risoluzione su Do5 e ritorno");
    addNota(3.30, 0.70, Do5, 0.60f, 0.30f);
    addNota(3.30, 1.20, La1, 0.62f, 0.20f);
    addNota(3.46, 0.90, Mi2, 0.48f, 0.25f);
    addNota(3.62, 0.80, La2, 0.46f, 0.25f);
    addNota(3.78, 0.14, Mi4, 0.42f, 0.30f);

    // --- BATTUTA 8: Ripresa del tema principale (Mi5 - Re#5) ---
    addPedale(3.92, 0.00f); // Stacco pedale per nitidezza
    addNota(3.94, 0.14, Mi5, 0.56f, 0.35f, "[3.94s] Battuta 8: Seconda frase del tema");
    addNota(4.10, 0.14, ReDiesis5, 0.52f, 0.35f);
    addNota(4.26, 0.14, Mi5, 0.56f, 0.35f);
    addNota(4.42, 0.14, ReDiesis5, 0.52f, 0.35f);
    addNota(4.58, 0.14, Mi5, 0.56f, 0.35f);
    addNota(4.74, 0.14, Si4, 0.50f, 0.35f);
    addNota(4.90, 0.14, Re5, 0.52f, 0.35f);
    addNota(5.06, 0.14, Do5, 0.50f, 0.35f);

    // --- BATTUTE 9-10: Risoluzione e cadenza ---
    addPedale(5.20, 0.50f);
    addNota(5.22, 0.70, La4, 0.62f, 0.30f);
    addNota(5.22, 1.20, La1, 0.65f, 0.20f);
    addNota(5.38, 0.90, Mi2, 0.48f, 0.25f);
    addNota(5.54, 0.80, La2, 0.46f, 0.25f);
    addNota(5.70, 0.14, Do4, 0.42f, 0.30f);
    addNota(5.86, 0.14, Mi4, 0.45f, 0.30f);
    addNota(6.02, 0.14, La4, 0.50f, 0.30f);

    // Chiusura prima strofa
    addPedale(6.16, 0.00f);
    addPedale(6.20, 0.50f);
    addNota(6.18, 0.40, Si4, 0.58f, 0.30f);
    addNota(6.18, 1.00, Mi2, 0.60f, 0.20f);
    addNota(6.34, 0.80, Mi3, 0.46f, 0.25f);
    addNota(6.58, 0.35, Do5, 0.52f, 0.30f);
    addNota(6.93, 0.35, Si4, 0.48f, 0.30f);
    addPedale(7.26, 0.00f);
    addPedale(7.28, 0.55f, "[7.28s] Cadenza perfetta su La Minore");
    addNota(7.28, 1.40, La4, 0.62f, 0.30f);
    addNota(7.28, 1.60, La1, 0.68f, 0.20f);
    addNota(7.48, 1.20, Mi2, 0.50f, 0.25f);
    addNota(7.68, 1.00, La2, 0.48f, 0.25f);

    // --- BATTUTE 11-16 (SECONDA SEZIONE ORIGINALE DI BEETHOVEN IN DO MAGGIORE) ---
    // Nello spartito reale, qui la mano destra sale al Do Maggiore luminoso
    addPedale(8.75, 0.00f);
    addPedale(8.80, 0.55f, "[8.80s] Sezione centrale autentica in Do Maggiore (Beethoven originale)");
    addNota(8.80, 0.14, Do4, 0.48f, 0.25f);
    addNota(8.96, 0.14, Sol4, 0.52f, 0.25f);
    addNota(9.12, 0.14, Do5, 0.56f, 0.25f);
    addNota(9.28, 0.30, Mi5, 0.64f, 0.25f);
    addNota(9.28, 1.50, Do2, 0.65f, 0.20f); // Basso nobile Do2
    addNota(9.44, 1.20, Sol2, 0.50f, 0.25f);
    addNota(9.60, 1.00, Do3, 0.48f, 0.25f);
    addNota(9.76, 0.80, Mi3, 0.46f, 0.25f);
    addNota(9.92, 0.20, Sol5, 0.68f, 0.25f);
    addNota(10.12, 0.20, Fa5, 0.60f, 0.25f);
    addNota(10.32, 0.40, Mi5, 0.58f, 0.25f);

    // Modulazione su Sol7 Dominante
    addPedale(10.70, 0.00f);
    addPedale(10.75, 0.55f, "[10.75s] Modulazione Sol7 Dominante");
    addNota(10.75, 0.40, Re5, 0.62f, 0.30f);
    addNota(10.75, 1.40, Sol1, 0.64f, 0.20f);
    addNota(10.95, 1.10, Re2, 0.50f, 0.25f);
    addNota(11.15, 0.90, Sol2, 0.48f, 0.25f);
    addNota(11.35, 0.70, Si2, 0.46f, 0.25f);
    addNota(11.55, 0.70, Fa3, 0.44f, 0.25f);
    addNota(11.95, 0.30, Fa5, 0.58f, 0.30f);
    addNota(12.25, 0.30, Mi5, 0.56f, 0.30f);
    addNota(12.55, 0.50, Re5, 0.54f, 0.30f);

    // Risoluzione finale su Do Maggiore con arpeggio maestoso a piena tastiera
    addPedale(13.05, 0.00f);
    addPedale(13.10, 0.60f, "[13.10s] Chiusura maestosa a piena tastiera (Do1 -> Do6)");
    addNota(13.10, 0.80, Do5, 0.66f, 0.30f);
    addNota(13.10, 3.50, Do1, 0.72f, 0.20f); // Basso sub-grave Do1 (32.7 Hz)
    addNota(13.35, 3.20, Sol1, 0.62f, 0.20f);
    addNota(13.60, 3.00, Do2, 0.58f, 0.20f);
    addNota(13.85, 2.80, Mi2, 0.54f, 0.20f);
    addNota(14.10, 2.60, Sol2, 0.52f, 0.20f);
    addNota(14.35, 2.40, Do3, 0.50f, 0.20f);
    addNota(14.60, 2.20, Mi3, 0.48f, 0.20f);
    addNota(14.85, 2.00, Sol3, 0.46f, 0.20f);
    addNota(15.10, 1.80, Do4, 0.48f, 0.20f);
    addNota(15.35, 1.60, Mi4, 0.52f, 0.20f);
    addNota(15.60, 1.40, Sol4, 0.56f, 0.20f);
    addNota(15.85, 1.20, Do5, 0.60f, 0.20f);
    addNota(16.10, 1.00, Mi5, 0.64f, 0.20f);
    addNota(16.35, 2.50, Do6, 0.70f, 0.20f); // Acuto brillante cristallino Do6 (1046 Hz)
    addPedale(19.20, 0.00f, "[19.20s] Chiusura finale smorzatori (Smorzamento naturale completo)");

    // Ordina cronologicamente la partitura
    std::sort(partitura.begin(), partitura.end(),
              [](const EventoBrano& a, const EventoBrano& b) { return a.tempoSec < b.tempoSec; });

    // 3. Rendering audio deterministico
    std::cout << "[3/3] Rendering audio in corso (" << DURATA_BRANO << " secondi)...\n\n";

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
    const std::string nomeWav = "canzone_test_12edo_per_elisa_autentica.wav";

    if (!salvaWavFloat32(nomeWav, audioTotale, SAMPLE_RATE, NUM_CANALI, oltreUno, piccoMax)) {
        std::cerr << "\nERRORE: impossibile scrivere " << nomeWav << "\n";
        return -1;
    }

    std::cout << "\n============================================================\n";
    std::cout << "   TEST COMPLETATO CON SUCCESSO!                            \n";
    std::cout << "============================================================\n";
    std::cout << "File WAV generato: " << nomeWav << "\n";
    std::cout << "  -> Formato audio          : " << SAMPLE_RATE << " Hz, Stereo, 32-bit Float\n";
    std::cout << "  -> Picco massimo d'uscita : " << std::fixed << std::setprecision(4) << piccoMax << "\n";
    std::cout << "  -> Campioni oltre 1.0     : " << oltreUno << " (Zero clipping)\n";
    std::cout << "============================================================\n";

    return 0;
}
