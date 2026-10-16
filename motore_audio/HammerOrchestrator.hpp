#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#ifndef HAMMERORCHESTRATOR_HPP
#define HAMMERORCHESTRATOR_HPP

#include "Martelletto.hpp"
#include "Dischetto.hpp"
#include "EventoAudio.hpp"
#include "LockFreeQueue.hpp"

class HammerOrchestrator {
    public:
        static constexpr size_t MAX_VOCI = 128;
        HammerOrchestrator(std::shared_ptr<const Dischetto> dischetto, int sampleRateStream, int numeroCanali = 2);

        bool noteOn(size_t indiceNota, float velocity = 1.0f);
        bool noteOff(size_t indiceNota);
        bool sustainPedal(bool premuto);
        bool rilasciaTutto();

        float suonaSingoloCampione();
        void suonaCampioneStereo(float& outL, float& outR);
        void processaBlocco(float* outputBuffer, size_t numeroFrame);

        size_t getNumeroVoci() const;
        size_t getVociAttive() const;

        void setGuadagnoMaster(float nuovoGuadagno) {
            guadagno_master.store(nuovoGuadagno, std::memory_order_relaxed);
        }
        float getGuadagnoMaster() const {
            return guadagno_master.load(std::memory_order_relaxed);
        }
        
    private:
        std::atomic<size_t> voci_attive{0};
        std::shared_ptr<const Dischetto> dischetto_riferimento;
        int sample_rate;
        int numeroCanali;
        std::atomic<float> guadagno_master{0.70f};
        std::array<Martelletto, MAX_VOCI> martelletti;

        std::array<size_t, MAX_VOCI> free_stack;
        size_t free_top;

        std::array<size_t, MAX_VOCI> voci_attive_indici;
        size_t num_voci_attive = 0;

        std::vector<uint8_t> tasti_premuti; // Array per tenere traccia dei tasti premuti
        bool sustain_pedal_down = false;

        LockFreeQueue<EventoAudio> coda_eventi;

        void eseguiNoteOnInterno(size_t indiceNota, float velocity);
        void eseguiNoteOffInterno(size_t indiceNota);
        void eseguiSustainPedalInterno(bool premuto);
        void eseguiRilasciaTuttoInterno();
        size_t trovaMartellettoDaRubare();
        
        void aggiungiVoceAttiva(size_t index);
        void rimuoviVoceAttiva(size_t index);
};

#endif // HAMMERORCHESTRATOR_HPP