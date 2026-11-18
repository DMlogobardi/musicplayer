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
        bool noteOff(size_t indiceNota, float releaseVelocity = 0.5f);
        bool sustainPedal(float profondita);
        bool sustainPedal(bool premuto) { return sustainPedal(premuto ? 1.0f : 0.0f); }
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
        float profondita_pedale = 0.0f;     // Corsa continua pedale di risonanza [0.0 ... 1.0]
        bool sustain_pedal_down = false;

        LockFreeQueue<EventoAudio> coda_eventi;

        void eseguiNoteOnInterno(size_t indiceNota, float velocity);
        void eseguiNoteOffInterno(size_t indiceNota, float releaseVelocity);
        void eseguiSustainPedalInterno(float profondita);
        void eseguiRilasciaTuttoInterno();
        size_t trovaMartellettoDaRubare();
        
        void aggiungiVoceAttiva(size_t index);
        void rimuoviVoceAttiva(size_t index);

        // --- Task 6: Risonanza Simpatica tra Corde Libere (Ponticello / Sympathetic String Bus) ---
        struct RisonatoreSimpatia {
            double c1 = 0.0;
            double c2 = 0.0;
            double g = 0.0;
            double y1 = 0.0;
            double y2 = 0.0;
            float guadagno_L = 0.707f;
            float guadagno_R = 0.707f;

            float processa(float in) {
                double out = c1 * y1 - c2 * y2 + g * static_cast<double>(in);
                y2 = y1;
                y1 = out;
                if (std::abs(y1) < 1e-15) y1 = 0.0;
                if (std::abs(y2) < 1e-15) y2 = 0.0;
                return static_cast<float>(out);
            }
            void reset() { y1 = 0.0; y2 = 0.0; }
        };

        std::vector<RisonatoreSimpatia> risonatori_simpatia;
        void inizializzaRisonanzaSimpatica(int sampleRate);
        void applicaRisonanzaSimpatica(float inBridge, float& outL, float& outR);

        // --- Task 7: Tavola Armonica (Soundboard Resonator & Body Filter) ---
        struct Biquad {
            float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
            float a1 = 0.0f, a2 = 0.0f;
            float z1 = 0.0f, z2 = 0.0f;

            float processa(float in) {
                float out = b0 * in + z1;
                z1 = b1 * in - a1 * out + z2;
                z2 = b2 * in - a2 * out;
                if (std::abs(z1) < 1e-15f) z1 = 0.0f;
                if (std::abs(z2) < 1e-15f) z2 = 0.0f;
                return out;
            }
            void reset() { z1 = 0.0f; z2 = 0.0f; }
        };

        Biquad soundboard_low_L, soundboard_low_R;
        Biquad soundboard_high_L, soundboard_high_R;
        std::vector<float> soundboard_diff_buffer_L;
        std::vector<float> soundboard_diff_buffer_R;
        size_t diff_idx = 0;
        size_t diff_delay = 66;

        void inizializzaTavolaArmonica(int sampleRate);
        void applicaTavolaArmonica(float& inL, float& inR);
};

#endif // HAMMERORCHESTRATOR_HPP