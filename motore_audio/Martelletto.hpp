#ifndef MARTELLETTO_HPP
#define MARTELLETTO_HPP

#include "NotaData.hpp"

class Martelletto {
public:
    // state of the hammer
    enum class StatoMartelletto {
        IDLE,             // A riposo, disponibile
        IN_PERCUSSIONE,   // Tasto premuto, in riproduzione
        IN_SMORZAMENTO    // Tasto rilasciato, lo smorzatore cala sulla corda
    };
    // Constructor
    Martelletto();

    // Getters and Setters
    int getId() const;
    float getGuadagno_velocity() const;
    float getInviluppo() const;
    float getVolume() const;
    StatoMartelletto getStato() const;
    const NotaData* getNota() const;

    void setId(int newId);
    void configura(int sampleRate);

    // Check if the hammer is free (IDLE)
    bool eLibero() const;

    //azzioni del martelletto
    void colpisci(const NotaData* nota, float velocity = 1.0f);
    void rilascia(float tauSecondi = 0.08f);
    float suona();

private:
    int id;
    const NotaData* nota_colpita;
    float guadagno_velocity;
    float inviluppo = 1.0f;
    StatoMartelletto stato;
    int sample_rate = 44100;
    float coef_rilascio = 0.999f;

    unsigned long long indice_lettura;
    unsigned long long lunghezza_fade = 220;

    float ultimo_campione_uscita = 0.0f;
    float valore_residuo_pop = 0.0f;
    unsigned long long campioni_declick = 0;
    unsigned long long durata_declick = 88;
};

#endif // MARTELLETTO_HPP