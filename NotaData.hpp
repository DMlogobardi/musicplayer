#pragma once

#include <string>
#include <vector>

struct NotaData {
    std::string nome;                   // Es. "Do4", "Nota_10", ecc.
    float frequenza = 0.0f;             // Frequenza fondamentale (es. 261.63Hz)
    std::vector<float> buffer_campioni; // Array di numeri float con tutte le armoniche già calcolate
};