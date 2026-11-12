# Especificación y Diseño Técnico R6.3 — Hito BP-011 / Fase C
## Protocolo Decisivo de Validación de Viabilidad Comercial en Telemetría Industrial

**Proyecto:** BinPack Enterprise SDK  
**Hito:** BP-011  
**Fase:** Fase C — Auditoría de Instrumentación y Diseño de Arneses (Revisión Definitiva R6.3)  
**Documento Rector:** `docs/audit/bp011_phase_c_technical_design.md`  
**Rama:** `audit/bp1-core-safety`  
**Fecha:** 12 de noviembre de 2026  
**Estado:** ESPECIFICACIÓN TÉCNICA DEFINITIVA DE DISEÑO — CIERRE CONTRACTUAL FINAL (REVISIÓN R6.3)  
**Gobernanza:** *Code Freeze* preservado al 100% (cero modificaciones en `src/`, `include/` y `deps/`; sin compilación ni ejecución en esta fase).

---

## Control de Cambios de la Revisión R6.3 (Cierre Técnico y Contractual Pleno)

En estricta atención al dictamen de auditoría emitido sobre la Revisión R6.2, la presente **Revisión R6.3** incorpora de forma concluyente las cuatro correcciones solicitadas por el Auditor Líder:

1. **NC-01 (Protocolo de Timestamp R15.10 Literal y Discontinuidad C-MAPSS):**  
   Se adopta de forma literal el protocolo aprobado en R15.10: codificación delta-of-delta con signo mediante prefijos `0` ($D=0$), `10` (7 bits), `110` (9 bits), `1110` (12 bits), `11110` (32 bits), escape de delta inicial `111110` (32 bits) y escape absoluto `111111` (64 bits). Para C-MAPSS (100 motores), cada motor comienza emitiendo incondicionalmente el escape absoluto `111111` seguido del `start_cycle` en 64 bits y reiniciando el estado (`has_prev_delta = false`, `prev_delta = 0`, `prev_time = start_cycle`), asegurando una especificación matemáticamente exacta.
2. **NC-02 (Fórmula Correcta de Ahorro de Compresión):**  
   Se corrige formalmente la fórmula de mejora frente al competidor $X$, normalizando sobre el tamaño del competidor:
   $$\text{Ahorro}_X = \left( \frac{B_{\text{total}, X} - B_{\text{total}, \text{BinPack}}}{B_{\text{total}, X}} \right) \times 100\%$$
   eliminando cualquier distorsión matemática y garantizando que un ahorro del 15% represente una reducción real del 15% del volumen del competidor.
3. **NC-03 (Delimitación Exacta de la Carga de `Zstd_Composed`):**  
   Se establece por contrato que `Zstd_Composed` opera sobre la **misma carga útil exacta de valores**: comprime la matriz de valores no comprimidos ($N \times K \times 8$ bytes) a nivel 3 (por defecto). La cabecera (64B), los metadatos y el stream temporal R15.10 se contabilizan de forma idéntica en los cuatro motores:
   $$B_{\text{total}} = B_{\text{header}} + B_{\text{meta}} + B_{\text{time}} + B_{\text{values}}$$
4. **NC-04 (Instrumentación Discriminada de Memoria RAM):**  
   Para distinguir el consumo del proceso del consumo propio del algoritmo, se mide:
   - $\text{RAM}_{\text{base}}$: Working set previo a la ejecución.
   - $\text{RAM}_{\text{peak}}$: Working set pico durante la operación (`PeakWorkingSetSize`).
   - $\Delta \text{RAM} = \text{RAM}_{\text{peak}} - \text{RAM}_{\text{base}}$: Memoria incremental neta del proceso (límite $\le 16\text{ MB}$).
   - $\text{RAM}_{\text{codec}}$: Memoria dinámica asignada exclusivamente por el códec (límite $\le 2\text{ MB}$).

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
4. **Cierre de Ciclo Documental:** R6.3 es la última revisión documental. Tras su aprobación, se inicia la codificación.

---

## 2. Matriz de Competidores y Corpus Mínimo Certificado

### 2.1 Matriz de Competidores (Denominación Compuesta)
Para aislar de forma rigurosa la eficiencia del algoritmo de valores sobre telemetría industrial multicanal, todos los motores se integran sobre el contenedor industrial común con timestamps R15.10:
1. **`Gorilla_Composed`:** Códec de valores Gorilla F64 (Pelkonen et al. 2015) sobre contenedor R15.10.
2. **`Chimp128_Composed`:** Códec de valores Chimp128 F64 (Liakos et al. VLDB 2022) sobre contenedor R15.10.
3. **`BinPack_Transpose`:** Nuestra propuesta central (transposición por planos de bytes + códec BinPack con acceso selectivo).
4. **`Zstd_Composed`:** Compresor general Zstd (nivel 3 por defecto) comprimiendo exactamente la misma matriz de valores en bruto.

### 2.2 Corpus Mínimo Certificado (Fase B)
1. **NASA Turbofan C-MAPSS (FD001):** 24 variables operativas y de sensores por ciclo, 20.631 registros temporales distribuidos en 100 motores discontinuos.
2. **NASA Bearing IMS:** 48 características estadísticas derivadas de condición (media, RMS, kurtosis, etc., 4 rodamientos $\times$ 12 métricas), 2.156 ventanas temporales continuas.

---

## 3. Contrato Temporal Común y Paridad de Carga Útil (NC-01 y NC-03)

### 3.1 Especificación Literal del Timestamp según Protocolo R15.10
Todos los motores utilizan el formato temporal canónico aprobado en el protocolo R15.10:

```
┌────────────────────────────────────────────────────────────────────────┐
│             FORMATO TEMPORAL CANÓNICO R15.10 (DELTA-OF-DELTA)          │
├────────────────────────────────────────────────────────────────────────┤
│ • Delta Cero (D = 0):                 Prefijo '0'       (1 bit)        │
│ • Delta Pequeño (-63 <= D <= 64):     Prefijo '10'      +  7 bits (9b) │
│ • Delta Mediano (-255 <= D <= 256):   Prefijo '110'     +  9 bits (12b)│
│ • Delta Grande (-2047 <= D <= 2048):  Prefijo '1110'    + 12 bits (16b)│
│ • Delta Irregular (D en [-2^31, 2^31-1]): Prefijo '11110' + 32 bits (37b)│
│ • Escape Delta Inicial:               Prefijo '111110'  + 32 bits (38b)│
│ • Escape Absoluto / Reinicio:         Prefijo '111111'  + 64 bits (70b)│
└────────────────────────────────────────────────────────────────────────┘
```

#### Máquina de Estados del Codificador Temporal R15.10:
- **Estado Inicial / Reinicio de Segmento:**  
  `has_prev_delta = false; prev_delta = 0; prev_time = t_start;`  
  Se emite el prefijo de escape absoluto `111111` (6 bits) seguido del valor literal de 64 bits `t_start` (70 bits en total).
- **Muestra Inmediatamente Siguiente ($i = 1$ tras inicio o reinicio):**  
  Se calcula $D_0 = t_1 - t_0$. Se emite el prefijo de escape de delta inicial `111110` (6 bits) seguido de $D_0$ en 32 bits con signo. Se fijan:  
  `has_prev_delta = true; prev_delta = D_0; prev_time = t_1;`
- **Muestras Subsecuentes ($i \ge 2$):**  
  Se calcula el delta-of-delta con signo:
  $$D = (t_i - \text{prev\_time}) - \text{prev\_delta}$$
  Se emite el prefijo correspondiente según el rango de $D$, se escribe el valor codificado y se actualiza:  
  `prev_delta = (t_i - prev_time); prev_time = t_i;`

### 3.2 Protocolo de Reinicio Temporal para C-MAPSS (100 Motores)
Dado que C-MAPSS contiene 100 motores independientes cuyos ciclos operativos recomienzan desde el ciclo 1:
1. Al transicionar del motor $m-1$ al motor $m$, el codificador emite obligatoriamente el prefijo de escape absoluto `111111` seguido del `start_cycle` del motor entrante en 64 bits.
2. Se reinicia estrictamente el estado temporal:
   $$\text{has\_prev\_delta} = \text{false}, \quad \text{prev\_delta} = 0, \quad \text{prev\_time} = \text{segment.start\_cycle}$$
3. Este procedimiento es idéntico e incondicional para los cuatro motores (`Gorilla_Composed`, `Chimp128_Composed`, `BinPack_Transpose`, `Zstd_Composed`).

### 3.3 Delimitación de Carga Útil y Contabilización de Bytes (NC-03)
Para asegurar paridad absoluta sin sobrecostes ocultos ni dobles contabilizaciones:
- **Matriz de Valores:** Para todos los motores, la carga de valores en bruto consiste exactamente en la matriz no comprimida de $N \times K$ flotantes de 64 bits ($N \times K \times 8$ bytes).
- **`Zstd_Composed`:** Comprime exactamente dicha matriz de valores en bruto a nivel 3 (por defecto). Su stream de valores resultante tiene tamaño $B_{\text{values}}$.
- **`Gorilla_Composed` y `Chimp128_Composed`:** Comprimen exactamente la misma secuencia de valores, produciendo su respectivo stream $B_{\text{values}}$.
- **`BinPack_Transpose`:** Transpone la matriz por planos de bytes y la comprime, produciendo su respectivo stream $B_{\text{values}}$.
- **Cómputo Total Idéntico:**
  $$B_{\text{total}} = B_{\text{header}} + B_{\text{meta}} + B_{\text{time}} + B_{\text{values}}$$
  donde $B_{\text{header}} = 64$ bytes y $B_{\text{time}}$ es el flujo temporal R15.10 compartido.

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

### 4.2 Chimp128 F64 Canónico según Figura 8
- **Estructura de Estado con Guarda de Ventana Válida:**
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
  5. Codificación de Bits según Figura 8:
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

5. **Instrumentación Discriminada de Memoria RAM:**
   - $\text{RAM}_{\text{base}}$: Working set previo a la ejecución (`WorkingSetSize`).
   - $\text{RAM}_{\text{peak}}$: Working set pico durante la operación (`PeakWorkingSetSize`).
   - $\Delta \text{RAM} = \text{RAM}_{\text{peak}} - \text{RAM}_{\text{base}}$ (memoria neta del proceso, límite $\le 16\text{ MB}$).
   - $\text{RAM}_{\text{codec}}$: Memoria dinámica asignada exclusivamente por las estructuras internas del códec (límite $\le 2\text{ MB}$).

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
│      TP_comp(BinPack) >= 0.50 * TP_comp(Gorilla)  Y  ΔRAM <= 16 MB.                    │
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
│ Paso 1: Aprobación Formal de R6.3 (Última Revisión Documental de Diseño).              │
│ Paso 2: Fase C (Codificación) en tests/experiments/bp011/ de los motores y arnés.     │
│ Paso 3: Fase D (Auditoría Estática de Arneses y Compilación Segura).                  │
│ Paso 4: Fase E (Campaña Experimental Única sobre C-MAPSS e IMS).                       │
│ Paso 5: Dictamen Comercial: Decisión definitiva (Continuar / Reposicionar / Congelar). │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 9. Conclusión y Petición Formal

El diseño técnico presentado en esta **Revisión R6.3** resuelve con exactitud matemática y rigor pleno los cuatro puntos observados:
1. Timestamp R15.10 literal con prefijos `0`, `10`, `110`, `1110`, `11110`, `111110` y escape absoluto `111111` para transiciones de motores C-MAPSS.
2. Normalización matemática correcta de la fórmula de $\text{Ahorro}_X$ sobre el tamaño del competidor.
3. Delimitación unívoca de `Zstd_Composed` comprimiendo exactamente la misma matriz de valores brutos a nivel 3.
4. Discriminación operativa de memoria (RAM base, RAM pico, $\Delta\text{RAM} \le 16\text{ MB}$ y $\text{RAM}_{\text{codec}} \le 2\text{ MB}$).

Por todo lo expuesto, se solicita formalmente al Auditor Líder:
1. **Aprobación incondicional de la Especificación y Diseño Técnico R6.3 como documento definitivo de la Fase C.**
2. **Autorización formal para iniciar la codificación experimental** exclusivamente en [`tests/experiments/bp011/`](file:///C:/Users/sgi.lm/Desktop/binpack-SDK/tests/experiments/bp011/), manteniendo intacto el *code freeze* en `src/`, `include/` y `deps/`.
