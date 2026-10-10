#pragma once
#include <cstddef>

enum class TipoEventoAudio {
    NOTE_ON,
    NOTE_OFF,
    RILASCIA_TUTTO
};

struct EventoAudio {
    TipoEventoAudio tipo = TipoEventoAudio::NOTE_ON;
    size_t indiceNota = 0;
    float velocity = 0.0f;
};