# Especificación y Diseño Técnico R6.2 — Hito BP-011 / Fase C
## Protocolo Decisivo de Validación de Viabilidad Comercial en Telemetría Industrial

**Proyecto:** BinPack Enterprise SDK  
**Hito:** BP-011  
**Fase:** Fase C — Auditoría de Instrumentación y Diseño de Arneses (Revisión Definitiva R6.2)  
**Documento Rector:** `docs/audit/bp011_phase_c_technical_design.md`  
**Rama:** `audit/bp1-core-safety`  
**Fecha:** 12 de noviembre de 2026  
**Estado:** ESPECIFICACIÓN TÉCNICA DEFINITIVA DE DISEÑO — REVISIÓN MÍNIMA DE CIERRE (REVISIÓN R6.2)  
**Gobernanza:** *Code Freeze* preservado al 100% (cero modificaciones en `src/`, `include/` y `deps/`; sin compilación ni ejecución en esta fase).

---

## Control de Cambios de la Revisión R6.2 (Subsanación Focalizada y Definitiva)

En estricta atención al dictamen técnico emitido sobre la Revisión R6.1, la presente **Revisión R6.2** subsana de forma quirúrgica, unívoca e inmediata los seis requerimientos contractuales:

1. **Alineación Estricta del Timestamp con el Protocolo Aprobado R15.10:**  
   Se sustituye la descripción previa por la especificación exacta de R15.10 (prefijos `0`, `10`, `110`, `1110` y escape/reinicio absoluto `1111` con 64 bits). Se formaliza el reinicio temporal incondicional al transicionar entre los 100 motores de C-MAPSS emitiendo el prefijo `1111` y reiniciando el estado del predictor temporal (`has_prev_delta = false`).
2. **Guarda de Ventana Válida en Chimp128 (`has_valid_window`):**  
   Se añade a `chimp128_f64_state_t` el campo booleano `has_valid_window`, inicializado a `false`. Se impide formalmente que el codificador emita un bit de control `0` (reutilización de ventana) antes de que se haya emitido al menos una ventana significativa no nula en el bloque, erradicando el riesgo de emitir ventanas de longitud cero.
3. **Orden Seguro en la Validación de Cabecera:**  
   En `bp011_validate_header`, la inspección de padding (`file_buf[...]`) se traslada estrictamente **después** de comprobar que todos los offsets y longitudes estén confinados dentro de `total_file_bytes`, que `total_file_bytes <= actual_file_size` y que ningún rango activo se solape con otro.
4. **Prevención de Underflow en el Directorio C-MAPSS:**  
   En `bp011_verify_series_directory`, se valida primero incondicionalmente `seg->start_sample_idx <= h->sample_count` antes de evaluar `h->sample_count - seg->start_sample_idx`, eliminando el desbordamiento por resta en enteros sin signo de 32 bits.
5. **Denominación Rigurosa y Honesta de Códecs Compuestos:**  
   Se explicita que al utilizar el contenedor común con timestamps R15.10, los códecs se denominan y reportan como:  
   - `Gorilla_Composed` (Códec de valores Gorilla sobre contenedor común R15.10).  
   - `Chimp128_Composed` (Códec de valores Chimp128 sobre contenedor común R15.10).  
   - `BinPack_Transpose` (Transposición plana por planos de bytes + BinPack sobre contenedor común R15.10).  
   - `Zstd_Composed` (Compresor general Zstd sobre contenedor común).  
   Esto refleja fielmente el aislamiento de la eficiencia de compresión de valores en entornos industriales sin atribuir erróneamente formatos de bitstream ajenos.
6. **Delimitación Operativa de Memoria Pico y Consultas Selectivas:**  
   Se formaliza que la memoria pico se mide mediante contadores del SO (`WorkingSetSize`) acotada a $\le 16\text{ MB}$, y que la consulta selectiva se evalúa midiendo el tiempo y los bytes efectivamente descomprimidos para extraer un único canal $k \in [0, K-1]$ sobre la totalidad de los registros.

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
4. **Cierre de Ciclo Documental:** R6.2 es la última revisión documental. Tras su aprobación, se inicia la codificación.

---

## 2. Matriz de Competidores y Corpus Mínimo Certificado

### 2.1 Matriz de Competidores (Denominación Compuesta)
Para aislar de forma científicamente rigurosa la eficiencia del algoritmo de valores sobre telemetría industrial multicanal, todos los motores se integran sobre el contenedor industrial común con timestamps R15.10:
1. **`Gorilla_Composed`:** Códec de valores Gorilla F64 (Pelkonen et al. 2015) sobre contenedor R15.10.
2. **`Chimp128_Composed`:** Códec de valores Chimp128 F64 (Liakos et al. VLDB 2022) sobre contenedor R15.10.
3. **`BinPack_Transpose`:** Nuestra propuesta central (transposición por planos de bytes + códec BinPack con acceso selectivo).
4. **`Zstd_Composed`:** Baseline general secundario consolidado en la industria.

### 2.2 Corpus Mínimo Certificado (Fase B)
1. **NASA Turbofan C-MAPSS (FD001):** 24 variables físicas y operativas por registro (3 operacionales + 21 sensores), 20.631 registros temporales repartidos en 100 motores discontinuos.
2. **NASA Bearing IMS:** 48 características estadísticas derivadas de condición (media, RMS, kurtosis, etc., 4 rodamientos $\times$ 12 métricas), 2.156 ventanas temporales continuas.

---

## 3. Contrato Temporal Común según Protocolo R15.10 (NC-R6-01 y NC-R6-05)

### 3.1 Especificación Exacta de Timestamps según R15.10
Todos los motores comprimen timestamps de 64 bits (`int64_t`) utilizando la codificación aprobada de R15.10:
- **Timestamp Inicial / Escape de Reinicio:** Prefijo `1111` seguido de 64 bits literales (`uint64_t`). Se utiliza obligatoriamente para la primera muestra del archivo y en cada reinicio de segmento/motor.
- **Delta Cero ($\Delta = 0$):** Prefijo `0` (1 bit).
- **Delta Pequeño ($1 \le \Delta \le 255$):** Prefijo `10` + 8 bits de carga útil (10 bits).
- **Delta Mediano ($256 \le \Delta \le 65.535$):** Prefijo `110` + 16 bits de carga útil (19 bits).
- **Delta Grande ($65.536 \le \Delta \le 2^{32}-1$):** Prefijo `1110` + 32 bits de carga útil (36 bits).
- **Delta Irregular / Negativo / Salto de Motor:** Prefijo `1111` + 64 bits literales del nuevo timestamp absoluto (68 bits en total).

### 3.2 Protocolo de Reinicio Temporal para C-MAPSS (100 Motores)
En C-MAPSS, cada uno de los 100 motores reinicia su contador de ciclo operativo en el ciclo 1. Al cruzar el límite entre el motor $m-1$ y el motor $m$:
1. El codificador temporal emite obligatoriamente el prefijo de reinicio `1111` seguido de `start_cycle` en 64 bits.
2. Se reinicia incondicionalmente el estado del predictor temporal:
   $$\text{has\_prev\_delta} = \text{false}, \quad \text{prev\_delta} = 0, \quad \text{prev\_time} = \text{segment.start\_cycle}$$
3. Este procedimiento se ejecuta de forma idéntica y simultánea en los cuatro motores evaluados (`Gorilla_Composed`, `Chimp128_Composed`, `BinPack_Transpose`, `Zstd_Composed`), asegurando una paridad de carga útil de 100%.

### 3.3 Cómputo Total de Tamaño Comprimido
El tamaño final de bloque $B_{\text{total}}$ contabiliza todos los bytes para todos los motores:
$$B_{\text{total}} = B_{\text{header}} + B_{\text{meta}} + B_{\text{time}} + B_{\text{values}}$$

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

### 4.2 Chimp128 F64 Canónico según Figura 8 (NC-R6-02)
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

## 5. Contrato Binario y Garantías de Seguridad de R5 (NC-R6-03 y NC-R6-04)

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
Para evitar lecturas fuera de buffer, **las lecturas de padding se ejecutan estrictamente después de validar todos los límites y solapamientos**:

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

    // 1. Rechazo de flags desconocidos
    if ((h->flags & ~BP011_KNOWN_FLAGS) != 0) return BP011_ERR_UNKNOWN_FLAGS;

    // 2. Presencia obligatoria de streams
    if (h->sample_count == 0 || h->num_channels == 0) return BP011_ERR_INVALID_ARG;
    if (h->time_stream_bytes == 0 || h->time_stream_bits == 0) return BP011_ERR_INVALID_ARG;
    if (h->values_stream_bytes == 0 || h->values_stream_bits == 0) return BP011_ERR_INVALID_ARG;

    // 3. Coherencia matemática bits/bytes
    if (h->time_stream_bits > (uint64_t)h->time_stream_bytes * 8ULL) return BP011_ERR_INVALID_ARG;
    if (h->values_stream_bits > (uint64_t)h->values_stream_bytes * 8ULL) return BP011_ERR_INVALID_ARG;

    // 4. Secciones opcionales vinculadas a flags
    bool has_chan = (h->flags & BP011_FLAG_HAS_CHANNEL_TABLE) != 0;
    if (!has_chan && (h->channel_table_offset != 0 || h->channel_table_bytes != 0)) return BP011_ERR_INVALID_OFFSET;
    if (has_chan && (h->channel_table_offset < BP011_HEADER_SIZE_BYTES || h->channel_table_bytes == 0)) return BP011_ERR_INVALID_OFFSET;

    bool has_series = (h->flags & BP011_FLAG_HAS_SERIES_DIR) != 0;
    if (!has_series && (h->series_dir_offset != 0 || h->series_dir_bytes != 0)) return BP011_ERR_INVALID_OFFSET;
    if (has_series && (h->series_dir_offset < BP011_HEADER_SIZE_BYTES || h->series_dir_bytes == 0)) return BP011_ERR_INVALID_OFFSET;

    // 5. Validación previa de límites y ausencia de overflow antes de leer cualquier byte
    #define CHECK_BOUNDS(off, bytes) do {         if ((off) < BP011_HEADER_SIZE_BYTES) return BP011_ERR_INVALID_OFFSET;         if ((off) > h->total_file_bytes) return BP011_ERR_INVALID_OFFSET;         if ((bytes) > h->total_file_bytes - (off)) return BP011_ERR_INVALID_OFFSET;     } while (0)

    if (has_chan) CHECK_BOUNDS(h->channel_table_offset, h->channel_table_bytes);
    if (has_series) CHECK_BOUNDS(h->series_dir_offset, h->series_dir_bytes);
    CHECK_BOUNDS(h->time_stream_offset, h->time_stream_bytes);
    CHECK_BOUNDS(h->values_stream_offset, h->values_stream_bytes);

    // 6. Validación de no solapamiento por pares
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

    // 7. Inspección de padding seguro (ejecutado estrictamente tras validar límites)
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

        // Comprobación segura contra underflow antes de restar
        if (seg->start_sample_idx > h->sample_count) return BP011_ERR_INVALID_OFFSET;
        if (seg->sample_count > h->sample_count - seg->start_sample_idx) return BP011_ERR_INVALID_ARG;

        if (seg->start_sample_idx < expected_sample_cursor) return BP011_ERR_OVERLAPPING_SEGMENT;
        expected_sample_cursor = seg->start_sample_idx + seg->sample_count;

        // Confinamiento en streams temporales y de valores
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

## 6. Fórmulas Matemáticas y Criterios Comerciales

### 6.1 Fórmulas Analíticas Exactas
1. **Ratio de Compresión ($CR$):**
   $$CR = \frac{B_{\text{raw}}}{B_{\text{total}}}$$
   donde $B_{\text{raw}} = N \times (8 + K \times 8)$ bytes para tuplas F64 con timestamps de 64 bits.
   
   **Mejora Porcentual frente al Baseline $X$ ($\Delta CR_X$):**
   $$\Delta CR_X = \left( \frac{B_{\text{total}, X} - B_{\text{total}, \text{BinPack}}}{B_{\text{total}, \text{BinPack}}} \right) \times 100\%$$

2. **Throughputs Computacionales:**
   $$TP_{\text{comp}} = \frac{B_{\text{raw}}}{10^6 \times T_{\text{comp}}} \quad (\text{MB/s}), \qquad TP_{\text{decomp}} = \frac{B_{\text{raw}}}{10^6 \times T_{\text{decomp}}} \quad (\text{MB/s})$$

3. **Eficiencia de Consulta Selectiva de un Canal:**
   - Speedup frente a `Gorilla_Composed`:
     $$S_{\text{select}} = \frac{T_{\text{select}, \text{Gorilla}}}{T_{\text{select}, \text{BinPack}}}$$
   - Amplificación de Descompresión:
     $$A_{\text{decomp}} = \frac{B_{\text{decomp}}}{N \times 8}$$

4. **Huella de Memoria RAM Pico:** Medida mediante `GetProcessMemoryInfo` (`WorkingSetSize` en Windows). Límite contractual:
   $$\text{RAM}_{\text{peak}} \le 16 \text{ MB}$$

### 6.2 Criterios Inmutables de Decisión Comercial

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                         CRITERIOS DE DECISIÓN COMERCIAL BP-011                         │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ 1. [SEGUIR DESARROLLANDO] Ventaja Comercial Demostrada:                                │
│    • ΔCR_Gorilla >= 15.0%  Y  ΔCR_Chimp128 >= 15.0%                                    │
│      O BIEN                                                                            │
│    • S_select >= 2.0x frente a Gorilla Y A_decomp <= 2.0                               │
│      CONDICIÓN COMPLEMENTARIA:                                                         │
│      TP_comp(BinPack) >= 0.50 * TP_comp(Gorilla)  Y  RAM_peak <= 16 MB.                │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ 2. [REPOSICIONAR] Ventaja de Nicho Especializado:                                      │
│    • BinPack no alcanza +15% de compresión global, pero demuestra S_select >= 3.0x     │
│      en C-MAPSS (K=24) o IMS (K=48) con A_decomp <= 1.5, posibilitando un motor de    │
│      consultas analíticas directas por canal sin descompresión masiva.                 │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ 3. [DESCARTAR / CONGELAR] Sin Ventaja Demostrada:                                      │
│    • BinPack no supera a Gorilla/Chimp128 en compresión (ΔCR < 5%), y S_select < 1.5x. │
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
│ Paso 1: Aprobación Formal de R6.2 (Última Revisión Documental de Diseño).              │
│ Paso 2: Fase C (Codificación) en tests/experiments/bp011/ de los motores y arnés.     │
│ Paso 3: Fase D (Auditoría Estática de Arneses y Compilación Segura).                  │
│ Paso 4: Fase E (Campaña Experimental Única sobre C-MAPSS e IMS).                       │
│ Paso 5: Dictamen Comercial: Decisión definitiva (Continuar / Reposicionar / Congelar). │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 9. Conclusión y Petición Formal

El diseño técnico presentado en esta **Revisión R6.2** subsana la totalidad de las observaciones bloqueantes:
1. Timestamp estrictamente alineado con R15.10 y reinicio explícito en los 100 motores de C-MAPSS.
2. Guarda `has_valid_window` en Chimp128 impidiendo la reutilización de ventanas inexistentes.
3. Validación de cabecera en orden seguro: límites previos a la lectura de padding.
4. Guarda incondicional anti-underflow en el directorio de series C-MAPSS.
5. Denominación honesta de los códecs como compuestos sobre contenedor R15.10.
6. Delimitación unívoca de memoria RAM pico y consulta selectiva.

Por todo lo expuesto, se solicita formalmente al Auditor Líder:
1. **Aprobación incondicional de la Especificación y Diseño Técnico R6.2 como documento definitivo de la Fase C.**
2. **Autorización formal para iniciar la codificación experimental** exclusivamente en [`tests/experiments/bp011/`](file:///C:/Users/sgi.lm/Desktop/binpack-SDK/tests/experiments/bp011/), manteniendo intacto el *code freeze* en `src/`, `include/` y `deps/`.
