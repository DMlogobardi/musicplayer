#ifndef MARTELLETTO_HPP
#define MARTELLETTO_HPP

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
    float getNota() const;
    float getVolume() const;
    StatoMartelletto getStato() const;

    void setId(int newId);
    void setNota(float newNota);
    void setVolume(float newVolume);
    void setStato(StatoMartelletto newStato);

    // Check if the hammer is free (IDLE)
    bool eLibero() const;

    //azzioni del martelletto
    void colpisci(float nota, float volumeIniziale = 1.0f);
    void rilascia();
    float suona(int sampleRate = 44100);

private:
    int id;
    float nota;
    float volume;
    StatoMartelletto stato;

    unsigned long long indice_lettura;
};

#endif // MARTELLETTO_HPP