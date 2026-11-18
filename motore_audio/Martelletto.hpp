#ifndef MARTELLETTO_HPP
#define MARTELLETTO_HPP

#include "NotaData.hpp"

class Martelletto {
public:
    // state of the hammer
    enum class StatoMartelletto {
        IDLE,             // A riposo, disponibile
        IN_PERCUSSIONE,   // Tasto premuto, in riproduzione
        IN_SUSTAIN,        // Tasto premuto, in riproduzione con smorzatore alzato
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
    float getGuadagnoL() const { return guadagno_L; }
    float getGuadagnoR() const { return guadagno_R; }
    size_t getIndiceNota() const { return indice_nota; }

    void setId(int newId);
    void setStato(StatoMartelletto nuovoStato) { stato = nuovoStato; }
    void configura(int sampleRate);

    // Check if the hammer is free (IDLE)
    bool eLibero() const;
    void reset();

    //azzioni del martelletto
    void colpisci(size_t idxNota, const NotaData* nota, float velocity = 1.0f);
    void rilascia(float releaseVelocity = 0.5f, float profonditaPedale = 0.0f);
    void smorzaDaRibattuto(float tauSecondi = 0.02f);
    bool isSmorzamentoRibattuto() const { return smorzamento_ribattuto; }
    float suona();

private:
    int id;
    size_t indice_nota = 0;
    const NotaData* nota_colpita;
    const std::vector<float>* buffer_layer_inf = nullptr;
    const std::vector<float>* buffer_layer_sup = nullptr;
    float peso_layer_inf = 1.0f;
    float peso_layer_sup = 0.0f;

    float guadagno_velocity;
    float inviluppo = 1.0f;
    StatoMartelletto stato;
    int sample_rate = 44100;
    float coef_rilascio = 0.999f;
    float guadagno_L = 0.707f;
    float guadagno_R = 0.707f;
    float livello = 0.0f;
    float coef_livello = 0.9996f;

    unsigned long long indice_lettura;
    unsigned long long lunghezza_fade = 220;

    float ultimo_campione_uscita = 0.0f;
    float valore_residuo_pop = 0.0f;
    unsigned long long campioni_declick = 0;
    unsigned long long durata_declick = 88;
    unsigned long long durata_attacco = 88;

    float filtro_smorzatore_stato = 0.0f;
    float alpha_smorzatore = 0.0f;
    bool smorzamento_ribattuto = false;
    bool attacco_da_ribattuto = false; // true solo se il Note-On interrompe una voce già attiva
};

#endif // MARTELLETTO_HPP