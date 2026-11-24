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
        float pedale_smooth = 0.0f;         // Inviluppo a un polo continuo anti-click [0.0 ... 1.0]
        bool sustain_pedal_down = false;

        LockFreeQueue<EventoAudio> coda_eventi;

        void eseguiNoteOnInterno(size_t indiceNota, float velocity);
        void eseguiNoteOffInterno(size_t indiceNota, float releaseVelocity);
        void eseguiSustainPedalInterno(float profondita);
        void eseguiRilasciaTuttoInterno();
        size_t trovaMartellettoDaRubare();
        
        void aggiungiVoceAttiva(size_t index);
        void rimuoviVoceAttiva(size_t index);

        // --- Task 6: Risonanza Simpatica Multimodale (Fondamentale + 2°/3° Armonico) ---
        struct RisonatoreSimpatia {
            // Modo 1 (f0)
            double c1_1 = 0.0, c2_1 = 0.0, g_1 = 0.0;
            double y1_1 = 0.0, y2_1 = 0.0;
            // Modo 2 (2*f0 - ottava)
            double c1_2 = 0.0, c2_2 = 0.0, g_2 = 0.0;
            double y1_2 = 0.0, y2_2 = 0.0;
            // Modo 3 (3*f0 - dodicesima)
            double c1_3 = 0.0, c2_3 = 0.0, g_3 = 0.0;
            double y1_3 = 0.0, y2_3 = 0.0;

            float guadagno_L = 0.707f;
            float guadagno_R = 0.707f;

            float processa(float in) {
                double d_in = static_cast<double>(in);
                double out1 = c1_1 * y1_1 - c2_1 * y2_1 + g_1 * d_in;
                y2_1 = y1_1; y1_1 = out1;
                if (std::abs(y1_1) < 1e-15) y1_1 = 0.0;
                if (std::abs(y2_1) < 1e-15) y2_1 = 0.0;

                double out2 = c1_2 * y1_2 - c2_2 * y2_2 + g_2 * d_in;
                y2_2 = y1_2; y1_2 = out2;
                if (std::abs(y1_2) < 1e-15) y1_2 = 0.0;
                if (std::abs(y2_2) < 1e-15) y2_2 = 0.0;

                double out3 = c1_3 * y1_3 - c2_3 * y2_3 + g_3 * d_in;
                y2_3 = y1_3; y1_3 = out3;
                if (std::abs(y1_3) < 1e-15) y1_3 = 0.0;
                if (std::abs(y2_3) < 1e-15) y2_3 = 0.0;

                return static_cast<float>(out1 + out2 + out3);
            }
            void attenua() {
                y1_1 *= 0.95; y2_1 *= 0.95;
                y1_2 *= 0.95; y2_2 *= 0.95;
                y1_3 *= 0.95; y2_3 *= 0.95;
                if (std::abs(y1_1) < 1e-15 && std::abs(y1_2) < 1e-15 && std::abs(y1_3) < 1e-15) reset();
            }
            void dissipa() {
                y1_1 *= 0.9999; y2_1 *= 0.9999;
                y1_2 *= 0.9999; y2_2 *= 0.9999;
                y1_3 *= 0.9999; y2_3 *= 0.9999;
            }
            bool isAttivo() const {
                return (y1_1 != 0.0 || y1_2 != 0.0 || y1_3 != 0.0);
            }
            void reset() {
                y1_1 = y2_1 = 0.0;
                y1_2 = y2_2 = 0.0;
                y1_3 = y2_3 = 0.0;
            }
        };

        std::vector<RisonatoreSimpatia> risonatori_simpatia;
        // Memoria di stato per Anti-Derivative Anti-Aliasing (ADAA) del ponte
        float adaa_bridge_x1 = 0.0f;
        float adaa_bridge_F1 = 1.0f;
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

        Biquad soundboard_bass_L, soundboard_bass_R; // Risonanza profonda cassa (125 Hz)
        Biquad soundboard_body_L, soundboard_body_R; // Risonanza corpo legno (280 Hz - calore medio)
        Biquad soundboard_presence_L, soundboard_presence_R; // Presenza ponte e legno (2100 Hz - proiezione)
        Biquad soundboard_high_L, soundboard_high_R; // Assorbimento naturale aria (8500 Hz)

        void inizializzaTavolaArmonica(int sampleRate);
        void applicaTavolaArmonica(float& inL, float& inR);

        // --- Modello Meccanico Rumore Pedaliera & Feltri Smorzatori ---
        struct ModelloMeccanicaPedale {
            float pedale_precedente = 0.0f;
            Biquad whoosh_L, whoosh_R; // Filtro passa-banda sollevamento feltri (1400 Hz)
            Biquad thud_L, thud_R;     // Filtro passa-basso ricaduta feltri (135 Hz)
            float env_whoosh = 0.0f;
            float decay_whoosh = 0.9993f;
            float env_thud = 0.0f;
            float decay_thud = 0.9986f;
            uint32_t prng_state = 123456789;

            inline float rumoreUniforme() {
                prng_state = prng_state * 1664525u + 1013904223u;
                return static_cast<float>(static_cast<int32_t>(prng_state)) * 4.6566129e-10f;
            }
            void configura(int sampleRate);
            void aggiorna(float nuovaProfondita, int sampleRate);
            void processa(float& outL, float& outR);
        } meccanica_pedale;
};

#endif // HAMMERORCHESTRATOR_HPP