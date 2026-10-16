#pragma once
#ifndef IAUDIODRIVER_HPP
#define IAUDIODRIVER_HPP

#include <cstddef>

class IAudioDriver {
public:
    virtual ~IAudioDriver() = default;

    /**
     * Inizializza l'hardware audio chiedendo al Sistema Operativo le sue impostazioni nativamente ottimali.
     * @return true se l'inizializzazione del dispositivo hardware è andata a buon fine.
     */
    virtual bool inizializza() = 0;

    virtual bool avvia() = 0;
    virtual bool ferma() = 0;
    virtual bool isAttivo() const = 0;

    virtual int getSampleRateReale() const = 0;
    virtual int getBufferSizeReale() const = 0;
    virtual int getNumeroCanaliReale() const = 0;
};

#endif // IAUDIODRIVER_HPP