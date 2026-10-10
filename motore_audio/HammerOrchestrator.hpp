#pragma once
#include <array>
#include "Martelletto.hpp"
#include "Dischetto.hpp"
#include "EventoAudio.hpp"
#include "LockFreeQueue.hpp"

#ifndef HAMMERORCHESTRATOR_HPP
#define HAMMERORCHESTRATOR_HPP

class HammerOrchestrator {
    public:
        static constexpr size_t MAX_VOCI = 32;
        HammerOrchestrator(const Dischetto* dischetto, int sampleRateStream, int numeroCanali = 2);

        void noteOn(size_t indiceNota, float velocity = 1.0f);
        void noteOff(size_t indiceNota);
        void rilasciaTutto();

        float suonaSingoloCampione();
        void processaBlocco(float* outputBuffer, size_t numeroFrame);

        size_t getNumeroVoci() const;
        size_t getVociAttive() const;
    private:
        const Dischetto* dischetto_riferimento;
        int sample_rate;
        int numeroCanali;
        const float guadagno_master = 0.25f;
        std::array<Martelletto, MAX_VOCI> martelletti;

        std::array<size_t, MAX_VOCI> free_stack;
        size_t free_top;

        LockFreeQueue<EventoAudio> coda_eventi;

        void eseguiNoteOnInterno(size_t indiceNota, float velocity);
        void eseguiNoteOffInterno(size_t indiceNota);
        void eseguiRilasciaTuttoInterno();
        size_t trovaMartellettoDaRubare();
};

#endif // HAMMERORCHESTRATOR_HPP