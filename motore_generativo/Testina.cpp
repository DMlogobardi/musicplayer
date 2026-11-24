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

        // B. Trasformata dell'impulso martelletto con Isteresi Viscoelastica del Feltro (Stulov / Suzuki)
        double x_tc = 2.0 * freqParziale * tc;
        double denom_tc = 1.0 - x_tc * x_tc;
        double h_hammer;
        if (std::abs(denom_tc) > 0.06) {
            h_hammer = std::abs(std::cos(M_PI * freqParziale * tc) / denom_tc);
        } else {
            h_hammer = M_PI * 0.25; // Limite continuo per x_tc -> 1
        }
        // Termine di asimmetria isteretica del feltro: rinforza il mordente percussivo a velocity elevate
        double eta_hyst = 0.40 * std::pow(v_norm, 0.35);
        double boost_isteresi = 1.0 + eta_hyst * (x_tc / (1.0 + x_tc));
        h_hammer = std::pow(std::clamp(h_hammer * boost_isteresi, 0.001, 1.5), 0.85);

        // C. Spettro armonico integrato con termine non lineare di tensione (Phantom Partials k=2..6)
        double ampiezzaK = (1.0 / static_cast<double>(k)) * ampiezzaSpaziale * h_hammer;

        // Rinforzo non lineare armonico da auto-modulazione di tensione (Conklin / Bilbao)
        if (k >= 2 && k <= 6) {
            double phantom_coupling = 0.18 * std::pow(v_norm, 1.8) * (150.0 / (static_cast<double>(f0) + 120.0));
            ampiezzaK += phantom_coupling * ampiezzaSpaziale / static_cast<double>(k);
        }

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

            // Dispersione di fase elastica d'impatto (Chaigne-Askenfelt):
            // La fondamentale k=1 parte con spostamento nullo y(0)=0.
            // Le armoniche superiori curvano dolcemente la fase iniziale per dispersione d'onda flessionale:
            double theta_k = (k == 1) ? 0.0 : (0.35 * std::tanh(0.28 * static_cast<double>(k - 1)));
            double cos_th = std::cos(theta_k);
            double sin_th = std::sin(theta_k);

            f.re_p = -ampPrompt * polaritaModale * sin_th;
            f.im_p = -ampPrompt * polaritaModale * cos_th;

            // Modo ortogonale aftersound: orientato con leggera rotazione di polarizzazione
            f.re_a = -ampAfter * polaritaModale * sin_th;
            f.im_a = -ampAfter * polaritaModale * cos_th;

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

    // 2. Felt Chiff: attrito dinamico della lana e transiente percussivo (adattivo su tutti i registri)
    {
        double f1_chiff, decay_rate, chiff_amp;
        size_t durata_samples;
        if (f0 < 600.0f) {
            durata_samples = static_cast<size_t>(0.0035 * fs);
            f1_chiff = 2100.0 + 0.30 * static_cast<double>(f0);
            decay_rate = 1050.0;
            chiff_amp = 0.011 * std::pow(v_norm, 1.6) * ampiezzaCorda;
        } else {
            // Negli acuti il feltro è sottilissimo e compresso: transiente percussivo secco e cristallino
            durata_samples = static_cast<size_t>(0.0015 * fs);
            f1_chiff = std::clamp(2800.0 + 1.10 * static_cast<double>(f0), 3500.0, 5800.0);
            decay_rate = 2600.0; // Decade in ~1.2 ms
            chiff_amp = 0.018 * std::pow(v_norm, 1.35) * ampiezzaCorda;
        }

        size_t campioniChiff = std::min(totaleCampioni, durata_samples);
        double f2_chiff = f1_chiff * 1.61803398875; // Rapporto aureo anarmonico
        double omega1 = 2.0 * M_PI * f1_chiff / fs;
        double omega2 = 2.0 * M_PI * f2_chiff / fs;
        double decay_chiff = std::exp(-decay_rate / fs);
        double chiff_env = chiff_amp;
        size_t campioniFadeChiff = static_cast<size_t>(0.0006 * fs);

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

    // 3. Modi Longitudinali delle corde avvolte (Piano Growl / Ringhio metallico nobile nei bassi f0 < 350 Hz)
    // Conklin (1996) / Giordano: onde di compressione longitudinali nel filo d'acciaio/rame
    if (f0 < 350.0f) {
        double f_long = static_cast<double>(f0) * (12.0 + 8.0 * (100.0 / (static_cast<double>(f0) + 60.0)));
        f_long = std::clamp(f_long, 450.0, 2100.0);
        double omega_long = 2.0 * M_PI * f_long / fs;
        double omega_long2 = omega_long * 1.88;
        double decay_long = std::exp(-28.0 / fs); // Decade naturalmente in ~70-90 ms
        size_t campioniLong = std::min(totaleCampioni, static_cast<size_t>(0.085 * fs));
        double pesoRegistroGrave = std::clamp((350.0 - static_cast<double>(f0)) / 280.0, 0.0, 1.0);
        double long_amp = 0.022 * std::pow(v_norm, 1.85) * ampiezzaCorda * pesoRegistroGrave;
        double long_env = long_amp;
        size_t campioniFadeLong = static_cast<size_t>(0.015 * fs);

        for (size_t n = 0; n < campioniLong; ++n) {
            double finestraLong = 1.0;
            if (n + campioniFadeLong >= campioniLong) {
                double rest = static_cast<double>(campioniLong - 1 - n) / static_cast<double>(campioniFadeLong);
                finestraLong = 0.5 * (1.0 - std::cos(rest * M_PI));
            }
            double s_long = 0.70 * std::sin(static_cast<double>(n) * omega_long) + 
                            0.30 * std::sin(static_cast<double>(n) * omega_long2);
            bufferUscita[n] += static_cast<float>(long_env * finestraLong * s_long);
            long_env *= decay_long;
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
