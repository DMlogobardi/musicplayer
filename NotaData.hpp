#pragma once

#include <string>
#include <vector>

struct LayerVelocity {
    float soglia_velocity;              // Es. 0.25, 0.50, 0.75, 1.00
    std::vector<float> buffer_campioni; // Campioni pre-generati con lo spettro armonico reale
};

struct NotaData {
    std::string nome;                   // Es. "Do4", "Nota_10", ecc.
    float frequenza = 0.0f;
    float guadagno_L = 0.707f;        // Frequenza fondamentale (es. 261.63Hz)
    float guadagno_R = 0.707f;         // Frequenza fondamentale (es. 261.63Hz)
    std::vector<LayerVelocity> layers; // Array di numeri float con tutte le armoniche già calcolate
};