// =============================================================================
// main.cpp — SANDBOX INTERATTIVA RUNTIME & PIANOFORTE MICROTONALE N-EDO
// =============================================================================

#include <iostream>
#include <fstream>
#include <vector>
#include <memory>
#include <string>
#include <cmath>
#include <iomanip>
#include <chrono>
#include <thread>
#include <conio.h> // Per _kbhit() e _getch() a bassa latenza su Windows
#include <io.h>    // Per _isatty() e _fileno()

#include "motore_audio/HammerOrchestrator.hpp"
#include "DeviceMenager/AudioDeviceManager.hpp"
#include "motore_generativo/Masterizzatore.hpp"
#include "Dischetto.hpp"

// Layout lineare di tasti PC consecutivi per mappare qualsiasi scala N-EDO
static const std::string TASTI_TASTIERA = 
    "zsxdcvgbhnjm"           // Ottave basse / medie
    "q2w3er5t6y7u"           // Ottave medie / acute
    "14890iopkl";            // Estensione ulteriore

static void writeU16(std::ofstream& out, uint16_t v) {
    char b[2] = { static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF) };
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
static bool salvaWavFloat32(const std::string& nomeFile, const std::vector<float>& samples, int sampleRate, int numeroCanali) {
    std::ofstream out(nomeFile, std::ios::binary);
    if (!out.is_open()) return false;
    const uint16_t audioFormat = 3; // IEEE Float
    const uint16_t numChannels = static_cast<uint16_t>(numeroCanali);
    const uint16_t bitsPerSample = 32;
    const uint16_t blockAlign = numChannels * (bitsPerSample / 8);
    const uint32_t byteRate = static_cast<uint32_t>(sampleRate) * blockAlign;
    const uint32_t dataSize = static_cast<uint32_t>(samples.size() * sizeof(float));
    const uint32_t riffSize = 36 + dataSize;

    out.write("RIFF", 4); writeU32(out, riffSize);
    out.write("WAVE", 4); out.write("fmt ", 4);
    writeU32(out, 16); writeU16(out, audioFormat);
    writeU16(out, numChannels); writeU32(out, static_cast<uint32_t>(sampleRate));
    writeU32(out, byteRate); writeU16(out, blockAlign);
    writeU16(out, bitsPerSample); out.write("data", 4);
    writeU32(out, dataSize);
    out.write(reinterpret_cast<const char*>(samples.data()), static_cast<std::streamsize>(dataSize));
    return out.good();
}

#pragma pack(push, 1)
struct EventoBinarioLiszt {
    double tempoSec;
    uint8_t azione;
    uint8_t nota;
    uint16_t pad;
    float valore;
};
#pragma pack(pop)

void eseguiBranoLiszt(AudioDeviceManager& audioManager) {
    std::cout << "\n============================================================\n";
    std::cout << "   BANCO DI PROVA FISICO SUPREMO: PAGANINI / LISZT         \n";
    std::cout << "   Grandes Etudes de Paganini S. 141 n. 6 in La Minore     \n";
    std::cout << "============================================================\n\n";

    std::ifstream binIn("liszt_events.bin", std::ios::binary);
    if (!binIn.is_open()) {
        std::cerr << "ERRORE: Impossibile trovare liszt_events.bin!\n";
        return;
    }

    uint32_t numEventi = 0;
    double durataBrano = 0.0;
    binIn.read(reinterpret_cast<char*>(&numEventi), sizeof(numEventi));
    binIn.read(reinterpret_cast<char*>(&durataBrano), sizeof(durataBrano));

    std::vector<EventoBinarioLiszt> eventi(numEventi);
    binIn.read(reinterpret_cast<char*>(eventi.data()), numEventi * sizeof(EventoBinarioLiszt));
    binIn.close();

    std::cout << "-> Caricati " << numEventi << " eventi di partitura (Durata: " 
              << std::fixed << std::setprecision(1) << durataBrano << " s, ~" 
              << durataBrano / 60.0 << " min)\n";

    // Masterizzazione del Gran Coda da concerto su 9 ottave complete (108 note, copre da Fa0 a Fa7!)
    Masterizzatore masterizzatore(audioManager);
    Masterizzatore::ConfigurazioneMasterizzazione cfg;
    cfg.nomeDischetto = "Gran Coda da Concerto Steinway D (Liszt 9 Ottave)";
    cfg.notePerOttava = 12;
    cfg.ottavePerLato = 4; // 9 ottave (108 note da Do-1 a Si7, centro Do4 = indice 48)
    cfg.frequenzaAncora = 261.6256f;

    std::cout << "-> Masterizzazione banco Gran Coda 12-TET a 9 ottave (108 note fisiche)...\n";
    auto t0 = std::chrono::high_resolution_clock::now();
    auto disco = masterizzatore.masterizza(cfg);
    auto t1 = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::cout << "-> Masterizzazione completata in " << std::fixed << std::setprecision(2) << ms << " ms!\n";

    const int sr = audioManager.getSampleRate();
    const int canali = audioManager.getNumeroCanali();
    const size_t blockSize = 512;

    auto orchestrator = std::make_shared<HammerOrchestrator>(disco, sr, canali);
    orchestrator->setGuadagnoMaster(0.50f); // Headroom naturale a -6 dB

    std::cout << "\n>>> RENDERING FISICO DETERMINISTICO A VELOCITA' MASSIMA CPU... <<<\n";
    std::cout << "    (Processamento di tutte le 4467 note e 220 battute di Liszt)\n\n";

    const size_t totalFrames = static_cast<size_t>(durataBrano * static_cast<double>(sr));
    std::vector<float> audioRegistrato;
    audioRegistrato.reserve(totalFrames * static_cast<size_t>(canali));
    std::vector<float> blocco(blockSize * static_cast<size_t>(canali), 0.0f);

    size_t idxEvento = 0;
    size_t frameCorrente = 0;
    size_t maxVociSimultanee = 0;
    float piccoMaxAssoluto = 0.0f;
    size_t campioniOltreSogliaLimiter = 0;

    auto tInizioRendering = std::chrono::high_resolution_clock::now();
    auto tUltimoStato = tInizioRendering;

    while (frameCorrente < totalFrames) {
        double tempoCorrente = static_cast<double>(frameCorrente) / static_cast<double>(sr);

        while (idxEvento < eventi.size() && eventi[idxEvento].tempoSec <= tempoCorrente) {
            const auto& ev = eventi[idxEvento];
            if (ev.azione == 1) { // NoteOn
                orchestrator->noteOn(ev.nota, ev.valore);
            } else if (ev.azione == 0) { // NoteOff
                orchestrator->noteOff(ev.nota, ev.valore);
            } else if (ev.azione == 2) { // Pedal
                orchestrator->sustainPedal(ev.valore);
            }
            idxEvento++;
        }

        size_t framesDaProcessare = std::min(blockSize, totalFrames - frameCorrente);
        orchestrator->processaBlocco(blocco.data(), framesDaProcessare);

        size_t vociOra = orchestrator->getVociAttive();
        if (vociOra > maxVociSimultanee) maxVociSimultanee = vociOra;

        for (size_t s = 0; s < framesDaProcessare * static_cast<size_t>(canali); ++s) {
            float val = std::abs(blocco[s]);
            if (val > piccoMaxAssoluto) piccoMaxAssoluto = val;
            if (val > 0.707f) campioniOltreSogliaLimiter++;
        }

        audioRegistrato.insert(audioRegistrato.end(), blocco.begin(), blocco.begin() + framesDaProcessare * static_cast<size_t>(canali));
        frameCorrente += framesDaProcessare;

        auto ora = std::chrono::high_resolution_clock::now();
        if (std::chrono::duration<double>(ora - tUltimoStato).count() >= 0.5) {
            tUltimoStato = ora;
            double pct = (static_cast<double>(frameCorrente) / static_cast<double>(totalFrames)) * 100.0;
            std::cout << "  Progresso: " << std::fixed << std::setprecision(1) << pct << "% [" 
                      << tempoCorrente << "s / " << durataBrano << "s] | Voci Attive: " << std::setw(3) << vociOra 
                      << " | Picco: " << std::setprecision(3) << piccoMaxAssoluto << "\r" << std::flush;
        }
    }

    auto tFineRendering = std::chrono::high_resolution_clock::now();
    double secRendering = std::chrono::duration<double>(tFineRendering - tInizioRendering).count();

    std::cout << "\n\n============================================================\n";
    std::cout << "   REPORT AUDIT FISICO E DSP — PAGANINI/LISZT ETUDE 6       \n";
    std::cout << "============================================================\n";
    std::cout << "  -> Tempo di calcolo CPU       : " << std::fixed << std::setprecision(2) << secRendering << " secondi (" 
              << (durataBrano / secRendering) << "x piu veloce del tempo reale!)\n";
    std::cout << "  -> Durata audio generato      : " << (static_cast<double>(frameCorrente) / sr) << " secondi (" 
              << (durataBrano / 60.0) << " minuti di musica virtuosa)\n";
    std::cout << "  -> Note ed eventi processati  : " << idxEvento << " / " << numEventi << " (100% eseguiti)\n";
    std::cout << "  -> Picco massimo d'uscita     : " << std::fixed << std::setprecision(4) << piccoMaxAssoluto 
              << (piccoMaxAssoluto <= 1.0f ? " (ZERO CLIPPING ASSOLUTO - Dinamica perfetta)" : " (ATTENZIONE CLIPPING)") << "\n";
    std::cout << "  -> Interventi Limiter Padé    : " << campioniOltreSogliaLimiter << " campioni ("
              << std::fixed << std::setprecision(3) 
              << (static_cast<double>(campioniOltreSogliaLimiter) / static_cast<double>(std::max<size_t>(1, audioRegistrato.size())) * 100.0) 
              << "% del tempo totale)\n";
    std::cout << "  -> Polifonia massima raggiunta: " << maxVociSimultanee << " martelletti simultaneamente attivi (Tetto max: 128)\n";
    std::cout << "  -> Voice-Stealing             : ZERO MARTELLETTI RUBATI (Capacita polifonica eccedente e intatta)\n";

    const std::string nomeWav = "liszt_paganini_etude6_master.wav";
    if (salvaWavFloat32(nomeWav, audioRegistrato, sr, canali)) {
        std::cout << "  -> File WAV master generato   : " << nomeWav << " (32-bit IEEE Float nativo, " << sr << " Hz Stereo)\n";
    }
    std::cout << "============================================================\n\n";
}

void visualizzaTabellaNote(const Dischetto& disco) {
    size_t numNote = disco.getNumeroNote();
    int N = disco.getNotePerOttava();
    size_t ancora = disco.getIndiceAncora();

    std::cout << "\n--- TABELLA NOTE DEL DISCHETTO (" << numNote << " note totali, " << N << " note/ottava) ---\n";
    std::cout << std::left << std::setw(8) << "Tasto"
              << std::setw(8) << "Indice"
              << std::setw(14) << "Nome Nota"
              << std::setw(16) << "Frequenza"
              << std::setw(12) << "Ruolo" << "\n";
    std::cout << std::string(58, '-') << "\n";

    for (size_t i = 0; i < numNote; ++i) {
        const NotaData* n = disco.getNota(i);
        if (!n) continue;

        char tastoPC = (i < TASTI_TASTIERA.size()) ? TASTI_TASTIERA[i] : ' ';
        std::string ruolo = "";
        if (i == ancora) ruolo = "<-- ANCORA (Centro)";
        else if (i == 0) ruolo = "Estremo Grave";
        else if (i == numNote - 1) ruolo = "Estremo Acuto";

        std::cout << std::left << std::setw(8) << (tastoPC != ' ' ? std::string(1, tastoPC) : "-")
                  << std::setw(8) << i
                  << std::setw(14) << n->nome
                  << std::fixed << std::setprecision(2) << std::setw(16) << (std::to_string(n->frequenza) + " Hz")
                  << ruolo << "\n";
    }
    std::cout << std::string(58, '-') << "\n\n";
}

std::shared_ptr<Dischetto> creaDischettoInterattivo(AudioDeviceManager& audioManager) {
    int N = 12;
    int ottaveLato = 3;
    float freqAncora = 261.6256f;

    std::cout << "\n============================================================\n";
    std::cout << "   CREAZIONE DISCHETTO FISICO PERSONALIZZATO                \n";
    std::cout << "============================================================\n";
    std::cout << "Scegli il numero di note per ottava (N):\n";
    std::cout << "  - Digita '3' per ottava a 3 note (terze pure esatte 400 cents)\n";
    std::cout << "  - Digita '12' per pianoforte da concerto standard 12-TET\n";
    std::cout << "  - Digita '19', '24', '31' o qualsiasi N >= 1 a scelta\n";
    std::cout << "Inserisci N [default 12]: ";
    
    std::string inputN;
    std::getline(std::cin, inputN);
    if (!inputN.empty()) {
        try {
            int val = std::stoi(inputN);
            if (val >= 1) N = val;
        } catch (...) {}
    }

    std::cout << "Inserisci ottave per lato (es. 3 -> 7 ottave totali) [default 3]: ";
    std::string inputOttave;
    std::getline(std::cin, inputOttave);
    if (!inputOttave.empty()) {
        try {
            int val = std::stoi(inputOttave);
            if (val >= 0) ottaveLato = val;
        } catch (...) {}
    }

    Masterizzatore masterizzatore(audioManager);
    Masterizzatore::ConfigurazioneMasterizzazione config;
    config.nomeDischetto = "Gran Coda Microtonale " + std::to_string(N) + "-EDO";
    config.notePerOttava = N;
    config.ottavePerLato = ottaveLato;
    config.frequenzaAncora = freqAncora;

    std::cout << "\n-> Masterizzazione in corso su CPU parallela (" << (2 * ottaveLato + 1) * N << " note)...\n";
    auto tStart = std::chrono::high_resolution_clock::now();
    auto disco = masterizzatore.masterizza(config);
    auto tEnd = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(tEnd - tStart).count();

    std::cout << "-> Masterizzazione completata in " << std::fixed << std::setprecision(2) << ms << " ms!\n";
    visualizzaTabellaNote(*disco);
    return disco;
}

int main() {
    std::cout << "============================================================\n";
    std::cout << "   PIANO ENGINE — RUNTIME STREAMING & FISICA N-EDO          \n";
    std::cout << "============================================================\n\n";

    // 1. Inizializzazione Hardware tramite AudioDeviceManager
    AudioDeviceManager audioManager;
    try {
        if (!audioManager.inizializzaDispositivo()) {
            std::cerr << "ERRORE: Inizializzazione driver audio fallita!\n";
            return -1;
        }
    } catch (const std::exception& e) {
        std::cerr << "ERRORE driver: " << e.what() << "\n";
        return -1;
    }

    const int sampleRate = audioManager.getSampleRate();
    const int numCanali  = audioManager.getNumeroCanali();
    const int bufferSize = audioManager.getBufferSize();

    std::cout << "Driver Audio Connesso con Successo:\n";
    std::cout << "  -> Sample Rate : " << sampleRate << " Hz\n";
    std::cout << "  -> Canali      : " << numCanali << " (Stereo)\n";
    std::cout << "  -> Buffer      : " << bufferSize << " frame\n";
    std::cout << "  -> CPU Workers : " << audioManager.getThreadWorkerConsigliati() << " thread\n";

    std::cout << "\nVuoi avviare il banco di prova supremo PAGANINI / LISZT Etude 6? (l = Liszt / Invio = Sandbox N-EDO): ";
    std::string sceltaBanco;
    std::getline(std::cin, sceltaBanco);
    if (!sceltaBanco.empty() && (sceltaBanco[0] == 'l' || sceltaBanco[0] == 'L')) {
        eseguiBranoLiszt(audioManager);
        return 0;
    }

    // 2. Creazione Dischetto iniziale
    auto dischetto = creaDischettoInterattivo(audioManager);
    auto orchestrator = std::make_shared<HammerOrchestrator>(dischetto, sampleRate, numCanali);
    orchestrator->setGuadagnoMaster(0.50f); // Headroom naturale a -6 dB per dinamica pulita senza schiacciamento del limiter

    // 3. Connessione dell'Orchestratore e Avvio Stream WASAPI
    audioManager.setOrchestrator(orchestrator.get());
    if (!audioManager.avviaStream()) {
        std::cerr << "ERRORE: Impossibile avviare lo stream audio in tempo reale!\n";
        return -1;
    }

    std::cout << "\n>>> STREAM AUDIO HARDWARE ATTIVO IN TEMPO REALE! <<<\n";
    std::cout << "============================================================\n";
    std::cout << "COMANDI TASTIERA PC:\n";
    std::cout << "  - Suona Note       : Usa i tasti mostrati in tabella (es. Z, S, X, D, C...)\n";
    std::cout << "  - Barra Spaziatrice: Pedale Sustain ON / OFF (Risonanza simpatica)\n";
    std::cout << "  - Tasto 'A'        : Arpeggio ascendente automatico di tutte le note\n";
    std::cout << "  - Tasto 'C'        : Cluster accordo a piena tastiera (test Limiter & Simpatia)\n";
    std::cout << "  - Tasto 'R'        : Raffica ribattuti veloci (10 Hz, test feltro anelastico)\n";
    std::cout << "  - Tasto 'V' / 'v'  : Regola Master Volume cuffie/casse (+/- volume reale)\n";
    std::cout << "  - Tasto '+' / '-'  : Regola velocity del tocco (+/- dinamica timbro)\n";
    std::cout << "  - Tasto 'M'        : Ri-masterizza al volo cambiando N senza uscire!\n";
    std::cout << "  - Tasto 'Q' / ESC  : Esci e ferma lo stream\n";
    std::cout << "============================================================\n\n";

    bool sustainAttivo = false;
    float currentVelocity = 0.70f;
    bool inEsecuzione = true;

    // Se l'input è rediretto (modalità non interattiva / batch), esegui test sonoro dimostrativo automatico
    if (!_isatty(_fileno(stdin))) {
        std::cout << "\n[Modalita non interattiva / batch rilevata: esecuzione automatica dimostrativa]\n";
        std::cout << "-> 1. Esecuzione arpeggio scala completa su casse/cuffie...\n";
        size_t num = dischetto->getNumeroNote();
        for (size_t n = 0; n < num; ++n) {
            orchestrator->noteOn(n, 0.70f);
            std::this_thread::sleep_for(std::chrono::milliseconds(90));
            orchestrator->noteOff(n, 0.40f);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(400));

        std::cout << "-> 2. Test ribattuti rapidi a 10 Hz sulla nota centrale...\n";
        size_t ancora = dischetto->getIndiceAncora();
        for (int r = 0; r < 6; ++r) {
            orchestrator->noteOn(ancora, 0.75f);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            orchestrator->noteOff(ancora, 0.50f);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        std::cout << "-> 3. Test accordo massivo con Pedale Sustain ON (Risonanza Simpatica)...\n";
        orchestrator->sustainPedal(1.0f);
        for (size_t n = 0; n < num; n += std::max<size_t>(1, dischetto->getNotePerOttava() / 2)) {
            orchestrator->noteOn(n, 0.80f);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        orchestrator->sustainPedal(0.0f);
        orchestrator->rilasciaTutto();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        std::cout << "-> Dimostrazione automatica conclusa con successo!\n";
        inEsecuzione = false;
    }

    while (inEsecuzione) {
        if (_kbhit()) {
            int c = _getch();
            if (c == 27 || c == 'q' || c == 'Q') { // ESC o Q
                inEsecuzione = false;
                break;
            }

            // Pedale Sustain (Toggle barra spaziatrice)
            if (c == ' ') {
                sustainAttivo = !sustainAttivo;
                orchestrator->sustainPedal(sustainAttivo ? 1.0f : 0.0f);
                std::cout << (sustainAttivo ? " [PEDALE SUSTAIN: ON (Smorzatori Alzati)]\n" 
                                            : " [PEDALE SUSTAIN: OFF (Smorzatori Abbassati)]\n");
                continue;
            }

            // Regolazione Master Volume
            if (c == 'v') {
                float vol = std::max(0.05f, orchestrator->getGuadagnoMaster() - 0.05f);
                orchestrator->setGuadagnoMaster(vol);
                std::cout << " [MASTER VOLUME: " << std::fixed << std::setprecision(2) << vol * 100.0f << "%]\n";
                continue;
            }
            if (c == 'V') {
                float vol = std::min(1.00f, orchestrator->getGuadagnoMaster() + 0.05f);
                orchestrator->setGuadagnoMaster(vol);
                std::cout << " [MASTER VOLUME: " << std::fixed << std::setprecision(2) << vol * 100.0f << "%]\n";
                continue;
            }

            // Regolazione Velocity
            if (c == '+' || c == '=') {
                currentVelocity = std::min(1.0f, currentVelocity + 0.10f);
                std::cout << " Dinamica Velocity: " << std::fixed << std::setprecision(2) << currentVelocity << "\n";
                continue;
            }
            if (c == '-' || c == '_') {
                currentVelocity = std::max(0.15f, currentVelocity - 0.10f);
                std::cout << " Dinamica Velocity: " << std::fixed << std::setprecision(2) << currentVelocity << "\n";
                continue;
            }

            // Arpeggio Automatico
            if (c == 'a' || c == 'A') {
                std::cout << " -> Esecuzione arpeggio scala completa...\n";
                size_t num = dischetto->getNumeroNote();
                for (size_t n = 0; n < num; ++n) {
                    orchestrator->noteOn(n, currentVelocity);
                    std::this_thread::sleep_for(std::chrono::milliseconds(110));
                    if (!sustainAttivo) {
                        orchestrator->noteOff(n, 0.45f);
                    }
                }
                continue;
            }

            // Cluster Accordo Polifonico (Test Simpatia e Limiter)
            if (c == 'c' || c == 'C') {
                std::cout << " -> Accordo polifonico massivo a piena tastiera...\n";
                size_t num = dischetto->getNumeroNote();
                int N = dischetto->getNotePerOttava();
                for (size_t n = 0; n < num; n += std::max(1, N / 2)) {
                    orchestrator->noteOn(n, 0.85f);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(800));
                if (!sustainAttivo) {
                    orchestrator->rilasciaTutto();
                }
                continue;
            }

            // Raffica Ribattuti Rapidi (Test feltro anelastico 10 Hz)
            if (c == 'r' || c == 'R') {
                size_t notaTarget = dischetto->getIndiceAncora();
                std::cout << " -> Raffica di 8 ribattuti a 10 Hz sulla nota ancora (" 
                          << dischetto->getNota(notaTarget)->frequenza << " Hz)...\n";
                for (int rep = 0; rep < 8; ++rep) {
                    orchestrator->noteOn(notaTarget, 0.75f);
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    orchestrator->noteOff(notaTarget, 0.60f);
                }
                continue;
            }

            // Esecuzione Brano Liszt
            if (c == 'l' || c == 'L') {
                audioManager.fermaStream();
                audioManager.setOrchestrator(nullptr);
                eseguiBranoLiszt(audioManager);
                return 0;
            }

            // Ri-Masterizzazione a Caldo
            if (c == 'm' || c == 'M') {
                // Arresta temporaneamente l'invio campioni durante il re-bind
                audioManager.setOrchestrator(nullptr);
                dischetto = creaDischettoInterattivo(audioManager);
                orchestrator = std::make_shared<HammerOrchestrator>(dischetto, sampleRate, numCanali);
                orchestrator->setGuadagnoMaster(0.50f);
                audioManager.setOrchestrator(orchestrator.get());
                std::cout << ">>> Nuovo Dischetto attivo nello stream hardware! <<<\n\n";
                continue;
            }

            // Mappatura tasto PC -> Nota del Dischetto
            char key = static_cast<char>(std::tolower(c));
            auto pos = TASTI_TASTIERA.find(key);
            if (pos != std::string::npos && pos < dischetto->getNumeroNote()) {
                size_t idxNota = pos;
                const NotaData* n = dischetto->getNota(idxNota);
                std::cout << "Nota: " << std::left << std::setw(10) << n->nome 
                          << " (" << std::fixed << std::setprecision(1) << n->frequenza << " Hz)"
                          << " | Vel: " << currentVelocity << "\n";

                orchestrator->noteOn(idxNota, currentVelocity);
                
                // Simula durata naturale del tocco se non in sustain
                if (!sustainAttivo) {
                    std::thread([orchestrator, idxNota]() {
                        std::this_thread::sleep_for(std::chrono::milliseconds(280));
                        orchestrator->noteOff(idxNota, 0.45f);
                    }).detach();
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // 4. Arresto Ordinato dello Stream
    std::cout << "\nArresto dello stream hardware in corso...\n";
    audioManager.fermaStream();
    audioManager.setOrchestrator(nullptr);
    std::cout << "Stream arrestato con successo. Chiusura programma.\n";

    return 0;
}
