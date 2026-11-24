#ifndef PIANO_ENGINE_C_API_H
#define PIANO_ENGINE_C_API_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- MACRO DI ESPORTAZIONE SIMBOLI MULTI-PIATTAFORMA ---
#if defined(_WIN32)
    #if defined(BUILDING_PIANO_ENGINE_DLL)
        #define PIANO_API __declspec(dllexport)
    #else
        #define PIANO_API __declspec(dllimport)
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    #define PIANO_API __attribute__((visibility("default")))
#else
    #define PIANO_API
#endif

// =============================================================================
// HANDLE OPACHI PER GLI OGGETTI REALI DEL SISTEMA
// =============================================================================
typedef struct MasterizzatoreHandle_* MasterizzatoreHandle;
typedef struct DischettoHandle_* DischettoHandle;
typedef struct HammerOrchestratorHandle_* HammerOrchestratorHandle;

// =============================================================================
// 1. GESTIONE ERRORI ED ECCEZIONI C++ (Thread-Local Safe)
// =============================================================================
PIANO_API const char* piano_get_last_error(void);
PIANO_API void piano_clear_last_error(void);

// =============================================================================
// 2. MOTORE GENERATIVO OFFLINE (Masterizzatore)
// =============================================================================
/**
 * Crea l'istanza del Motore Generativo (Masterizzatore).
 * Collega internamente l'hardware audio sottostante per ottenere sampleRate reale e core CPU.
 */
PIANO_API MasterizzatoreHandle masterizzatore_create(void);

/**
 * Distrugge l'istanza del Masterizzatore.
 */
PIANO_API void masterizzatore_destroy(MasterizzatoreHandle handle);

/**
 * Genera il Dischetto fisico per una scala a N note per ottava.
 * @param nomeDisco       Nome identificativo del dischetto
 * @param notePerOttava   Numero di note per ottava (N-EDO)
 * @param ottavePerLato   Ottave sopra e sotto il centro (es. 3 -> 7 ottave)
 * @param frequenzaAncora Frequenza in Hz del tasto centrale (es. 261.6256f per Do4)
 * @return Handle al Dischetto generato in memoria, o NULL in caso di errore.
 */
PIANO_API DischettoHandle masterizzatore_masterizza(
    MasterizzatoreHandle handle,
    const char* nomeDisco,
    int32_t notePerOttava,
    int32_t ottavePerLato,
    float frequenzaAncora
);

// =============================================================================
// 3. SOUNDBANK FISICA PRE-CALCOLATA (Dischetto)
// =============================================================================
/**
 * Distrugge il Dischetto e rilascia la memoria fisica allocata.
 */
PIANO_API void dischetto_destroy(DischettoHandle handle);

/**
 * Restituisce il numero totale di note del Dischetto.
 */
PIANO_API int32_t dischetto_get_numero_note(DischettoHandle handle);

/**
 * Restituisce la divisione dell'ottava N (note per ottava).
 */
PIANO_API int32_t dischetto_get_note_per_ottava(DischettoHandle handle);

/**
 * Restituisce l'indice del tasto centrale di ancoraggio.
 */
PIANO_API int32_t dischetto_get_indice_ancora(DischettoHandle handle);

/**
 * Restituisce il numero di ottave per lato.
 */
PIANO_API int32_t dischetto_get_ottave_per_lato(DischettoHandle handle);

/**
 * Restituisce il Sample Rate con cui è stato generato il Dischetto.
 */
PIANO_API int32_t dischetto_get_sample_rate(DischettoHandle handle);

/**
 * Restituisce il nome del Dischetto.
 */
PIANO_API const char* dischetto_get_nome(DischettoHandle handle);

/**
 * Restituisce la frequenza fondamentale f0 in Hz della nota specificata.
 */
PIANO_API float dischetto_get_nota_frequenza(DischettoHandle handle, int32_t index);

/**
 * Restituisce il nome descrittivo della nota (es. "O0_G1", "Do4").
 */
PIANO_API const char* dischetto_get_nota_nome(DischettoHandle handle, int32_t index);

// =============================================================================
// 4. MOTORE ESECUTIVO REAL-TIME (HammerOrchestrator)
// =============================================================================
/**
 * Istanzia il motore esecutivo con il Dischetto scelto.
 * Inizializza 128 martelletti, risonanza per simpatia, ponticello e avvia l'uscita audio hardware.
 */
PIANO_API HammerOrchestratorHandle orchestrator_create(DischettoHandle dischettoHandle);

/**
 * Ferma l'audio e distrugge l'orchestratore.
 */
PIANO_API void orchestrator_destroy(HammerOrchestratorHandle handle);

/**
 * Cambia il Dischetto attivo in tempo reale a caldo in modo lock-free.
 */
PIANO_API bool orchestrator_cambia_dischetto(HammerOrchestratorHandle handle, DischettoHandle nuovoDischetto);

/**
 * Suona una nota (percussione martelletto-corda).
 * @param noteIndex Indice della nota nel dischetto [0 ... totaleNote-1]
 * @param velocity  Dinamica del tocco [0.01 ... 1.0]
 */
PIANO_API void orchestrator_note_on(HammerOrchestratorHandle handle, int32_t noteIndex, float velocity);

/**
 * Rilascia una nota (smorzamento meccanico sulla corda).
 * @param noteIndex       Indice della nota
 * @param releaseVelocity Velocità di stacco del dito [0.05 ... 1.0]
 */
PIANO_API void orchestrator_note_off(HammerOrchestratorHandle handle, int32_t noteIndex, float releaseVelocity);

/**
 * Modula la posizione continua del pedale di risonanza Sustain [0.0 ... 1.0].
 * Consente controllo continuo (mezzo pedale) e attiva rumore meccanico e risonanza simpatica.
 */
PIANO_API void orchestrator_sustain_pedal(HammerOrchestratorHandle handle, float depth);

/**
 * Smorza tutte le voci simultaneamente.
 */
PIANO_API void orchestrator_rilascia_tutto(HammerOrchestratorHandle handle);

/**
 * Imposta il guadagno master d'uscita [0.0 ... 1.0].
 */
PIANO_API void orchestrator_set_guadagno_master(HammerOrchestratorHandle handle, float gain);

/**
 * Legge il guadagno master d'uscita.
 */
PIANO_API float orchestrator_get_guadagno_master(HammerOrchestratorHandle handle);

/**
 * Restituisce il numero di voci/martelletti attualmente in vibrazione.
 */
PIANO_API int32_t orchestrator_get_voci_attive(HammerOrchestratorHandle handle);

/**
 * Restituisce il limite massimo di voci polifoniche simultanee (128).
 */
PIANO_API int32_t orchestrator_get_numero_voci(HammerOrchestratorHandle handle);

/**
 * Avvia il flusso audio hardware.
 */
PIANO_API bool orchestrator_avvia_audio(HammerOrchestratorHandle handle);

/**
 * Sospende il flusso audio hardware.
 */
PIANO_API void orchestrator_ferma_audio(HammerOrchestratorHandle handle);

/**
 * Verifica se il flusso audio è attualmente attivo.
 */
PIANO_API bool orchestrator_is_audio_attivo(HammerOrchestratorHandle handle);

#ifdef __cplusplus
}
#endif

#endif // PIANO_ENGINE_C_API_H
