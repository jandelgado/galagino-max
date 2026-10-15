#include <math.h>
#include "phoenix_sound.h"

// ============================================================================
// PHOENIX discrete audio @ 24 kHz mono — porting FEDELE da MAME
// src/mame/phoenix/phoenix_a.cpp (phoenix_sound_device + phoenix_discrete netlist).
// Ogni stadio replica la fisica RC/555 esatta del circuito reale (non uno square
// wave approssimato). Sostituisce la precedente approssimazione "Galaxian-style".
//
// Mappa registri (raw latch, settati da Phoenix::wrZ80):
//   soundregs[0] = sound A (0x6000): bit0-3=PHOENIX_EFFECT_2_DATA, bit4-5=EFFECT_2_FREQ,
//                  bit6-7=noise gen (C24/C25 charge/discharge, "future effect 3/4")
//   soundregs[1] = sound B (0x6800): bit0-3=PHOENIX_EFFECT_1_DATA, bit4=EFFECT_1_FREQ,
//                  bit5=EFFECT_1_FILT, bit6-7=melody tune select (MM6221AA)
//
// Catena Effect 1 (comment originale: "shield, bird explode, level 3&4 siren,
// level 5 spaceship"): NODE_20 RCDISC4 (inviluppo pitch pilotato da FREQ bit) ->
// NODE_21 555 CV -> NODE_22 NOTE (LS163+preload DATA) -> switch/filtro (FILT bit).
//
// Catena Effect 2 (comment originale: "bird flying, bird/phoenix/spaceship hit,
// phoenix wing hit"): NODE_30 selezione capacita' (FREQ 2-bit) -> NODE_33/34 due
// 555 fissi -> mixer resistivo -> NODE_37 filtro lento (~0.3s) -> NODE_38 mixer ->
// NODE_39 555 CV -> NODE_40 NOTE (preload DATA) -> * livello ampiezza (FREQ bit alto).
//
// Rumore (bit 6-7 sound A, MAI usato dall'approssimazione precedente): NE555 con
// doppio inviluppo RC (C24 ~0.136s discharge, C25 ~0.32s discharge) che modula la
// frequenza di un LFSR 18-bit tra 588-6325 Hz (porting diretto di noise()/update_c24/
// update_c25 da phoenix_a.cpp, invariato).
//
// Melody (MM6221AA, bit 6-7 sound B): approssimazione mantenuta da SPINNERINO (4 tune
// brevi, chip melodia reale non disponibile come sorgente separato).
//
// NOTE: alcune costanti MAME non verificabili da qui (DEFAULT_TTL_V_LOGIC_1=3.4,
// OP_AMP_VP_RAIL_OFFSET=1.5) sono valori standard assunti: influenzano solo il
// bilanciamento fine dei livelli, non la frequenza/tempistica (che e' esatta).
// Guadagno finale PHOENIX_MASTER_SCALE tarabile se il volume risultasse squilibrato.
//
// PERFORMANCE (fix task watchdog su HW): tutta la catena e' in float con gli
// esponenziali RC del passo pieno PRECALCOLATI (le combinazioni R/C sono note a
// priori). L'ESP32 ha la FPU solo single-precision: la prima stesura in double
// (exp/log software, ~migliaia di cicli l'una, ~10 per sample a 24kHz) superava
// il budget dell'intero core -> transmit() non riempiva mai il DMA piu' veloce
// dell'I2S, loopTask restava nel do/while e scattava il task watchdog (reboot).
// exp/log runtime sopravvivono solo nella correzione sub-sample dei crossing
// (~1-3 per sample, ph_neg_log1m()) e nel raro fallback di ph_exp1m.
// ============================================================================

// Parametri precalcolati di un 555 astabile (R/C fissi per ogni istanza).
struct ph555Params {
  float exp_charge;        // 1-exp(-dt/tau) per il passo pieno da 1/24000s
  float exp_discharge;
  float tau_charge;        // (r1+r2)*c
  float tau_discharge;     // r2*c
  float inv_tau_charge;
  float inv_tau_discharge;
};

static ph555Params ph_make_555(float r1, float r2, float c) {
  const float dt = 1.0f / 24000.0f;
  ph555Params p;
  p.tau_charge        = (r1 + r2) * c;
  p.tau_discharge     = r2 * c;
  p.inv_tau_charge    = 1.0f / p.tau_charge;
  p.inv_tau_discharge = 1.0f / p.tau_discharge;
  p.exp_charge        = 1.0f - expf(-dt * p.inv_tau_charge);
  p.exp_discharge     = 1.0f - expf(-dt * p.inv_tau_discharge);
  return p;
}

// 1-exp(-x): serie troncata al 5° ordine per x<=1 (errore <0.1%), usata per il
// tempo residuo dopo un crossing (dt < passo pieno). Fallback expf per x>1
// (solo il 555 CV di Effect2 in scarica puo' arrivarci, tau < dt_full).
static inline float ph_exp1m(float x) {
  if (x > 1.0f) return 1.0f - expf(-x);
  return x * (1.0f - 0.5f*x*(1.0f - 0.333333f*x*(1.0f - 0.25f*x*(1.0f - 0.2f*x))));
}

// -log(1-f) for the crossing time of the 555s below, f in [0, 0.999].
// They cross on most samples, newlib logf() there cost more than the rest
// of the netlist. ln(x) = e*ln2 + 2*atanh((m-1)/(m+1)), m in [1,2):
// absolute error < 2e-6.
static inline float ph_neg_log1m(float f) {
  union { float f; uint32_t i; } u = { 1.0f - f };
  const int e = (int)((u.i >> 23) & 0xff) - 127;
  u.i = (u.i & 0x007fffff) | 0x3f800000;
  const float t = (u.f - 1.0f) / (u.f + 1.0f);
  const float t2 = t * t;
  const float ln_m = 2.0f * t * (1.0f + t2 * (1.0f/3 + t2 * (1.0f/5 + t2 * (1.0f/7 + t2 * (1.0f/9)))));
  return -(e * 0.69314718f + ln_m);
}

// -- 555 astabile (dsd_555_astbl): RC charge/discharge esatto con correzione
// dell'overshoot in tempo continuo. ctrlv<0 => soglie fisse (v_pos*2/3, v_pos/3);
// ctrlv>=0 => CV-modulato (threshold=ctrlv, trigger=ctrlv/2).
// energy_mode=false -> ritorna count_f+x_time (DISC_555_OUT_COUNT_F_X, per NOTE);
// energy_mode=true  -> ritorna v_out_high*(duty) (DISC_555_OUT_ENERGY, per i mixer).
static float ph_555_step(float &cap_v, uint8_t &ff, const ph555Params &p,
                         float v_pos, float v_charge, float ctrlv,
                         bool energy_mode, float v_out_high) {
  const float dt_full = 1.0f / 24000.0f;
  if (ctrlv >= 0 && ctrlv < 0.25f)
    return energy_mode ? (ff ? 0.0f : v_out_high) : 0.0f;

  float threshold = (ctrlv >= 0) ? ctrlv          : (v_pos * (2.0f / 3.0f));
  float trigger   = (ctrlv >= 0) ? (0.5f * ctrlv) : (v_pos * (1.0f / 3.0f));
  int count_f = 0;

  if (ctrlv >= 0) {
    if (cap_v >= threshold)      { ff = 0; count_f++; }
    else if (cap_v <= trigger)   { ff = 1; }
  }

  float dt     = dt_full;
  float v_cap  = cap_v;
  float x_time = 0;
  bool  full_step = true;   // primo giro: esponente precalcolato del passo pieno
  int   guard = 8;          // rete di sicurezza: mai piu' di 8 crossing per sample

  do {
    if (ff) {
      float exponent   = full_step ? p.exp_charge : ph_exp1m(dt * p.inv_tau_charge);
      float v_cap_next = v_cap + (v_charge - v_cap) * exponent;
      dt = 0;
      if (v_cap_next >= threshold) {
        float denom = v_charge - v_cap;
        if (denom > 1e-9f) {
          float f = (v_cap_next - threshold) / denom;
          if (f > 0.999f) f = 0.999f;
          dt = p.tau_charge * ph_neg_log1m(f);
        }
        x_time = dt;
        v_cap_next = threshold;
        ff = 0;
        count_f++;
      }
      v_cap = v_cap_next;
    } else {
      float exponent   = full_step ? p.exp_discharge : ph_exp1m(dt * p.inv_tau_discharge);
      float v_cap_next = v_cap - v_cap * exponent;
      dt = 0;
      if (v_cap_next <= trigger) {
        if (v_cap_next < trigger && v_cap > 1e-9f) {
          float f = (trigger - v_cap_next) / v_cap;
          if (f > 0.999f) f = 0.999f;
          dt = p.tau_discharge * ph_neg_log1m(f);
        }
        x_time = dt;
        v_cap_next = trigger;
        ff = 1;
      }
      v_cap = v_cap_next;
    }
    full_step = false;
  } while (dt > 0 && --guard);

  cap_v = v_cap;
  float x_frac = x_time * 24000.0f;   // = x_time / dt_full

  if (energy_mode) {
    float xt = (x_frac == 0) ? 1.0f : x_frac;
    return v_out_high * (ff ? xt : (1.0f - xt));
  }
  return count_f ? (count_f + x_frac) : 0.0f;
}

// -- NOTE generator (dss_note, DISC_CLK_BY_COUNT | DISC_OUT_IS_ENERGY): contatore
// LS163 che conta da 'data' (preload) a 15 poi ribalta count2 (0/1) -- la nota
// suonata e' il toggle di count2. clock_in = output combinato del 555 (count_f+x_time).
static float ph_note_step(int &count1, int &count2, float clock_in, int data) {
  int   clock  = (int)clock_in;
  float x_time = clock_in - clock;
  int   last_count2 = count2;

  if (data != 15) {
    for (int k = 0; k < clock; k++) {
      count1++;
      if (count1 > 15) { count1 = data; count2 += 1; if (count2 > 1) count2 = 0; }
    }
  }

  float v_out = count2;
  if (count2 != last_count2) {
    if (x_time == 0) x_time = 1.0f;
    v_out = last_count2;
    if (count2 > last_count2) v_out += (count2 - last_count2) * x_time;
    else                      v_out -= (last_count2 - count2) * x_time;
  }
  return v_out;
}

// -- RCDISC4 tipo 1 (NODE_20): inviluppo di tensione per il pitch-bend di Effect 1,
// pilotato dal bit FREQ. Costanti precalcolate da R22=470,R23=100k,R24=33k,C7=6.8uF,VP=12V.
static float ph_rcdisc4_step(float &vc1, int freq_bit) {
  static const float dt   = 1.0f / 24000.0f;
  static const float V    = 12.0f - 0.5f;                                    // VP - diode drop
  static const float r1p3 = (470.0f * 33000.0f) / (470.0f + 33000.0f);       // R1||R3
  static const float rT1  = 100000.0f + r1p3;                                // R2+r
  static const float m_v1 = (V / rT1) * r1p3 + 0.5f;
  static const float rT1b = (100000.0f * r1p3) / (100000.0f + r1p3);         // R2||r
  static const float exp1 = 1.0f - expf(-dt / (rT1b * 6.8e-6f));
  static const float rT0  = 100000.0f + 33000.0f;                            // R2+R3
  static const float m_v0 = (V / rT0) * 33000.0f + 0.5f;
  static const float rT0b = (100000.0f * 33000.0f) / (100000.0f + 33000.0f); // R2||R3
  static const float exp0 = 1.0f - expf(-dt / (rT0b * 6.8e-6f));

  float target   = freq_bit ? m_v1 : m_v0;
  float exponent = freq_bit ? exp1 : exp0;
  vc1 += (target - vc1) * exponent;
  if (vc1 > 10.5f) vc1 = 10.5f;   // max_out = VP(12) - OP_AMP_VP_RAIL_OFFSET(1.5)
  if (vc1 < 0)     vc1 = 0;
  return vc1;
}

// -- RC filter a un polo (dst_rcfilter): v += (vin-v)*exponent, exponent precalcolato dal chiamante.
static inline float ph_rcfilter_step(float &v_out, float vin, float exponent) {
  v_out += (vin - v_out) * exponent;
  return v_out;
}

// -- Noise generator NE555 + doppio inviluppo RC (porting diretto di update_c24/
// update_c25/noise da phoenix_a.cpp, invariato). Pilotato da bit 6/7 del sound A latch.
// Rates as float reciprocals: double math is software on the ESP32, two
// double divisions per sample cost more than the rest of the netlist.
static int32_t ph_update_c24(int32_t &level, int32_t &counter, bool bit40) {
  static const float C24 = 6.8e-6f, R49 = 1000, R51 = 330, R52 = 20000;
  static const float RATE_DOWN = 1.0f / (R52 * C24);
  static const float RATE_UP = 1.0f / ((R51 + R49) * C24);
  if (bit40) {
    if (level > 0) {
      counter -= (int32_t)((level - 0) * RATE_DOWN);
      if (counter <= 0) {
        int32_t n = -counter / 24000 + 1;
        counter += n * 24000;
        level -= n; if (level < 0) level = 0;
      }
    }
  } else {
    if (level < 32767) {
      counter -= (int32_t)((32767 - level) * RATE_UP);
      if (counter <= 0) {
        int32_t n = -counter / 24000 + 1;
        counter += n * 24000;
        level += n; if (level > 32767) level = 32767;
      }
    }
  }
  return 32767 - level;
}

static int32_t ph_update_c25(int32_t &level, int32_t &counter, bool bit80) {
  static const float C25 = 6.8e-6f, R50 = 1000, R53 = 330, R54 = 47000;
  static const float RATE_UP = 1.0f / ((R50 + R53) * C25);
  static const float RATE_DOWN = 1.0f / (R54 * C25);
  if (bit80) {
    if (level < 32767) {
      counter -= (int32_t)((32767 - level) * RATE_UP);
      if (counter <= 0) {
        int32_t n = -counter / 24000 + 1;
        counter += n * 24000;
        level += n; if (level > 32767) level = 32767;
      }
    }
  } else {
    if (level > 0) {
      counter -= (int32_t)((level - 0) * RATE_DOWN);
      if (counter <= 0) {
        int32_t n = -counter / 24000 + 1;
        counter += n * 24000;
        level -= n; if (level < 0) level = 0;
      }
    }
  }
  return level;
}

void PhoenixSound::render(const uint8_t *regs, int16_t *out, int n) {
  static const uint16_t TUNE_ROMANCE[] = {
    440, 0, 440, 0, 440, 0,        // A4 A4 A4
    440, 0, 392, 0, 349, 0,        // A4 G4 F4
    349, 0, 330, 0, 294, 0,        // F4 E4 D4
    294, 0, 349, 0, 440, 0,        // D4 F4 A4
    587, 0,   0, 0,   0, 0,        // D5 [sustain lungo]
    587, 0, 523, 0, 466, 0,        // D5 C5 Bb4
    466, 0, 440, 0, 392, 0,        // Bb4 A4 G4
    392, 0, 440, 0, 466, 0,        // G4 A4 Bb4
    440, 0, 466, 0, 440, 0,        // A4 Bb4 A4
    554, 0, 466, 0, 440, 0,        // C#5 Bb4 A4
    440, 0, 392, 0, 349, 0,        // A4 G4 F4
    349, 0, 330, 0, 294, 0,        // F4 E4 D4
    330, 0, 330, 0, 330, 0,        // E4 E4 E4
    330, 0, 349, 0, 330, 0,        // E4 F4 E4
    294, 0, 349, 0, 440, 0,        // D4 F4 A4
    587, 0,   0, 0,   0, 0,        // D5 [sustain finale]
    0xFFFF
  };
  // TUNE_ROMANCE dura 96 voci * 0.2s = 19.2s e (confermato dall'utente) suona per
  // intero senza tagli al livello 1 — corretto per confronto con MAME. FUR_ELISE e
  // WARNING erano invece placeholder brevi (~1.6-1.8s, singola ripetizione del
  // motivo) che si interrompevano molto prima della melodia reale: il motivo viene
  // ripetuto qui per raggiungere una durata paragonabile (~18-19s), NON in loop a
  // runtime (nessuna ripetizione oltre il terminatore 0xFFFF, e' un unico array
  // piu' lungo scritto per intero, coerente con "un brano lungo suonato una volta").
  static const uint16_t TUNE_FUR_ELISE[] = {
    659, 622, 659, 622, 659, 494, 587, 523, 440,   // ripetuto 10x = 90 voci = 18.0s
    659, 622, 659, 622, 659, 494, 587, 523, 440,
    659, 622, 659, 622, 659, 494, 587, 523, 440,
    659, 622, 659, 622, 659, 494, 587, 523, 440,
    659, 622, 659, 622, 659, 494, 587, 523, 440,
    659, 622, 659, 622, 659, 494, 587, 523, 440,
    659, 622, 659, 622, 659, 494, 587, 523, 440,
    659, 622, 659, 622, 659, 494, 587, 523, 440,
    659, 622, 659, 622, 659, 494, 587, 523, 440,
    659, 622, 659, 622, 659, 494, 587, 523, 440,
    0xFFFF
  };
  static const uint16_t TUNE_WARNING[] = {
    523, 784, 523, 784, 523, 784, 523, 784,        // ripetuto 12x = 96 voci = 19.2s
    523, 784, 523, 784, 523, 784, 523, 784,
    523, 784, 523, 784, 523, 784, 523, 784,
    523, 784, 523, 784, 523, 784, 523, 784,
    523, 784, 523, 784, 523, 784, 523, 784,
    523, 784, 523, 784, 523, 784, 523, 784,
    523, 784, 523, 784, 523, 784, 523, 784,
    523, 784, 523, 784, 523, 784, 523, 784,
    523, 784, 523, 784, 523, 784, 523, 784,
    523, 784, 523, 784, 523, 784, 523, 784,
    523, 784, 523, 784, 523, 784, 523, 784,
    523, 784, 523, 784, 523, 784, 523, 784,
    0xFFFF
  };
  static const uint16_t* TUNES[4] = {
    TUNE_ROMANCE, TUNE_WARNING, TUNE_FUR_ELISE, TUNE_ROMANCE
  };

  // Guadagno finale: catena analogica reale ~x40000 (gain del mixer MAME) * 0.6
  // (route gain del device "discrete" nel machine_config) rescalato /64 per il
  // contratto +/-512 di valueToBuffer(). Unico numero da ritarare se serve.
  const float PHOENIX_MASTER_SCALE = 40000.0f * 0.6f / 64.0f;

  uint8_t a = regs[0];   // sound A raw latch (0x6000)
  uint8_t b = regs[1];   // sound B raw latch (0x6800)

  // Melody trigger: MAME chiama mm6221aa_tune_w(data>>6) ad OGNI scrittura di
  // control_b_w, ma quello stesso latch porta anche Effect1 (bit0-5), riscritto
  // di continuo durante il gioco (es. esplosione navetta) — verificato che
  // questo produce letture transitorie con bit6-7=0 non legate alla melodia
  // (il gioco NON mantiene sempre una shadow-copy coerente). Il chip MM6221AA
  // reale suona il brano scelto UNA VOLA SOLA fino alla fine (confermato: non
  // va in loop, ~10s+, vedi TUNE_ROMANCE) e non si riavvia/interrompe per una
  // fluttuazione di un frame su un latch condiviso. Quindi:
  //  - (ri)parte da capo SOLO se il valore non-zero richiesto e' DIVERSO dalla
  //    tune ATTUALMENTE IN RIPRODUZIONE (non dall'ultimo valore letto: una
  //    rilettura dello stesso numero, anche dopo uno zero spurio, non tocca
  //    l'indice e lascia proseguire la riproduzione in corso);
  //  - un valore 0 non ferma mai nulla: la melodia si ferma SOLO da sola al
  //    proprio terminatore 0xFFFF (gestito piu' sotto).
  uint8_t new_tune = (b >> 6) & 0x03;
  if (new_tune != 0 && (!mel_active || mel_tune != new_tune)) {
    mel_tune = new_tune; mel_idx = 0; mel_timer = 1; mel_active = true;
  }

  int   e1_data = b & 0x0F;
  int   e1_freq = (b >> 4) & 0x01;
  int   e1_filt = (b >> 5) & 0x01;
  int   e2_data = a & 0x0F;
  int   e2_freq = (a >> 4) & 0x03;
  bool  noise_b40 = (a & 0x40) != 0;
  bool  noise_b80 = (a & 0x80) != 0;

  // NODE_30: capacita' selezionata per Effect2 (COMP_ADDER, cDefault=C18=0.01uF)
  float node30_c = 0.01e-6f;
  if (e2_freq & 1) node30_c += 0.47e-6f;   // C16
  if (e2_freq & 2) node30_c += 1.0e-6f;    // C17
  // NODE_31/32: bit alto di FREQ -> livello ampiezza Effect2 (SWITCH)
  float node32_level = (e2_freq & 2) ? (3.4f / 2.0f) : 3.4f;

  // Parametri 555 precalcolati. e1/e2b/e2cv hanno R/C fissi -> calcolati una volta
  // sola (static). e2a dipende da node30_c (cambia con e2_freq) -> ricalcolato ad
  // ogni chiamata (poche volte al secondo, non per-sample).
  static const ph555Params p_e1_555   = ph_make_555(47000.0f, 47000.0f, 1e-9f);
  static const ph555Params p_e2_555b  = ph_make_555(510000.0f, 510000.0f, 1e-6f);
  static const ph555Params p_e2_555cv = ph_make_555(20000.0f, 20000.0f, 1e-9f);
  const ph555Params p_e2_555a = ph_make_555(47000.0f, 100000.0f, node30_c);

  // Precalcolati (fissi): RCFILTER NODE_25 (Effect1 filtro), NODE_37 (Effect2 lento),
  // e i 3 canali del mixer finale (DC-block).
  static const float dt = 1.0f / 24000.0f;
  static const float exp_node25 = 1.0f - expf(-dt / ((1.0f/(1.0f/10000.0f+1.0f/100000.0f)) * 0.047e-6f));
  static const float exp_node37 = 1.0f - expf(-dt / (3051.98f * 100e-6f));
  static const float exp_mix1   = 1.0f - expf(-dt / (8507.46f * 10e-6f));   // canale Effect1
  static const float exp_mix2   = 1.0f - expf(-dt / (7500.0f  * 10e-6f));   // canale Effect2
  static const float exp_camp   = 1.0f - expf(-dt / (6628.0f  * 10e-6f));   // cAmp finale

  // Millman denominators as reciprocals: no hardware float division
  static const float MILLMAN35 = 1.0f / (1.0f/10000.0f + 1.0f/10200.0f + 1.0f/5000.0f);
  static const float MILLMAN38 = 1.0f / (1.0f/10000.0f + 1.0f/5100.0f + 1.0f/5000.0f);

  for (int i = 0; i < n; i++) {
    // ── Rumore NE555 (bit 6-7 sound A) ──
    int32_t vc24 = ph_update_c24(c24_level, c24_counter, noise_b40);
    int32_t vc25 = ph_update_c25(c25_level, c25_counter, noise_b80);
    int32_t noise_level = (vc24 < vc25) ? (vc24 + (vc25 - vc24) / 2) : (vc25 + (vc24 - vc25) / 2);
    int32_t noise_freq  = 588 + 6325 * noise_level / 32768;
    noise_counter -= noise_freq;
    if (noise_counter <= 0) {
      int32_t n = (-noise_counter / 24000) + 1;
      noise_counter += n * 24000;
      for (int32_t k = 0; k < n; k++) {
        int fb = (((noise_shiftreg >> 16) & 1) == ((noise_shiftreg >> 17) & 1)) ? 1 : 0;
        noise_shiftreg = (noise_shiftreg << 1) | fb;
      }
      noise_polybit = noise_shiftreg & 1;
    }
    int32_t noise_sum = 0;
    if (!noise_polybit) noise_sum += vc24;
    noise_lp_counter -= 400;
    if (noise_lp_counter <= 0) { noise_lp_counter += 24000; noise_lp_polybit = noise_polybit; }
    if (!noise_lp_polybit) noise_sum += vc25;
    float noise_out = ((float)(noise_sum / 2) - 16384.0f) / 64.0f;   // centrato + rescale

    // ── Effect 1 (sound B): RCDISC4 -> 555 CV -> NOTE -> switch/filtro ──
    ph_rcdisc4_step(e1_vc1, e1_freq);
    float node21 = ph_555_step(e1_555_cap, e1_555_ff, p_e1_555,
                                5.0f, 5.0f, e1_vc1, false, 0.0f);
    float node22 = ph_note_step(e1_note_c1, e1_note_c2, node21, e1_data);
    float node23 = e1_filt ? (3.4f * 100000.0f / 110000.0f) : 3.4f;
    float node24 = node22 * node23;
    float node25 = ph_rcfilter_step(e1_rcfilt, node24, exp_node25);
    float effect1_snd = e1_filt ? node25 : node24;

    // ── Effect 2 (sound A): doppio 555 -> mixer -> filtro lento -> 555 CV -> NOTE ──
    float node33 = ph_555_step(e2_555a_cap, e2_555a_ff, p_e2_555a,
                                5.0f, 5.0f, -1.0f, true, 4.0f);
    float node34 = ph_555_step(e2_555b_cap, e2_555b_ff, p_e2_555b,
                                5.0f, 5.0f, -1.0f, true, 4.0f);
    float node35 = (node33 * (1.0f/10000.0f) + node34 * (1.0f/10200.0f) + 5.0f/5000.0f) * MILLMAN35;
    float node36 = (node34 + node35) * 0.5f;   // mixer2, R45=R46 -> media semplice
    float node37 = ph_rcfilter_step(e2_rcfilt, node36, exp_node37);
    float node38 = (node33 * (1.0f/10000.0f) + node37 * (1.0f/5100.0f) + 5.0f/5000.0f) * MILLMAN38;
    float node39 = ph_555_step(e2_555cv_cap, e2_555cv_ff, p_e2_555cv,
                                5.0f, 5.0f, node38, false, 0.0f);
    float node40 = ph_note_step(e2_note_c1, e2_note_c2, node39, e2_data);
    float effect2_snd = node40 * node32_level;

    // ── Mixer finale (DISCRETE_MIXER4): DC-block per canale + Millman + cAmp + gain ──
    float vt1 = effect1_snd; mix_vcap1 += (vt1 - mix_vcap1) * exp_mix1; vt1 -= mix_vcap1;
    float vt2 = effect2_snd; mix_vcap2 += (vt2 - mix_vcap2) * exp_mix2; vt2 -= mix_vcap2;
    float mix_i = vt1 * (1.0f/57000.0f) + vt2 * (1.0f/30000.0f);
    float mix_v = mix_i * 6628.0f;
    mix_vcamp += (mix_v - mix_vcamp) * exp_camp;
    mix_v -= mix_vcamp;

    float value = mix_v * PHOENIX_MASTER_SCALE + noise_out;

    // ── Melodia MM6221AA (approssimazione, invariata da SPINNERINO) ──
    // Il chip reale suona il brano in loop continuo finche' resta selezionato
    // (viene fermato solo dal trigger sopra, quando il gioco seleziona tune=0).
    // I nostri array sono approssimazioni brevi (~1.6-1.8s): al terminatore
    // 0xFFFF: NON e' un loop (confermato via confronto diretto con MAME dall'utente
    // — la melodia reale e' un brano lungo (~10s+) che suona una volta sola, non si
    // ripete). Il vero bug era che i nostri array TUNE_WARNING/TUNE_FUR_ELISE erano
    // placeholder troppo corti (~1.6-1.8s): allungati per coprire l'intera durata.
    if (mel_active) {
      if (--mel_timer == 0) {
        const uint16_t* t = TUNES[mel_tune & 0x03];
        uint16_t f = t[mel_idx];
        if (f == 0xFFFF) { mel_active = false; mel_freq = 0; }
        else {
          if (f != 0) mel_freq = f;
          mel_idx++;
          if (mel_idx >= 100) mel_idx = 99;
          mel_timer = 24000 / 5;
        }
      }
      if (mel_freq) {
        mel_phase += (uint32_t)mel_freq * 65536U / 24000U;
        int16_t m  = (mel_phase & 0x8000) ? 28 : -28;
        int16_t m2 = ((mel_phase << 1) & 0x8000) ? 14 : -14;
        value += m + m2;
      }
    }

    if (value > 512)  value = 512;
    if (value < -512) value = -512;
    out[i] = (int16_t)value;
  }
}
