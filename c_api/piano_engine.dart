// =============================================================================
// piano_engine.dart — FLUTTER DART FFI BINDING PER IL MOTORE DI PIANOFORTE C++
// =============================================================================

import 'dart:ffi';
import 'dart:io';
import 'package:ffi/ffi.dart';

/// Eccezione sollevata quando il motore audio C++ segnala un errore
class PianoEngineException implements Exception {
  final String message;
  PianoEngineException(this.message);

  @override
  String toString() => 'PianoEngineException: $message';
}

// =============================================================================
// CARICAMENTO DINAMICO DELLA LIBRERIA NATIVA
// =============================================================================
DynamicLibrary _openPianoEngineLibrary([String? customPath]) {
  if (customPath != null && customPath.isNotEmpty) {
    return DynamicLibrary.open(customPath);
  }
  if (Platform.isWindows) {
    return DynamicLibrary.open('piano_engine.dll');
  } else if (Platform.isAndroid || Platform.isLinux) {
    return DynamicLibrary.open('libpiano_engine.so');
  } else if (Platform.isMacOS || Platform.isIOS) {
    return DynamicLibrary.process();
  }
  throw UnsupportedError('Piattaforma non supportata dal motore audio nativo.');
}

// =============================================================================
// BINDINGS FFI BASSO LIVELLO (Tipi C & Funzioni Native)
// =============================================================================
class _PianoEngineBindings {
  static _PianoEngineBindings? _instance;
  final DynamicLibrary dylib;

  // Gestione Errori
  late final Pointer<Utf8> Function() pianoGetLastError;
  late final void Function() pianoClearLastError;

  // Masterizzatore
  late final Pointer<Void> Function() masterizzatoreCreate;
  late final void Function(Pointer<Void>) masterizzatoreDestroy;
  late final Pointer<Void> Function(
    Pointer<Void>,
    Pointer<Utf8>,
    int,
    int,
    double,
  ) masterizzatoreMasterizza;

  // Dischetto
  late final void Function(Pointer<Void>) dischettoDestroy;
  late final int Function(Pointer<Void>) dischettoGetNumeroNote;
  late final int Function(Pointer<Void>) dischettoGetNotePerOttava;
  late final int Function(Pointer<Void>) dischettoGetIndiceAncora;
  late final int Function(Pointer<Void>) dischettoGetOttavePerLato;
  late final int Function(Pointer<Void>) dischettoGetSampleRate;
  late final Pointer<Utf8> Function(Pointer<Void>) dischettoGetNome;
  late final double Function(Pointer<Void>, int) dischettoGetNotaFrequenza;
  late final Pointer<Utf8> Function(Pointer<Void>, int) dischettoGetNotaNome;

  // HammerOrchestrator
  late final Pointer<Void> Function(Pointer<Void>) orchestratorCreate;
  late final void Function(Pointer<Void>) orchestratorDestroy;
  late final bool Function(Pointer<Void>, Pointer<Void>) orchestratorCambiaDischetto;
  late final void Function(Pointer<Void>, int, double) orchestratorNoteOn;
  late final void Function(Pointer<Void>, int, double) orchestratorNoteOff;
  late final void Function(Pointer<Void>, double) orchestratorSustainPedal;
  late final void Function(Pointer<Void>) orchestratorRilasciaTutto;
  late final void Function(Pointer<Void>, double) orchestratorSetGuadagnoMaster;
  late final double Function(Pointer<Void>) orchestratorGetGuadagnoMaster;
  late final int Function(Pointer<Void>) orchestratorGetVociAttive;
  late final int Function(Pointer<Void>) orchestratorGetNumeroVoci;
  late final bool Function(Pointer<Void>) orchestratorAvviaAudio;
  late final void Function(Pointer<Void>) orchestratorFermaAudio;
  late final bool Function(Pointer<Void>) orchestratorIsAudioAttivo;

  _PianoEngineBindings._(this.dylib) {
    // Gestione Errori
    pianoGetLastError = dylib
        .lookup<NativeFunction<Pointer<Utf8> Function()>>('piano_get_last_error')
        .asFunction();
    pianoClearLastError = dylib
        .lookup<NativeFunction<Void Function()>>('piano_clear_last_error')
        .asFunction();

    // Masterizzatore
    masterizzatoreCreate = dylib
        .lookup<NativeFunction<Pointer<Void> Function()>>('masterizzatore_create')
        .asFunction();
    masterizzatoreDestroy = dylib
        .lookup<NativeFunction<Void Function(Pointer<Void>)>>('masterizzatore_destroy')
        .asFunction();
    masterizzatoreMasterizza = dylib
        .lookup<
          NativeFunction<
            Pointer<Void> Function(
              Pointer<Void>,
              Pointer<Utf8>,
              Int32,
              Int32,
              Float,
            )
          >
        >('masterizzatore_masterizza')
        .asFunction();

    // Dischetto
    dischettoDestroy = dylib
        .lookup<NativeFunction<Void Function(Pointer<Void>)>>('dischetto_destroy')
        .asFunction();
    dischettoGetNumeroNote = dylib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>)>>('dischetto_get_numero_note')
        .asFunction();
    dischettoGetNotePerOttava = dylib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>)>>('dischetto_get_note_per_ottava')
        .asFunction();
    dischettoGetIndiceAncora = dylib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>)>>('dischetto_get_indice_ancora')
        .asFunction();
    dischettoGetOttavePerLato = dylib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>)>>('dischetto_get_ottave_per_lato')
        .asFunction();
    dischettoGetSampleRate = dylib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>)>>('dischetto_get_sample_rate')
        .asFunction();
    dischettoGetNome = dylib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>)>>('dischetto_get_nome')
        .asFunction();
    dischettoGetNotaFrequenza = dylib
        .lookup<NativeFunction<Float Function(Pointer<Void>, Int32)>>('dischetto_get_nota_frequenza')
        .asFunction();
    dischettoGetNotaNome = dylib
        .lookup<NativeFunction<Pointer<Utf8> Function(Pointer<Void>, Int32)>>('dischetto_get_nota_nome')
        .asFunction();

    // HammerOrchestrator
    orchestratorCreate = dylib
        .lookup<NativeFunction<Pointer<Void> Function(Pointer<Void>)>>('orchestrator_create')
        .asFunction();
    orchestratorDestroy = dylib
        .lookup<NativeFunction<Void Function(Pointer<Void>)>>('orchestrator_destroy')
        .asFunction();
    orchestratorCambiaDischetto = dylib
        .lookup<NativeFunction<Bool Function(Pointer<Void>, Pointer<Void>)>>('orchestrator_cambia_dischetto')
        .asFunction();
    orchestratorNoteOn = dylib
        .lookup<NativeFunction<Void Function(Pointer<Void>, Int32, Float)>>('orchestrator_note_on')
        .asFunction();
    orchestratorNoteOff = dylib
        .lookup<NativeFunction<Void Function(Pointer<Void>, Int32, Float)>>('orchestrator_note_off')
        .asFunction();
    orchestratorSustainPedal = dylib
        .lookup<NativeFunction<Void Function(Pointer<Void>, Float)>>('orchestrator_sustain_pedal')
        .asFunction();
    orchestratorRilasciaTutto = dylib
        .lookup<NativeFunction<Void Function(Pointer<Void>)>>('orchestrator_rilascia_tutto')
        .asFunction();
    orchestratorSetGuadagnoMaster = dylib
        .lookup<NativeFunction<Void Function(Pointer<Void>, Float)>>('orchestrator_set_guadagno_master')
        .asFunction();
    orchestratorGetGuadagnoMaster = dylib
        .lookup<NativeFunction<Float Function(Pointer<Void>)>>('orchestrator_get_guadagno_master')
        .asFunction();
    orchestratorGetVociAttive = dylib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>)>>('orchestrator_get_voci_attive')
        .asFunction();
    orchestratorGetNumeroVoci = dylib
        .lookup<NativeFunction<Int32 Function(Pointer<Void>)>>('orchestrator_get_numero_voci')
        .asFunction();
    orchestratorAvviaAudio = dylib
        .lookup<NativeFunction<Bool Function(Pointer<Void>)>>('orchestrator_avvia_audio')
        .asFunction();
    orchestratorFermaAudio = dylib
        .lookup<NativeFunction<Void Function(Pointer<Void>)>>('orchestrator_ferma_audio')
        .asFunction();
    orchestratorIsAudioAttivo = dylib
        .lookup<NativeFunction<Bool Function(Pointer<Void>)>>('orchestrator_is_audio_attivo')
        .asFunction();
  }

  static _PianoEngineBindings get instance {
    _instance ??= _PianoEngineBindings._(_openPianoEngineLibrary());
    return _instance!;
  }

  static void initLibrary([String? path]) {
    _instance = _PianoEngineBindings._(_openPianoEngineLibrary(path));
  }

  String checkAndGetLastError() {
    final ptr = pianoGetLastError();
    if (ptr == nullptr) return '';
    final msg = ptr.toDartString();
    pianoClearLastError();
    return msg;
  }
}

// =============================================================================
// CLASSE 1: MOTORE GENERATIVO OFFLINE (Masterizzatore)
// =============================================================================
class Masterizzatore {
  final _PianoEngineBindings _bindings;
  Pointer<Void>? _handle;

  Masterizzatore([String? libraryPath])
      : _bindings = _PianoEngineBindings.instance {
    if (libraryPath != null) {
      _PianoEngineBindings.initLibrary(libraryPath);
    }
    _handle = _bindings.masterizzatoreCreate();
    if (_handle == nullptr || _handle == null) {
      final err = _bindings.checkAndGetLastError();
      throw PianoEngineException('Impossibile creare il Masterizzatore: $err');
    }
  }

  /// Masterizza e genera il Dischetto in memoria per qualsiasi divisione N-EDO.
  Dischetto masterizza({
    String nome = 'Pianoforte Microtonale',
    int notePerOttava = 12,
    int ottavePerLato = 3,
    double frequenzaAncora = 261.6256,
  }) {
    _checkDisposed();
    final nomePtr = nome.toNativeUtf8();
    try {
      final discPtr = _bindings.masterizzatoreMasterizza(
        _handle!,
        nomePtr,
        notePerOttava,
        ottavePerLato,
        frequenzaAncora,
      );
      if (discPtr == nullptr) {
        final err = _bindings.checkAndGetLastError();
        throw PianoEngineException('Masterizzazione fallita: $err');
      }
      return Dischetto._(discPtr, _bindings);
    } finally {
      calloc.free(nomePtr);
    }
  }

  void _checkDisposed() {
    if (_handle == null || _handle == nullptr) {
      throw StateError('Masterizzatore già rilasciato (disposed).');
    }
  }

  /// Rilascia le risorse allocate dal Masterizzatore
  void dispose() {
    if (_handle != null && _handle != nullptr) {
      _bindings.masterizzatoreDestroy(_handle!);
      _handle = null;
    }
  }
}

// =============================================================================
// CLASSE 2: SOUNDBANK FISICA PRE-CALCOLATA (Dischetto)
// =============================================================================
class Dischetto {
  final _PianoEngineBindings _bindings;
  Pointer<Void>? _handle;

  Dischetto._(this._handle, this._bindings);

  /// Puntatore nativo per il passaggio verso C++
  Pointer<Void> get handle {
    _checkDisposed();
    return _handle!;
  }

  /// Numero totale di note fisiche generate nel Dischetto
  int get numeroNote {
    _checkDisposed();
    return _bindings.dischettoGetNumeroNote(_handle!);
  }

  /// Divisione dell'ottava N (note per ottava)
  int get notePerOttava {
    _checkDisposed();
    return _bindings.dischettoGetNotePerOttava(_handle!);
  }

  /// Indice del tasto centrale (frequenza di riferimento Do4)
  int get indiceAncora {
    _checkDisposed();
    return _bindings.dischettoGetIndiceAncora(_handle!);
  }

  /// Numero di ottave sopra e sotto il centro
  int get ottavePerLato {
    _checkDisposed();
    return _bindings.dischettoGetOttavePerLato(_handle!);
  }

  /// Sample Rate con cui è stato generato il Dischetto
  int get sampleRate {
    _checkDisposed();
    return _bindings.dischettoGetSampleRate(_handle!);
  }

  /// Nome identificativo del Dischetto
  String get nome {
    _checkDisposed();
    final ptr = _bindings.dischettoGetNome(_handle!);
    return ptr == nullptr ? '' : ptr.toDartString();
  }

  /// Restituisce la frequenza fondamentale f0 in Hz della nota all'indice specificato
  double getNotaFrequenza(int indice) {
    _checkDisposed();
    return _bindings.dischettoGetNotaFrequenza(_handle!, indice);
  }

  /// Restituisce il nome descrittivo della nota (es. "O0_G1", "Do4")
  String getNotaNome(int indice) {
    _checkDisposed();
    final ptr = _bindings.dischettoGetNotaNome(_handle!, indice);
    return ptr == nullptr ? '' : ptr.toDartString();
  }

  void _checkDisposed() {
    if (_handle == null || _handle == nullptr) {
      throw StateError('Dischetto già rilasciato (disposed).');
    }
  }

  /// Rilascia la memoria allocata dal Dischetto
  void dispose() {
    if (_handle != null && _handle != nullptr) {
      _bindings.dischettoDestroy(_handle!);
      _handle = null;
    }
  }
}

// =============================================================================
// CLASSE 3: MOTORE ESECUTIVO REAL-TIME (HammerOrchestrator)
// =============================================================================
class HammerOrchestrator {
  final _PianoEngineBindings _bindings;
  Pointer<Void>? _handle;

  /// Istanzia il motore audio con il Dischetto scelto e avvia l'uscita audio
  HammerOrchestrator(Dischetto dischetto)
      : _bindings = _PianoEngineBindings.instance {
    _handle = _bindings.orchestratorCreate(dischetto.handle);
    if (_handle == nullptr || _handle == null) {
      final err = _bindings.checkAndGetLastError();
      throw PianoEngineException('Impossibile istanziare HammerOrchestrator: $err');
    }
  }

  /// Cambia a caldo il Dischetto in riproduzione (cambio scala/accordatura istantaneo)
  bool cambiaDischetto(Dischetto nuovoDischetto) {
    _checkDisposed();
    final ok = _bindings.orchestratorCambiaDischetto(_handle!, nuovoDischetto.handle);
    if (!ok) {
      final err = _bindings.checkAndGetLastError();
      throw PianoEngineException('Cambio Dischetto fallito: $err');
    }
    return true;
  }

  /// Suona una nota (percussione martelletto-corda)
  /// [noteIndex]: indice del tasto [0 ... totaleNote-1]
  /// [velocity]: dinamica del tocco [0.01 ... 1.0]
  void noteOn(int noteIndex, [double velocity = 1.0]) {
    _checkDisposed();
    _bindings.orchestratorNoteOn(_handle!, noteIndex, velocity);
  }

  /// Rilascia una nota (smorzamento meccanico sulla corda)
  /// [noteIndex]: indice del tasto
  /// [releaseVelocity]: velocità di rilascio del tasto [0.05 ... 1.0]
  void noteOff(int noteIndex, [double releaseVelocity = 0.5]) {
    _checkDisposed();
    _bindings.orchestratorNoteOff(_handle!, noteIndex, releaseVelocity);
  }

  /// Controllo continuo del pedale di risonanza Sustain [0.0 ... 1.0]
  /// Supporta il mezzo pedale continuo, simulando la frizione parziale dei feltri
  void sustainPedal(double profondita) {
    _checkDisposed();
    _bindings.orchestratorSustainPedal(_handle!, profondita.clamp(0.0, 1.0));
  }

  /// Smorza immediatamente tutte le note in riproduzione (Panic button)
  void rilasciaTutto() {
    _checkDisposed();
    _bindings.orchestratorRilasciaTutto(_handle!);
  }

  /// Imposta il guadagno master del motore audio [0.0 ... 1.0]
  set guadagnoMaster(double value) {
    _checkDisposed();
    _bindings.orchestratorSetGuadagnoMaster(_handle!, value.clamp(0.0, 1.0));
  }

  /// Restituisce il guadagno master attuale
  double get guadagnoMaster {
    _checkDisposed();
    return _bindings.orchestratorGetGuadagnoMaster(_handle!);
  }

  /// Restituisce il numero di martelletti/voci attualmente attive
  int get vociAttive {
    _checkDisposed();
    return _bindings.orchestratorGetVociAttive(_handle!);
  }

  /// Limite massimo di polifonia simultanea (128)
  int get numeroVoci {
    _checkDisposed();
    return _bindings.orchestratorGetNumeroVoci(_handle!);
  }

  /// Avvia o riprende il flusso audio hardware
  bool avviaAudio() {
    _checkDisposed();
    return _bindings.orchestratorAvviaAudio(_handle!);
  }

  /// Sospende il flusso audio hardware
  void fermaAudio() {
    _checkDisposed();
    _bindings.orchestratorFermaAudio(_handle!);
  }

  /// Verifica se l'uscita audio è attiva
  bool get isAudioAttivo {
    _checkDisposed();
    return _bindings.orchestratorIsAudioAttivo(_handle!);
  }

  void _checkDisposed() {
    if (_handle == null || _handle == nullptr) {
      throw StateError('HammerOrchestrator già rilasciato (disposed).');
    }
  }

  /// Rilascia l'orchestratore e arresta la riproduzione
  void dispose() {
    if (_handle != null && _handle != nullptr) {
      _bindings.orchestratorDestroy(_handle!);
      _handle = null;
    }
  }
}
