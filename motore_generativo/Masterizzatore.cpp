#include "Masterizzatore.hpp"
#include "Testina.hpp"
#include <thread>
#include <atomic>
#include <vector>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

Masterizzatore::Masterizzatore(const AudioDeviceManager& deviceManager)
    : device_manager(deviceManager) {}

std::shared_ptr<Dischetto> Masterizzatore::masterizza(const ConfigurazioneMasterizzazione& config) {
    int sampleRate = device_manager.getSampleRate();
    if (sampleRate <= 0) {
        // Fallback di sicurezza se interrogato prima dell'avvio dello stream
        sampleRate = 44100;
    }

    int N = std::max(1, config.notePerOttava);
    int O_lato = std::max(0, config.ottavePerLato);
    int K_ottave = 2 * O_lato + 1;
    size_t totaleNote = static_cast<size_t>(K_ottave * N);
    size_t indiceAncora = static_cast<size_t>(O_lato * N);

    // Pre-allocazione della memoria: ogni cella ha il suo spazio isolato
    std::vector<NotaData> note(totaleNote);

    // Determinazione del numero di thread worker contrattuali
    unsigned int numWorkers = device_manager.getThreadWorkerConsigliati();
    if (numWorkers == 0) numWorkers = 1;
    numWorkers = std::min<unsigned int>(numWorkers, static_cast<unsigned int>(totaleNote));

    std::atomic<size_t> indiceCondiviso{0};

    auto workerFunc = [&]() {
        Testina::ParametriSintesi params;
        params.sampleRate = sampleRate;
        params.livelloNormalizzazione = 0.35f; // Headroom acustica naturale per non saturare
        Testina testina(params);

        while (true) {
            size_t i = indiceCondiviso.fetch_add(1);
            if (i >= totaleNote) break;

            // Calcolo frequenza continua N-EDO con allargamento di Railsback (Stretch Tuning acustico)
            double esponente = static_cast<double>(static_cast<long long>(i) - static_cast<long long>(indiceAncora)) / static_cast<double>(N);
            double centsStretch = 0.0;
            if (esponente > 0.0) {
                // Curva Railsback asimmetrica Gran Coda: compensazione fisica inarmonicità acuti
                centsStretch = 2.85 * std::pow(esponente, 1.55);
            } else if (esponente < 0.0) {
                // Curva Railsback asimmetrica Gran Coda: compensazione fisica parziali gravi
                centsStretch = -2.20 * std::pow(-esponente, 1.45);
            }
            float f0 = config.frequenzaAncora * static_cast<float>(std::pow(2.0, esponente + centsStretch / 1200.0));

            // Panning equal-power stereofonico (Bassi a sinistra, Acuti a destra)
            float panPos = (totaleNote > 1) ? (static_cast<float>(i) / static_cast<float>(totaleNote - 1)) : 0.5f;
            float panL = std::cos(panPos * static_cast<float>(M_PI) * 0.5f);
            float panR = std::sin(panPos * static_cast<float>(M_PI) * 0.5f);

            // Nome descrittivo per Flutter e debug: Ottava relativa e grado
            int ottavaRelativa = static_cast<int>(i / N) - O_lato;
            int gradoRelativo = static_cast<int>(i % N);
            std::string nomeNota = "O" + std::to_string(ottavaRelativa) + "_G" + std::to_string(gradoRelativo);

            // Scrittura diretta e isolata nella cella i-esima del Dischetto
            testina.generaSingolaNota(f0, nomeNota, panL, panR, note[i]);
        }
    };

    // Avvio ed esecuzione del pool di thread worker
    std::vector<std::thread> threads;
    threads.reserve(numWorkers);
    for (unsigned int w = 0; w < numWorkers; ++w) {
        threads.emplace_back(workerFunc);
    }

    for (auto& th : threads) {
        if (th.joinable()) {
            th.join();
        }
    }

    return std::make_shared<Dischetto>(sampleRate, N, std::move(note), config.nomeDischetto, indiceAncora, O_lato);
}
