#ifndef MASTERIZZATORE_HPP
#define MASTERIZZATORE_HPP

#include "Dischetto.hpp"
#include "DeviceMenager/AudioDeviceManager.hpp"
#include <string>
#include <memory>

class Masterizzatore {
public:
    struct ConfigurazioneMasterizzazione {
        std::string nomeDischetto = "Pianoforte Microtonale";
        int notePerOttava = 12;             // Divisione dell'ottava N
        int ottavePerLato = 3;              // Ottave sopra e sotto il centro (3 -> 7 ottave)
        float frequenzaAncora = 261.6256f;  // Frequenza del tasto centrale (Do4 standard)
    };

    explicit Masterizzatore(const AudioDeviceManager& deviceManager);

    /**
     * Masterizza l'intero banco suoni in parallelo e restituisce il Dischetto finito.
     * @param config Parametri di generazione (N, ottave, nome)
     * @return Dischetto popolato e indicizzato O(1) pronto per il Motore Audio e Flutter
     */
    std::shared_ptr<Dischetto> masterizza(const ConfigurazioneMasterizzazione& config);

private:
    const AudioDeviceManager& device_manager;
};

#endif // MASTERIZZATORE_HPP
