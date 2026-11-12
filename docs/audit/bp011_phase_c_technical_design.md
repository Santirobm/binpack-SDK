# Especificación y Diseño Técnico R6.4 — Hito BP-011 / Fase C
## Protocolo Decisivo de Validación de Viabilidad Comercial en Telemetría Industrial

**Proyecto:** BinPack Enterprise SDK  
**Hito:** BP-011  
**Fase:** Fase C — Auditoría de Instrumentación y Diseño de Arneses (Revisión Definitiva R6.4)  
**Documento Rector:** `docs/audit/bp011_phase_c_technical_design.md`  
**Rama:** `audit/bp1-core-safety`  
**Fecha:** 12 de noviembre de 2026  
**Estado:** ESPECIFICACIÓN TÉCNICA DEFINITIVA DE DISEÑO — CIERRE CONTRACTUAL INCONDICIONAL (REVISIÓN R6.4)  
**Gobernanza:** *Code Freeze* preservado al 100% (cero modificaciones en `src/`, `include/` y `deps/`; sin compilación ni ejecución en esta fase).

---

## Control de Cambios de la Revisión R6.4 (Cierre Definitivo de Observaciones)

En cumplimiento estricto del dictamen de auditoría sobre R6.3, esta **Revisión R6.4** incorpora las cuatro resoluciones técnicas y metodológicas finales:

1. **NC-01 (Timestamp R15.10 Literal y Dominio de Ciclos C-MAPSS):**
   - Se copia literalmente la especificación canónica de R15.10: codificación delta-of-delta con sesgo (*bias*) para deltas de 7 bits ($D_7 = D + 63$), 9 bits ($D_9 = D + 255$) y 12 bits ($D_{12} = D + 2047$), delta irregular de 32 bits en complemento a 2, delta inicial de 64 bits con signo bajo prefijo `111110` (70 bits en total) y escape absoluto de 64 bits bajo prefijo `111111` (70 bits en total).
   - Para C-MAPSS, se formaliza el dominio temporal como un contador entero discreto de ciclos de operación por motor mapeado a `int64_t` ($t_i = \text{cycle}_i \in [1, 2^{63}-1]$). En cada límite de motor, se emite incondicionalmente el escape absoluto `111111` (64 bits) y se reinicia `has_prev_delta = false`, unificando el tratamiento en los cuatro motores.
2. **NC-02 (Fórmula Canónica de Ahorro Preservada):**  
   Se mantiene la fórmula aprobada normalizada sobre el tamaño del competidor:
   $$\text{Ahorro}_X = \left( \frac{B_{\text{total}, X} - B_{\text{total}, \text{BinPack}}}{B_{\text{total}, X}} \right) \times 100\%$$
3. **NC-03 (Paridad de `Zstd_Composed` Preservada):**  
   Se mantiene la regla aprobada: `Zstd_Composed` comprime exactamente la misma matriz de valores F64 no comprimidos ($N \times K \times 8$ bytes) a nivel 3, compartiendo la cabecera fija de 64B, metadatos y stream temporal R15.10.
4. **NC-04 (Medición de Memoria RAM por Proceso Aislado y Asignación de Códec):**  
   Para asegurar que el working set pico corresponda estrictamente a la operación evaluada, cada medición experimental se ejecuta en un proceso independiente (*isolated process run*), midiendo $\Delta \text{RAM}_{\text{process}} = \text{PeakWorkingSetSize} - \text{WorkingSetSize}_{\text{init}} \le 16\text{ MB}$, complementado con la medición de memoria dinámica asignada por las estructuras del códec ($\text{RAM}_{\text{codec}} \le 2\text{ MB}$).
5. **Denominación Rigurosa y Honesta de `Chimp128_Composed_Variant`:**  
   Se rotula formalmente como `Chimp128_Composed_Variant`: una variante experimental de alta compresión inspirada en Chimp128 (VLDB 2022) con tabla hash de $2^{14}$ posiciones, buffer circular de 128 elementos y guarda `has_valid_window`, evitando atribuirle indebidamente una equivalencia canónica absoluta.

---

## 1. Misión, Pregunta Comercial y Principios Rectores

### 1.1 Pregunta Comercial de Negocio
El objetivo exclusivo de BP-011 es responder con evidencia empírica inmutable a la siguiente pregunta:
> *¿Ofrece BinPack una ventaja comercial suficientemente importante en telemetría industrial frente a las soluciones establecidas (Gorilla y Chimp128) como para justificar su continuidad, comercialización e inversión?*

BinPack **no necesita ganar en todos los datasets ni en todas las métricas**:
- Si logra compresión similar pero **recupera un sensor individual $\ge 2\times$ más rápido** sin descomprimir toda la matriz, tiene un producto viable para bases de datos de series temporales (TSDB) e historiadores industriales.
- Si logra $\ge 15\%$ de ahorro de espacio con velocidad competitiva, tiene valor para compresión en el Edge y reducción de costes de transmisión en la nube.
- Si no ofrece ventaja en compresión ni en consultas selectivas, se detendrá el proyecto de forma responsable.

### 1.2 Principios Rectores Innegociables
1. **Reconstrucción Exacta (Lossless):** Tolerancia cero a pérdida de datos; verificación bit a bit en todas las muestras flotantes.
2. **Paridad Estricta de Carga Útil:** Todos los motores comprimen exactamente las mismas tuplas $(t_i, v_{i, 0}, \dots, v_{i, K-1})$ empaquetadas en bloques autodescriptivos.
3. **Aislamiento Experimental Absoluto:** Todo el código reside exclusivamente en [`tests/experiments/bp011/`](file:///C:/Users/sgi.lm/Desktop/binpack-SDK/tests/experiments/bp011/), manteniendo `src/`, `include/` y `deps/` congelados al 100%.
4. **Cierre de Ciclo Documental:** R6.4 es la última revisión documental. Tras su aprobación, se inicia la codificación.

---

## 2. Matriz de Competidores y Corpus Mínimo Certificado

### 2.1 Matriz de Competidores (Denominación Compuesta)
Todos los motores operan sobre el contenedor industrial común con timestamps R15.10 para aislar el rendimiento del códec de valores:
1. **`Gorilla_Composed`:** Códec de valores Gorilla F64 (Pelkonen et al. 2015) sobre contenedor R15.10.
2. **`Chimp128_Composed_Variant`:** Variante experimental de alta compresión inspirada en Chimp128 F64 (Liakos et al. VLDB 2022) sobre contenedor R15.10.
3. **`BinPack_Transpose`:** Nuestra propuesta central (transposición por planos de bytes + códec BinPack con acceso selectivo).
4. **`Zstd_Composed`:** Compresor general Zstd (nivel 3 por defecto) comprimiendo exactamente la misma matriz de valores en bruto.

### 2.2 Corpus Mínimo Certificado (Fase B)
1. **NASA Turbofan C-MAPSS (FD001):** 24 variables operativas y de sensores por ciclo, 20.631 registros temporales distribuidos en 100 motores discontinuos.
2. **NASA Bearing IMS:** 48 características estadísticas derivadas de condición (media, RMS, kurtosis, etc., 4 rodamientos $\times$ 12 métricas), 2.156 ventanas temporales continuas.

---

## 3. Contrato Temporal Literal según Protocolo R15.10 (NC-01 y NC-03)

### 3.1 Especificación Exacta y Literal de R15.10
Todos los motores comprimen timestamps de 64 bits (`int64_t`) utilizando la tabla libre de prefijos aprobada en R15.10:

```
                          [Root]
                         /                             [0]      [1]
                     (D=0)     /                                [10]  [11]
                           (7 bits)/                                   [110]  [111]
                              (9 bits) /                                        [1110] [1111]
                                 (12 bits) /                                            [11110] [11111]
                                       (32 bits)/                                                  [111110]  [111111]
                                           (Delta 64) (Escape Abs 64)
```

| Operación / Rango de $D$ | Prefijo Binario | Longitud | Campo de Datos Asociado | Codificación de Datos |
|---|:---:|:---:|---|---|
| **$D = 0$** | `0` | 1 bit | *Ninguno* | *Sin datos adicionales* |
| **$-63 \le D \le 64$** | `10` | 2 bits | 7 bits no signados | Sesgo (*bias*): $D_7 = (uint8\_t)(D + 63) \in [0, 127]$ |
| **$-255 \le D \le 256$** | `110` | 3 bits | 9 bits no signados | Sesgo (*bias*): $D_9 = (uint16\_t)(D + 255) \in [0, 511]$ |
| **$-2047 \le D \le 2048$** | `1110` | 4 bits | 12 bits no signados | Sesgo (*bias*): $D_{12} = (uint16\_t)(D + 2047) \in [0, 4095]$ |
| **$-2^{31} \le D < 2^{31}$** | `11110` | 5 bits | 32 bits con signo | Complemento a 2: $D_{32} = (uint32\_t)((int32\_t)D)$ |
| **Delta Inicial de 64 Bits** | `111110` | 6 bits | 64 bits con signo | Complemento a 2: `(uint64_t)delta_init` (70 bits total) |
| **Escape Absoluto de 64 Bits** | `111111` | 6 bits | 64 bits con signo | Timestamp literal: `(uint64_t)t_i` (70 bits total) |

#### Máquina de Estados del Codificador Temporal R15.10:
1. **Emisión y Recepción de Escape Absoluto (Muestra $i$):**
   - *Codificador:* Emite el prefijo `111111` (6 bits) seguido de los 64 bits de $t_i$.
   - *Decodificador:* Lee `111111`, lee 64 bits de $t_i$, actualiza `prev_timestamp = t_i` y fija:
     $$\text{has\_prev\_delta} = \text{false}$$
2. **Transición en la Muestra Subsiguiente (Muestra $i+1$):**
   - Al estar $\text{has\_prev\_delta} = \text{false}$, se evalúa la diferencia simple $\Delta t_{i+1} = t_{i+1} - t_i$.
   - *Caso A (Delta representable sin desbordamiento en `int64_t`):*
     - *Codificador:* Emite `111110` (6 bits) seguido de los 64 bits de $\Delta t_{i+1}$.
     - *Decodificador:* Lee `111110`, lee 64 bits de $\Delta t_{i+1}$, comprueba ausencia de overflow, calcula $t_{i+1} = t_i + \Delta t_{i+1}$, actualiza `prev_timestamp = t_{i+1}`, `prev_delta = \Delta t_{i+1}` y fija:
       $$\text{has\_prev\_delta} = \text{true}$$
   - *Caso B (Diferencia simple desborda `int64_t`):*
     - *Codificador:* Emite nuevamente el escape absoluto `111111` seguido de los 64 bits de $t_{i+1}$.
     - *Decodificador:* Lee `111111`, actualiza `prev_timestamp = t_{i+1}` y permanece en $\text{has\_prev\_delta} = \text{false}$.
3. **Régimen Delta-of-Delta Pleno (Muestra $i+2$ en adelante):**
   - Con $\text{has\_prev\_delta} = \text{true}$, se evalúa $D = \Delta t_{i+2} - \Delta t_{i+1}$.
   - Se emite el prefijo correspondiente con su campo sesgado o en complemento a 2, y se actualiza `prev_delta = \Delta t_{i+2}`, `prev_timestamp = t_{i+2}`.

### 3.2 Dominio Temporal de C-MAPSS y Protocolo de Reinicio (100 Motores)
- **Dominio de Ciclos:** En C-MAPSS, el dominio temporal no representa una época Unix en microsegundos, sino un contador entero discreto de ciclos de operación por motor, mapeado directamente al tipo entero `int64_t` ($t_i = \text{cycle}_i \in [1, 2^{63}-1]$).
- **Reinicio por Frontera de Motor:** Al transicionar del motor $m-1$ al motor $m$, el codificador emite obligatoriamente el **escape absoluto `111111` (6 bits)** seguido del `start_cycle` del motor entrante en 64 bits (70 bits en total) y reinicia el predictor:
  $$\text{has\_prev\_delta} = \text{false}, \quad \text{prev\_delta} = 0, \quad \text{prev\_timestamp} = \text{segment.start\_cycle}$$
  Este procedimiento se ejecuta de forma idéntica y simultánea en los cuatro motores.

### 3.3 Delimitación de Carga Útil y Contabilización de Bytes (NC-03)
- **Matriz de Valores:** Para todos los motores, la carga de valores en bruto consiste exactamente en la matriz no comprimida de $N \times K$ flotantes de 64 bits ($N \times K \times 8$ bytes).
- **`Zstd_Composed`:** Comprime exactamente dicha matriz de valores en bruto a nivel 3 (por defecto), produciendo su stream $B_{\text{values}}$.
- **`Gorilla_Composed` y `Chimp128_Composed_Variant`:** Comprimen exactamente la misma secuencia de valores, produciendo su respectivo stream $B_{\text{values}}$.
- **`BinPack_Transpose`:** Transpone la matriz por planos de bytes y la comprime, produciendo su respectivo stream $B_{\text{values}}$.
- **Cómputo Total Idéntico:**
  $$B_{\text{total}} = B_{\text{header}} + B_{\text{meta}} + B_{\text{time}} + B_{\text{values}}$$
  donde $B_{\text{header}} = 64$ bytes y $B_{\text{time}}$ es el flujo temporal R15.10 compartido por todos.

---

## 4. Especificación Rigurosa de Códecs de Valores

### 4.1 Gorilla F64 (Pelkonen et al. 2015)
- **Máscara Segura de 64 Bits:**
  ```c
  static inline uint64_t bp011_make_mask64(uint8_t len) {
      if (len == 0) return 0ULL;
      if (len >= 64) return ~0ULL;
      return (1ULL << len) - 1ULL;
  }
  ```
- **Ciclo de Vida y Estados:**
  - Primer valor $v_0$: 64 bits literales. Se inicializa $\text{prev\_val} = v_0$, $\text{prev\_lz\_eff} = 64$, $\text{prev\_tz} = 64$, $\text{prev\_len} = 0$.
  - Muestra $i \ge 1$, $XOR = v_i \oplus v_{i-1}$:
    - Si $XOR == 0$: emite bit `0`.
    - Si $XOR \ne 0$: emite bit `1`.  
      $lz = \text{clz64}(XOR)$, $tz = \text{ctz64}(XOR)$, $lz_{\text{eff}} = \min(lz, 31)$, $\text{len} = 64 - lz_{\text{eff}} - tz$.
      - Si $lz_{\text{eff}} \ge \text{prev\_lz\_eff} \land tz \ge \text{prev\_tz}$: bit de control `0` + bits en ventana previa $\text{prev\_len}$.
      - En otro caso: bit de control `1` + $lz_{\text{eff}}$ (5b) + $(\text{len} - 1)$ (6b) + $\text{len}$ bits significativos. Actualiza $\text{prev\_lz\_eff} = lz_{\text{eff}}$, $\text{prev\_tz} = tz$, $\text{prev\_len} = \text{len}$.

### 4.2 `Chimp128_Composed_Variant` con Guarda de Ventana Válida
- **Estructura de Estado:**
  ```c
  #define CHIMP128_RING_SIZE        128
  #define CHIMP128_HASH_TABLE_SIZE  16384 // 2^14 entradas

  typedef struct {
      uint64_t ring_buf[CHIMP128_RING_SIZE];
      uint32_t ring_sample_idx[CHIMP128_RING_SIZE];
      int32_t  hash_table[CHIMP128_HASH_TABLE_SIZE];
      uint64_t prev_val;
      uint8_t  prev_lz_quant;
      uint8_t  prev_tz;
      uint8_t  prev_len;
      bool     has_valid_window; // Guarda: false hasta emitir la primera ventana no nula
  } chimp128_f64_state_t;
  ```

- **Inicialización Obligatoria:**
  ```c
  void chimp128_f64_init(chimp128_f64_state_t *st) {
      memset(st->ring_buf, 0, sizeof(st->ring_buf));
      memset(st->ring_sample_idx, 0, sizeof(st->ring_sample_idx));
      for (int i = 0; i < CHIMP128_HASH_TABLE_SIZE; i++) st->hash_table[i] = -1;
      st->prev_val = 0;
      st->prev_lz_quant = 0;
      st->prev_tz = 0;
      st->prev_len = 0;
      st->has_valid_window = false;
  }
  ```

- **Primer Valor ($v_0$):**  
  Se escribe como literal de 64 bits (`uint64_t`). Se inicializa:
  `st->prev_val = v_0; st->ring_buf[0] = v_0; st->ring_sample_idx[0] = 0; st->hash_table[v_0 & 0x3FFF] = 0; st->has_valid_window = false;`

- **Muestras Subsecuentes ($v_i, i \ge 1$):**
  1. Extraer clave hash: $\text{key} = \text{as\_uint64}(v_i) \ \& \ 0x3FFF$.
  2. Consultar candidato: $c = \text{st->hash\_table}[\text{key}]$.
  3. Validar ventana inclusiva:
     $$\text{is\_valid\_cand} = (c \ge 0) \ \land \ (1 \le (i - c) \le 128)$$
  4. Decisión de Umbral Estricto: Si $\text{is\_valid\_cand}$ es verdadero, se selecciona la referencia histórica ($v_{\text{ref}} = \text{st->ring\_buf}[c \bmod 128]$) ya que la clave de 14 bits asegura $tz_c \ge 14 > 13$. De lo contrario, $v_{\text{ref}} = \text{st->prev\_val}$.
  5. Codificación de Bits:
     - **Si usa referencia histórica:** Emite bit `1` + 7 bits de índice ($c \bmod 128$).
     - **Si usa referencia precedente:** Emite bit `0` (sin índice).
     - **Codificación del XOR ($XOR = v_i \oplus v_{\text{ref}}$):**
       - Si $XOR == 0$: emite bit `0`.
       - Si $XOR \ne 0$: emite bit `1`.  
         $lz = \text{clz64}(XOR)$, $tz = \text{ctz64}(XOR)$, $lz_{\text{quant}} = \min(7, \lfloor lz / 8 \rfloor)$.
         - **Reutilización de Ventana (Requiere Ventana Válida Previa):**
           Si $\text{st->has\_valid\_window} \land (lz_{\text{quant}} == \text{st->prev\_lz\_quant}) \land (tz == \text{st->prev\_tz})$:
           - Emite bit `0` + bits centrales de longitud $\text{st->prev\_len}$.
         - **Nueva Ventana Significativa:**
           - Emite bit `1` + $lz_{\text{quant}}$ (3b) + $(\text{len} - 1)$ (6b) + $\text{len}$ bits significativos, donde $\text{len} = 64 - (lz_{\text{quant}} \times 8) - tz$.
           - Actualiza: $\text{st->prev\_lz\_quant} = lz_{\text{quant}}$, $\text{st->prev\_tz} = tz$, $\text{st->prev\_len} = \text{len}$, $\text{st->has\_valid\_window} = \text{true}$.
  6. Actualización obligatoria de estado:
     `st->ring_buf[i % 128] = v_i; st->ring_sample_idx[i % 128] = i; st->hash_table[key] = i; st->prev_val = v_i;`

---

## 5. Contrato Binario y Garantías de Seguridad

### 5.1 Cabecera Canónica de Bloque (64 Bytes Exactos)
```c
#define BP011_BLOCK_MAGIC             0x4B504E42 // "BNPK" Little-Endian
#define BP011_FORMAT_VERSION          0x0001
#define BP011_HEADER_SIZE_BYTES       64
#define BP011_FLAG_HAS_CHANNEL_TABLE  (1U << 0)
#define BP011_FLAG_HAS_SERIES_DIR     (1U << 1)
#define BP011_KNOWN_FLAGS             (BP011_FLAG_HAS_CHANNEL_TABLE | BP011_FLAG_HAS_SERIES_DIR)

typedef struct {
    uint32_t magic;                 // 0x00: 0x4B504E42 ("BNPK")
    uint16_t version;               // 0x04: 0x0001
    uint16_t flags;                 // 0x06: Flags de presencia
    uint32_t num_channels;          // 0x08: K
    uint32_t sample_count;          // 0x0C: N
    uint32_t channel_table_offset;  // 0x10: Offset de tabla M7 (0 si ausente)
    uint32_t channel_table_bytes;   // 0x14: Bytes de tabla M7 (K * 12 si presente)
    uint32_t series_dir_offset;     // 0x18: Offset directorio series (0 si ausente)
    uint32_t series_dir_bytes;      // 0x1C: Bytes directorio (M * 32 si presente)
    uint32_t time_stream_offset;    // 0x20: Offset obligatorio stream tiempo
    uint32_t time_stream_bytes;     // 0x24: Bytes obligatorios stream tiempo
    uint32_t time_stream_bits;      // 0x28: Bits obligatorios stream tiempo
    uint32_t values_stream_offset;  // 0x2C: Offset obligatorio stream valores
    uint32_t values_stream_bytes;   // 0x30: Bytes obligatorios stream valores
    uint32_t values_stream_bits;    // 0x34: Bits obligatorios stream valores
    uint32_t total_file_bytes;      // 0x38: Tamaño total del bloque
    uint32_t crc32;                 // 0x3C: CRC-32 (IEEE 802.3) con crc32=0
} bp011_block_header_t;
```

### 5.2 Algoritmo de Validación Incondicional con Orden Seguro
```c
typedef struct {
    uint32_t start;
    uint32_t end;
    bool active;
} bp011_range_t;

static inline bool bp011_ranges_overlap(const bp011_range_t *a, const bp011_range_t *b) {
    if (!a->active || !b->active) return false;
    return !(a->end <= b->start || b->end <= a->start);
}

bp011_status_t bp011_validate_header(const bp011_block_header_t *h, const uint8_t *file_buf, uint64_t actual_file_size) {
    if (!h || !file_buf) return BP011_ERR_NULL_POINTER;
    if (actual_file_size < BP011_HEADER_SIZE_BYTES) return BP011_ERR_BUFFER_UNDERFLOW;
    if (h->magic != BP011_BLOCK_MAGIC) return BP011_ERR_INVALID_ARG;
    if (h->version != BP011_FORMAT_VERSION) return BP011_ERR_INVALID_ARG;
    if (h->total_file_bytes > actual_file_size) return BP011_ERR_STREAM_TRUNCATED;
    if (h->total_file_bytes < BP011_HEADER_SIZE_BYTES) return BP011_ERR_BUFFER_UNDERFLOW;

    if ((h->flags & ~BP011_KNOWN_FLAGS) != 0) return BP011_ERR_UNKNOWN_FLAGS;
    if (h->sample_count == 0 || h->num_channels == 0) return BP011_ERR_INVALID_ARG;
    if (h->time_stream_bytes == 0 || h->time_stream_bits == 0) return BP011_ERR_INVALID_ARG;
    if (h->values_stream_bytes == 0 || h->values_stream_bits == 0) return BP011_ERR_INVALID_ARG;

    if (h->time_stream_bits > (uint64_t)h->time_stream_bytes * 8ULL) return BP011_ERR_INVALID_ARG;
    if (h->values_stream_bits > (uint64_t)h->values_stream_bytes * 8ULL) return BP011_ERR_INVALID_ARG;

    bool has_chan = (h->flags & BP011_FLAG_HAS_CHANNEL_TABLE) != 0;
    if (!has_chan && (h->channel_table_offset != 0 || h->channel_table_bytes != 0)) return BP011_ERR_INVALID_OFFSET;
    if (has_chan && (h->channel_table_offset < BP011_HEADER_SIZE_BYTES || h->channel_table_bytes == 0)) return BP011_ERR_INVALID_OFFSET;

    bool has_series = (h->flags & BP011_FLAG_HAS_SERIES_DIR) != 0;
    if (!has_series && (h->series_dir_offset != 0 || h->series_dir_bytes != 0)) return BP011_ERR_INVALID_OFFSET;
    if (has_series && (h->series_dir_offset < BP011_HEADER_SIZE_BYTES || h->series_dir_bytes == 0)) return BP011_ERR_INVALID_OFFSET;

    #define CHECK_BOUNDS(off, bytes) do {         if ((off) < BP011_HEADER_SIZE_BYTES) return BP011_ERR_INVALID_OFFSET;         if ((off) > h->total_file_bytes) return BP011_ERR_INVALID_OFFSET;         if ((bytes) > h->total_file_bytes - (off)) return BP011_ERR_INVALID_OFFSET;     } while (0)

    if (has_chan) CHECK_BOUNDS(h->channel_table_offset, h->channel_table_bytes);
    if (has_series) CHECK_BOUNDS(h->series_dir_offset, h->series_dir_bytes);
    CHECK_BOUNDS(h->time_stream_offset, h->time_stream_bytes);
    CHECK_BOUNDS(h->values_stream_offset, h->values_stream_bytes);

    bp011_range_t ranges[5] = {
        { 0, BP011_HEADER_SIZE_BYTES, true },
        { h->channel_table_offset, h->channel_table_offset + h->channel_table_bytes, has_chan },
        { h->series_dir_offset, h->series_dir_offset + h->series_dir_bytes, has_series },
        { h->time_stream_offset, h->time_stream_offset + h->time_stream_bytes, true },
        { h->values_stream_offset, h->values_stream_offset + h->values_stream_bytes, true }
    };

    for (int i = 0; i < 5; i++) {
        for (int j = i + 1; j < 5; j++) {
            if (bp011_ranges_overlap(&ranges[i], &ranges[j])) return BP011_ERR_INVALID_OFFSET;
        }
    }

    // Inspección segura de padding (ejecutada estrictamente tras verificar todos los límites)
    uint8_t time_pad = (uint8_t)((h->time_stream_bytes * 8ULL) - h->time_stream_bits);
    if (time_pad > 0 && time_pad < 8) {
        uint8_t last_byte = file_buf[h->time_stream_offset + h->time_stream_bytes - 1];
        if ((last_byte & ((1U << time_pad) - 1U)) != 0) return BP011_ERR_CORRUPT_PADDING;
    }

    uint8_t val_pad = (uint8_t)((h->values_stream_bytes * 8ULL) - h->values_stream_bits);
    if (val_pad > 0 && val_pad < 8) {
        uint8_t last_byte = file_buf[h->values_stream_offset + h->values_stream_bytes - 1];
        if ((last_byte & ((1U << val_pad) - 1U)) != 0) return BP011_ERR_CORRUPT_PADDING;
    }

    return BP011_OK;
}
```

### 5.3 Validación Segura del Directorio C-MAPSS (Anti-Underflow)
```c
typedef struct {
    uint32_t unit_id;                  // Identificador del motor (1 a M)
    uint32_t start_sample_idx;         // Muestra inicial absoluta (0 a N-1)
    uint32_t sample_count;             // Cantidad de ciclos (> 0)
    uint32_t start_cycle;              // Ciclo inicial físico (ej. 1)
    uint32_t time_stream_byte_offset;  // Offset de tiempo del motor
    uint32_t time_stream_bytes;        // Longitud en bytes de tiempo del motor
    uint32_t values_stream_byte_offset;// Offset de valores del motor
    uint32_t values_stream_bytes;      // Longitud en bytes de valores del motor
} bp011_series_segment_t;

bp011_status_t bp011_verify_series_directory(const bp011_block_header_t *h, const bp011_series_segment_t *dir, uint32_t num_series) {
    if (!h || !dir) return BP011_ERR_NULL_POINTER;
    if (num_series == 0) return BP011_ERR_INVALID_ARG;

    uint32_t expected_sample_cursor = 0;
    uint32_t expected_time_cursor = h->time_stream_offset;
    uint32_t expected_val_cursor = h->values_stream_offset;

    for (uint32_t m = 0; m < num_series; m++) {
        const bp011_series_segment_t *seg = &dir[m];

        if (seg->sample_count == 0) return BP011_ERR_EMPTY_SERIES;
        if (seg->time_stream_bytes == 0 || seg->values_stream_bytes == 0) return BP011_ERR_INVALID_ARG;
        if (m > 0 && seg->unit_id <= dir[m - 1].unit_id) return BP011_ERR_CORRUPT_DIRECTORY;

        // Comprobación incondicional anti-underflow
        if (seg->start_sample_idx > h->sample_count) return BP011_ERR_INVALID_OFFSET;
        if (seg->sample_count > h->sample_count - seg->start_sample_idx) return BP011_ERR_INVALID_ARG;

        if (seg->start_sample_idx < expected_sample_cursor) return BP011_ERR_OVERLAPPING_SEGMENT;
        expected_sample_cursor = seg->start_sample_idx + seg->sample_count;

        if (seg->time_stream_byte_offset < expected_time_cursor) return BP011_ERR_OVERLAPPING_SEGMENT;
        if (seg->time_stream_byte_offset > h->time_stream_offset + h->time_stream_bytes) return BP011_ERR_INVALID_OFFSET;
        if (seg->time_stream_bytes > (h->time_stream_offset + h->time_stream_bytes) - seg->time_stream_byte_offset) {
            return BP011_ERR_INVALID_OFFSET;
        }
        expected_time_cursor = seg->time_stream_byte_offset + seg->time_stream_bytes;

        if (seg->values_stream_byte_offset < expected_val_cursor) return BP011_ERR_OVERLAPPING_SEGMENT;
        if (seg->values_stream_byte_offset > h->values_stream_offset + h->values_stream_bytes) return BP011_ERR_INVALID_OFFSET;
        if (seg->values_stream_bytes > (h->values_stream_offset + h->values_stream_bytes) - seg->values_stream_byte_offset) {
            return BP011_ERR_INVALID_OFFSET;
        }
        expected_val_cursor = seg->values_stream_byte_offset + seg->values_stream_bytes;
    }
    return BP011_OK;
}
```

---

## 6. Fórmulas Matemáticas y Criterios Comerciales (NC-02 y NC-04)

### 6.1 Fórmulas Analíticas Exactas
1. **Ratio de Compresión ($CR$):**
   $$CR = \frac{B_{\text{raw}}}{B_{\text{total}}}$$
   donde $B_{\text{raw}} = N \times (8 + K \times 8)$ bytes para tuplas F64 con timestamps de 64 bits.

2. **Ahorro Porcentual frente al Competidor $X$ ($\text{Ahorro}_X$):**
   $$\boxed{\text{Ahorro}_X = \left( \frac{B_{\text{total}, X} - B_{\text{total}, \text{BinPack}}}{B_{\text{total}, X}} \right) \times 100\%}$$
   *(Normalizado estrictamente sobre el tamaño del competidor $X$).*

3. **Throughputs Computacionales:**
   $$TP_{\text{comp}} = \frac{B_{\text{raw}}}{10^6 \times T_{\text{comp}}} \quad (\text{MB/s}), \qquad TP_{\text{decomp}} = \frac{B_{\text{raw}}}{10^6 \times T_{\text{decomp}}} \quad (\text{MB/s})$$

4. **Eficiencia de Consulta Selectiva de un Canal:**
   - Speedup frente a `Gorilla_Composed`:
     $$S_{\text{select}} = \frac{T_{\text{select}, \text{Gorilla}}}{T_{\text{select}, \text{BinPack}}}$$
   - Amplificación de Descompresión:
     $$A_{\text{decomp}} = \frac{B_{\text{decomp}}}{N \times 8}$$

5. **Medición Metodológica de Memoria RAM por Proceso Aislado:**
   Para garantizar que el working set pico mida exclusivamente la operación bajo prueba y no picos residuales previos:
   - **Protocolo de Proceso Aislado:** Cada benchmark se ejecuta en un proceso hijo independiente creado para tal fin (`CreateProcessA`), donde el sistema operativo asigna una tabla de páginas limpia.
   - $\text{RAM}_{\text{base}}$: Working set tras la carga inicial del ejecutable (`WorkingSetSize`).
   - $\text{RAM}_{\text{peak}}$: Working set pico durante la ejecución de la compresión/descompresión (`PeakWorkingSetSize`).
   - $\Delta \text{RAM}_{\text{process}} = \text{RAM}_{\text{peak}} - \text{RAM}_{\text{base}}$ (límite contractual $\le 16\text{ MB}$).
   - $\text{RAM}_{\text{codec}}$: Memoria dinámica asignada exclusivamente por las estructuras internas del algoritmo (ej. tabla hash de $16.384 \times 4\text{ B} = 64\text{ KB}$ y buffer circular de $128 \times 8\text{ B} = 1\text{ KB}$ en Chimp128), con límite estricto de $\text{RAM}_{\text{codec}} \le 2\text{ MB}$.

### 6.2 Criterios Inmutables de Decisión Comercial

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                         CRITERIOS DE DECISIÓN COMERCIAL BP-011                         │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ 1. [SEGUIR DESARROLLANDO] Ventaja Comercial Demostrada:                                │
│    • Ahorro_Gorilla >= 15.0%  Y  Ahorro_Chimp128 >= 15.0%                              │
│      O BIEN                                                                            │
│    • S_select >= 2.0x frente a Gorilla Y A_decomp <= 2.0                               │
│      CONDICIÓN COMPLEMENTARIA:                                                         │
│      TP_comp(BinPack) >= 0.50 * TP_comp(Gorilla)  Y  ΔRAM_process <= 16 MB.            │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ 2. [REPOSICIONAR] Ventaja de Nicho Especializado:                                      │
│    • BinPack no alcanza el 15% de ahorro global, pero demuestra S_select >= 3.0x       │
│      en C-MAPSS (K=24) o IMS (K=48) con A_decomp <= 1.5, posibilitando un motor de    │
│      consultas analíticas directas por canal sin descompresión masiva.                 │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ 3. [DESCARTAR / CONGELAR] Sin Ventaja Demostrada:                                      │
│    • BinPack no supera a Gorilla/Chimp128 en compresión (Ahorro < 5%), y S_select < 1.5x.│
│      Se procede al cierre y congelamiento definitivo y honesto del motor.              │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 7. Windows Unbuffered I/O y Tratamiento del Último Bloque

1. **Consulta Dinámica por Ruta de Volumen:**  
   Se obtiene la ruta del volumen mediante `GetVolumePathNameA(filepath, volume_path, MAX_PATH)` y se consultan los sectores físicos y lógicos mediante `DeviceIoControl` (`STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR`) con fallback a `GetDiskFreeSpaceA(volume_path)`.
2. **Validación de Buffer:**  
   Se verifica explícitamente que la dirección devuelta por `VirtualAlloc` cumpla `((uintptr_t)buf & (align.buffer_align - 1)) == 0`.
3. **Gestión Matemática del Último Bloque:**  
   Para evitar errores `ERROR_HANDLE_EOF` con `FILE_FLAG_NO_BUFFERING`:
   - Límite de archivo redondeado hacia arriba: $F_{\text{aligned}} = \lceil F_{\text{size}} / A \rceil \times A$.
   - Lectura acotada al sector: $L_{\text{read}} = \min(L_{\text{aligned}}, F_{\text{aligned}} - O_{\text{aligned}})$.
   - Bytes válidos extraídos: $\min(L, F_{\text{size}} - O)$.

---

## 8. Hoja de Ruta Ejecutiva Definitiva

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                        HOJA DE RUTA EJECUTIVA PARA BP-011                              │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ Paso 1: Aprobación Formal de R6.4 (Última Revisión Documental de Diseño).              │
│ Paso 2: Fase C (Codificación) en tests/experiments/bp011/ de los motores y arnés.     │
│ Paso 3: Fase D (Auditoría Estática de Arneses y Compilación Segura).                  │
│ Paso 4: Fase E (Campaña Experimental Única sobre C-MAPSS e IMS).                       │
│ Paso 5: Dictamen Comercial: Decisión definitiva (Continuar / Reposicionar / Congelar). │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 9. Conclusión y Petición Formal

El diseño técnico presentado en esta **Revisión R6.4** satisface plenamente y sin reservas las directrices de cierre:
1. Copia literal de campos, sesgos ($D_7, D_9, D_{12}$), prefijos y delta inicial de 64 bits de R15.10.
2. Definición formal del dominio de ciclos en C-MAPSS y reinicio mediante escape `111111` al saltar entre los 100 motores.
3. Medición de memoria pico limpia mediante arquitectura de proceso aislado ($\Delta	ext{RAM}_{	ext{process}} \le 16	ext{ MB}$) y $	ext{RAM}_{	ext{codec}} \le 2	ext{ MB}$.
4. Denominación honesta e inequívoca de `Chimp128_Composed_Variant`.

Por todo lo expuesto, se solicita formalmente al Auditor Líder:
1. **Aprobación incondicional de la Especificación y Diseño Técnico R6.4 como documento definitivo de la Fase C.**
2. **Autorización formal para iniciar la codificación experimental** exclusivamente en [`tests/experiments/bp011/`](file:///C:/Users/sgi.lm/Desktop/binpack-SDK/tests/experiments/bp011/), manteniendo intacto el *code freeze* en `src/`, `include/` y `deps/`.
