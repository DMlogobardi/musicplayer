#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <conio.h> // Libreria nativa Windows per leggere i tasti senza attendere INVIO (_getch, _kbhit)

#include "AudioDeviceManager.hpp"
#include "HammerOrchestrator.hpp"
#include "Dischetto.hpp"

int main() {
    std::cout << "==================================================" << std::endl;
    std::cout << "       PIANO ENGINE DIGITAL - WINDOWS TEST        " << std::endl;
    std::cout << "==================================================" << std::endl;

    try {
        // Step 1: Inizializza il gestore audio per negoziare le impostazioni con la scheda audio WASAPI
        std::cout << "\n[1/4] Inizializzazione driver audio Windows (WASAPI)..." << std::endl;
        AudioDeviceManager audioManager(nullptr);
        audioManager.inizializzaDispositivo();

        int sampleRateNativo = audioManager.getSampleRate();
        int numeroCanali = audioManager.getNumeroCanali();
        int bufferSize = audioManager.getBufferSize();

        std::cout << "  -> Sample Rate Hardware : " << sampleRateNativo << " Hz" << std::endl;
        std::cout << "  -> Canali Audio         : " << numeroCanali << std::endl;
        std::cout << "  -> Dimensione Buffer    : " << bufferSize << " frame" << std::endl;

        // Step 2: Genera un dischetto di prova con esattamente 7 note (corrispondenti ai tasti 1..7)
        std::cout << "\n[2/4] Creazione Dischetto di prova (7 note su " << sampleRateNativo << " Hz)..." << std::endl;
        auto dischetto = std::make_shared<Dischetto>();
        dischetto->simulaGenerazioneDisco(7, sampleRateNativo, 4.0f);

        // Step 3: Istanzia l'Orchestratore come shared_ptr per prevenire Use-After-Free nei thread detached
        std::cout << "\n[3/4] Inizializzazione HammerOrchestrator..." << std::endl;
        auto orchestrator = std::make_shared<HammerOrchestrator>(dischetto, sampleRateNativo, numeroCanali);
        orchestrator->setGuadagnoMaster(0.35f);

        // Step 4: Collega l'orchestratore all'AudioDeviceManager e avvia lo stream real-time
        audioManager.setOrchestrator(orchestrator.get());
        std::cout << "\n[4/4] Avvio dello Stream Audio in tempo reale..." << std::endl;
        
        if (!audioManager.avviaStream()) {
            std::cerr << "ERRORE CRITICO: Impossibile avviare lo stream audio su Windows!" << std::endl;
            return -1;
        }

        std::cout << "\n==================================================" << std::endl;
        std::cout << " MOTORE AUDIO ATTIVO E IN ESECUZIONE!" << std::endl;
        std::cout << " Comandi Tasti:" << std::endl;
        std::cout << "  [1] - [7] : Suona le 7 note del dischetto" << std::endl;
        std::cout << "  [SPAZIO]  : Attiva / Disattiva Pedale Sustain" << std::endl;
        std::cout << "  [R]       : Rilascia tutte le note (Smorzatori)" << std::endl;
        std::cout << "  [Q] / ESC : Arresta e chiudi l'applicazione" << std::endl;
        std::cout << "==================================================\n" << std::endl;

        bool sustainAttivo = false;
        bool inEsecuzione = true;

        while (inEsecuzione) {
            // Controlla se un tasto e' stato premuto sulla console senza bloccare l'esecuzione
            if (_kbhit()) {
                int tasto = _getch();

                // Tasti da '1' a '7' per attivare Note ON
                if (tasto >= '1' && tasto <= '7') {
                    size_t indiceNota = static_cast<size_t>(tasto - '1');
                    std::cout << "[NOTE ON] Tasto " << (indiceNota + 1) << " premuto (Velocity 0.85)" << std::endl;
                    
                    orchestrator->noteOn(indiceNota, 0.85f);

                    // Simuliamo il rilascio del tasto (Note OFF) dopo 500 ms su un thread separato
                    // La cattura per valore di orchestrator (shared_ptr) garantisce la sicurezza della memoria
                    std::thread([orchestrator, indiceNota]() {
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                        orchestrator->noteOff(indiceNota);
                    }).detach();
                }
                // Barra Spaziatrice: Toggle Pedale Sustain
                else if (tasto == ' ') {
                    sustainAttivo = !sustainAttivo;
                    orchestrator->sustainPedal(sustainAttivo);
                    std::cout << "[SUSTAIN] Pedale " << (sustainAttivo ? "PREMUTO (ON)" : "RILASCIATO (OFF)") << std::endl;
                }
                // Tasto R: Smorza tutte le note
                else if (tasto == 'r' || tasto == 'R') {
                    orchestrator->rilasciaTutto();
                    std::cout << "[RESET] Tutte le note rilasciate!" << std::endl;
                }
                // Tasto Q o ESC (ASCII 27): Uscita
                else if (tasto == 'q' || tasto == 'Q' || tasto == 27) {
                    std::cout << "\n[EXIT] Chiusura richiesta dall'utente..." << std::endl;
                    inEsecuzione = false;
                }
            }

            // Pausa breve del thread di controllo CLI per non saturare la CPU
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Arresto controllato del sistema e pulizia della memoria
        std::cout << "Arresto dello stream audio WASAPI..." << std::endl;
        audioManager.fermaStream();
        audioManager.setOrchestrator(nullptr);

        std::cout << "Programma terminato correttamente. Nessun memory leak." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "\nECCEZIONE FATALE RILEVATA: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}