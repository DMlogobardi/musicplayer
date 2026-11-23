#include "Testina.hpp"
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <random>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

Testina::Testina(const ParametriSintesi& parametri)
    : params(parametri) {
    if (params.sampleRate <= 0) {
        throw std::invalid_argument("Testina: sampleRate deve essere positivo!");
    }
}

float Testina::calcolaDurataBuffer(float f0) const {
    // I bassi hanno risonanza lunga (~5.5s), gli acuti si estinguono rapidamente (~1.2s)
    float d = 1.4f + 3.8f * std::pow(120.0f / (f0 + 80.0f), 0.70f);
    return std::clamp(d, 1.2f, 5.5f);
}

float Testina::calcolaInarmonicita(float f0) const {
    // Inarmonicità Fletcher-Rossing scalata con la frequenza della corda
    constexpr float f_ref = 261.6256f;
    constexpr float B_ref = 0.00035f;
    float B = B_ref * std::pow(f0 / f_ref, 0.85f);
    return std::clamp(B, 0.00004f, 0.015f);
}

float Testina::calcolaPuntoPercussione(float f0) const {
    // 1/8 nei bassi (0.125), avvicinandosi al capotasto negli acuti (fino a 0.065)
    float beta = 0.125f - 0.058f * std::clamp(f0 / 2500.0f, 0.0f, 1.0f);
    return std::clamp(beta, 0.065f, 0.125f);
}

int Testina::determinaNumeroCorde(float f0) const {
    if (f0 < 95.0f) return 1;       // Monocordo nei bassi profondi
    if (f0 < 230.0f) return 2;      // Bicordo nel registro medio-grave
    return 3;                       // Tricordo nel registro medio e acuto
}

void Testina::generaSingolaNota(float frequenzaFondamentale,
                                const std::string& nomeNota,
                                float panL,
                                float panR,
                                NotaData& destinazione) {
    if (frequenzaFondamentale <= 0.0f) return;

    destinazione.frequenza = frequenzaFondamentale;
    destinazione.nome = nomeNota;
    destinazione.guadagno_L = panL;
    destinazione.guadagno_R = panR;
    destinazione.layers.clear();

    float durataSec = calcolaDurataBuffer(frequenzaFondamentale);
    size_t totaleCampioni = static_cast<size_t>(durataSec * params.sampleRate);

    // 3 Layer di velocity fisici standard: Piano, Mezzo-Forte, Forte
    const std::vector<float> soglieVelocity = { 0.35f, 0.70f, 1.00f };

    for (float soglia : soglieVelocity) {
        LayerVelocity layer;
        layer.soglia_velocity = soglia;
        layer.buffer_campioni.resize(totaleCampioni, 0.0f);

        sintetizzaLayerFasori(frequenzaFondamentale, soglia, totaleCampioni, layer.buffer_campioni);
        destinazione.layers.push_back(std::move(layer));
    }

    // Normalizzazione coerente di tutti i layer al picco massimo (preserva timbro e dinamica)
    float piccoMaxNota = 0.0f;
    for (const auto& lay : destinazione.layers) {
        for (float s : lay.buffer_campioni) {
            float a = std::abs(s);
            if (a > piccoMaxNota) piccoMaxNota = a;
        }
    }

    if (piccoMaxNota > 1e-6f) {
        float scala = params.livelloNormalizzazione / piccoMaxNota;
        for (auto& lay : destinazione.layers) {
            for (float& s : lay.buffer_campioni) {
                s *= scala;
            }
        }
    }
}

// Struttura fasore complesso a 64 bit per sintesi esente da drift e da funzioni trigonometriche nel loop
struct FasoreParziale {
    double re_p, im_p;       // Stato del fasore Prompt (in-phase prompt sound)
    double w_re_p, w_im_p;   // Moltiplicatore di rotazione e smorzamento prompt
    double re_a, im_a;       // Stato del fasore Aftersound (out-of-phase aftersound)
    double w_re_a, w_im_a;   // Moltiplicatore aftersound
};

void Testina::sintetizzaLayerFasori(float f0, 
                                   float sogliaVelocity, 
                                   size_t totaleCampioni, 
                                   std::vector<float>& bufferUscita) {
    const double fs = static_cast<double>(params.sampleRate);
    const double nyquist = 0.48 * fs;

    float B = calcolaInarmonicita(f0);
    float beta = calcolaPuntoPercussione(f0);
    double v_norm = std::clamp(static_cast<double>(sogliaVelocity), 0.15, 1.0);

    // --- 1. MODELLO FISICO DEL CONTATTO MARTELLETTO-CORDA (Chaigne-Askenfelt / Suzuki) ---
    // Tempo di contatto reale tc(v, f0): decresce con la velocity per la compressione non lineare F = k*(dy)^p
    double tc = (0.00055 + 0.0022 / std::sqrt(1.0 + static_cast<double>(f0) / 120.0)) * std::pow(v_norm, -0.22);

    // Larghezza finita di contatto del feltro (spatial strike profile) delta_xi = delta / L
    double delta_xi = std::clamp(0.010 * std::pow(static_cast<double>(f0) / 261.6256, 0.25), 0.004, 0.030);

    // --- 2. FISICA REALE DELLE CORDE: MONOCORDO, BICORDO, TRICORDO ---
    const int numCorde = determinaNumeroCorde(f0);
    const double ampiezzaCorda = 1.0 / std::sqrt(static_cast<double>(numCorde));
    double decadimentoRegistro = std::sqrt(f0 / 261.6256);

    // Scordamento continuo simmetrico (Weinreich 1977): zero battimenti spuri nei monocordi dei bassi
    double detuningBase = (numCorde > 1) ? (0.20 + 0.55 * (static_cast<double>(f0) / (static_cast<double>(f0) + 350.0))) : 0.0;
    double detuningCents[3] = { 0.0, 0.0, 0.0 };
    if (numCorde == 2) {
        detuningCents[0] = -detuningBase;
        detuningCents[1] = +detuningBase;
    } else if (numCorde == 3) {
        detuningCents[0] = -detuningBase;
        detuningCents[1] = 0.0;
        detuningCents[2] = +detuningBase;
    }

    // Frazione continua dell'aftersound: cresce dolcemente dal registro grave al cantabile
    double quotaAfter = 0.08 + 0.08 * (static_cast<double>(f0) / (static_cast<double>(f0) + 300.0));
    double quotaPrompt = 1.0 - quotaAfter;

    std::vector<FasoreParziale> fasori;
    fasori.reserve(80 * numCorde);

    // Costruzione delle parziali sotto il limite di Nyquist (anti-aliasing)
    constexpr int MAX_PARZIALI = 80;
    for (int k = 1; k <= MAX_PARZIALI; ++k) {
        double freqParziale = k * f0 * std::sqrt(1.0 + B * k * k);
        if (freqParziale >= nyquist) break;

        // A. Efficienza spaziale con larghezza finita del feltro: Sk = sin(k*pi*beta) * sinc(k*pi*delta_xi)
        double argSinc = M_PI * k * delta_xi;
        double sincFeltro = (std::abs(argSinc) > 1e-5) ? (std::sin(argSinc) / argSinc) : 1.0;
        double s_k = std::sin(M_PI * k * beta) * sincFeltro;
        double polaritaModale = (s_k >= 0.0) ? 1.0 : -1.0;
        double ampiezzaSpaziale = std::max(std::abs(s_k), 0.04);

        // B. Trasformata di Fourier dell'impulso non lineare di forza del martelletto (Hall / Suzuki)
        // H_H(f) = |cos(pi*f*tc) / (1 - (2*f*tc)^2)|
        double x_tc = 2.0 * freqParziale * tc;
        double denom_tc = 1.0 - x_tc * x_tc;
        double h_hammer;
        if (std::abs(denom_tc) > 0.06) {
            h_hammer = std::abs(std::cos(M_PI * freqParziale * tc) / denom_tc);
        } else {
            h_hammer = M_PI * 0.25; // Limite continuo per x_tc -> 1
        }
        h_hammer = std::pow(std::clamp(h_hammer, 0.001, 1.2), 0.85);

        // C. Spettro armonico integrato
        double ampiezzaK = (1.0 / static_cast<double>(k)) * ampiezzaSpaziale * h_hammer;

        // Rinforzo calore armonico registro cantabile
        if (k >= 2 && k <= 5) {
            double fattoreCalore = 1.0 + 0.25 * (350.0 / (static_cast<double>(f0) + 180.0));
            ampiezzaK *= std::min(1.30, fattoreCalore);
        }
        if (freqParziale < 75.0) {
            double ratio = freqParziale / 75.0;
            ampiezzaK *= std::max(0.20, ratio * ratio);
        }

        // D. Smorzamento modale fisico del gran coda
        double alpha_k = decadimentoRegistro * (0.65 + 0.14 * (k - 1) + 0.0022 * (k * k));
        double alpha_prompt = 1.40 * alpha_k;
        double alpha_after  = 0.65 * alpha_k;

        for (int m = 0; m < numCorde; ++m) {
            double freqCorda = freqParziale * std::pow(2.0, detuningCents[m] / 1200.0);
            if (freqCorda >= nyquist) continue;

            double omega = 2.0 * M_PI * freqCorda / fs;

            FasoreParziale f;
            double r_p = std::exp(-alpha_prompt / fs);
            f.w_re_p = r_p * std::cos(omega);
            f.w_im_p = r_p * std::sin(omega);

            double omega_after = omega * (1.0 + 0.00030 * (150.0 / (static_cast<double>(f0) + 100.0)));
            double r_a = std::exp(-alpha_after / fs);
            f.w_re_a = r_a * std::cos(omega_after);
            f.w_im_a = r_a * std::sin(omega_after);

            double ampPrompt = quotaPrompt * ampiezzaK * ampiezzaCorda;
            double ampAfter  = quotaAfter  * ampiezzaK * ampiezzaCorda;

            // Fase d'onda fisica con spostamento iniziale nullo y(0) = 0 (onda sinusoidale).
            // Elimina il salto cosinusoidale istantaneo al campione zero e fonde
            // perfettamente l'attacco della corda con il colpo del martelletto.
            f.re_p = 0.0;
            f.im_p = -ampPrompt * polaritaModale;

            // Modo ortogonale aftersound a spostamento nullo iniziale:
            // l'energia orizzontale cresce per accoppiamento dinamico progressivo
            f.re_a = 0.0;
            f.im_a = -ampAfter * polaritaModale;

            fasori.push_back(f);
        }
    }

    // Sintesi ultra-rapida a blocchi da 128 campioni con potatura O(1) delle parziali estinte
    constexpr size_t BLOCK_SIZE = 128;
    size_t numFasoriAttivi = fasori.size();

    // Rampa di accoppiamento organico di Weinreich (~35 ms):
    // il modo ortogonale cresce gradualmente per trasferimento d'energia dalla tavola
    double bloomingEnv = 0.0;
    const double bloomingCoeff = std::exp(-1.0 / (0.035 * fs));

    for (size_t offset = 0; offset < totaleCampioni; offset += BLOCK_SIZE) {
        size_t blockEnd = std::min(offset + BLOCK_SIZE, totaleCampioni);

        for (size_t n = offset; n < blockEnd; ++n) {
            bloomingEnv = 1.0 - (1.0 - bloomingEnv) * bloomingCoeff;
            double campioneTotale = 0.0;

            for (size_t idx = 0; idx < numFasoriAttivi; ++idx) {
                auto& f = fasori[idx];

                // Rotazione e smorzamento Prompt
                double n_re_p = f.re_p * f.w_re_p - f.im_p * f.w_im_p;
                double n_im_p = f.re_p * f.w_im_p + f.im_p * f.w_re_p;
                f.re_p = n_re_p; f.im_p = n_im_p;

                // Rotazione e smorzamento Aftersound
                double n_re_a = f.re_a * f.w_re_a - f.im_a * f.w_im_a;
                double n_im_a = f.re_a * f.w_im_a + f.im_a * f.w_re_a;
                f.re_a = n_re_a; f.im_a = n_im_a;

                campioneTotale += n_re_p + (n_re_a * bloomingEnv);
            }
            bufferUscita[n] = static_cast<float>(campioneTotale);
        }

        // Potatura in O(1) delle parziali scese sotto -100 dB
        for (size_t idx = 0; idx < numFasoriAttivi; ) {
            const auto& f = fasori[idx];
            double pwr = (f.re_p * f.re_p + f.im_p * f.im_p) + (f.re_a * f.re_a + f.im_a * f.im_a);
            if (pwr < 1e-10) {
                fasori[idx] = fasori[numFasoriAttivi - 1];
                numFasoriAttivi--;
            } else {
                idx++;
            }
        }

        if (numFasoriAttivi == 0) break;
    }

    // --- Transiente meccanico anarmonico del martelletto: Knock (corpo legno) + Chiff (attrito feltro) ---
    // 1. Hammer Knock (colpo sordo di flessione ponticello a 110-150 Hz, decade in ~18 ms)
    size_t campioniKnock = std::min(totaleCampioni, static_cast<size_t>(0.018 * fs));
    double f_knock = 110.0 + 0.15 * static_cast<double>(f0);
    double omega_knock = 2.0 * M_PI * f_knock / fs;
    double decay_knock = std::exp(-160.0 / fs);
    double knock_amp = 0.035 * std::pow(v_norm, 1.5) * ampiezzaCorda;
    double knock_env = knock_amp;
    size_t campioniFadeKnock = static_cast<size_t>(0.004 * fs);

    for (size_t n = 0; n < campioniKnock; ++n) {
        // Finestra di raccordo dolce a zero negli ultimi 4 ms: zero micro-click
        double finestraKnock = 1.0;
        if (n + campioniFadeKnock >= campioniKnock) {
            double rest = static_cast<double>(campioniKnock - 1 - n) / static_cast<double>(campioniFadeKnock);
            finestraKnock = 0.5 * (1.0 - std::cos(rest * M_PI));
        }
        bufferUscita[n] += static_cast<float>(knock_env * finestraKnock * std::sin(static_cast<double>(n) * omega_knock));
        knock_env *= decay_knock;
    }

    // 2. Felt Chiff: compressione pura della lana (senza rumore bianco sintetico, solo nei registri medio-gravi)
    if (f0 < 600.0f) {
        size_t campioniChiff = std::min(totaleCampioni, static_cast<size_t>(0.0035 * fs));
        double f1_chiff = 2100.0 + 0.30 * static_cast<double>(f0);
        double f2_chiff = f1_chiff * 1.61803398875; // Rapporto aureo anarmonico: disperde energia e previene risonanze tonali
        double omega1 = 2.0 * M_PI * f1_chiff / fs;
        double omega2 = 2.0 * M_PI * f2_chiff / fs;
        double decay_chiff = std::exp(-1050.0 / fs);
        double chiff_amp = 0.011 * std::pow(v_norm, 1.6) * ampiezzaCorda;
        double chiff_env = chiff_amp;
        size_t campioniFadeChiff = static_cast<size_t>(0.001 * fs);

        for (size_t n = 0; n < campioniChiff; ++n) {
            double finestraChiff = 1.0;
            if (n + campioniFadeChiff >= campioniChiff) {
                double rest = static_cast<double>(campioniChiff - 1 - n) / static_cast<double>(campioniFadeChiff);
                finestraChiff = 0.5 * (1.0 - std::cos(rest * M_PI));
            }
            double s_anarmonico = 0.65 * std::sin(static_cast<double>(n) * omega1) + 
                                  0.35 * std::sin(static_cast<double>(n) * omega2 + 0.785398);
            double chiff_sample = chiff_env * finestraChiff * s_anarmonico;
            bufferUscita[n] += static_cast<float>(chiff_sample);
            chiff_env *= decay_chiff;
        }
    }

    // Rampa fisica ultra-rapida (0.4 ms): elimina click preservando il mordente percussivo reale
    size_t campioniRampa = std::min(totaleCampioni, static_cast<size_t>(0.0004 * fs));
    for (size_t n = 0; n < campioniRampa; ++n) {
        float f_rampa = static_cast<float>(n) / static_cast<float>(campioniRampa);
        float fattoreRampa = 0.5f * (1.0f - std::cos(f_rampa * static_cast<float>(M_PI)));
        bufferUscita[n] *= fattoreRampa;
    }
}
