# Especificación y Diseño Técnico R6.1 — Hito BP-011 / Fase C
## Protocolo Decisivo de Validación de Viabilidad Comercial en Telemetría Industrial

**Proyecto:** BinPack Enterprise SDK  
**Hito:** BP-011  
**Fase:** Fase C — Auditoría de Instrumentación y Diseño de Arneses (Revisión Definitiva R6.1)  
**Documento Rector:** `docs/audit/bp011_phase_c_technical_design.md`  
**Rama:** `audit/bp1-core-safety`  
**Fecha:** 12 de noviembre de 2026  
**Estado:** ESPECIFICACIÓN TÉCNICA DEFINITIVA DE DISEÑO — ÚLTIMA REVISIÓN DOCUMENTAL (REVISIÓN R6.1)  
**Gobernanza:** *Code Freeze* preservado al 100% (cero modificaciones en `src/`, `include/` y `deps/`; sin compilación ni ejecución en esta fase).

---

## Control de Cambios de la Revisión R6.1 (Cierre Definitivo de Contratos)

En cumplimiento estricto del dictamen de auditoría sobre R6, esta **Revisión R6.1** cierra de forma unívoca, matemática y verificable los cuatro puntos contractuales finales:

1. **NC-R6-01 (Chimp128 Canónico con Ciclo de Vida Completo):**
   - Se formaliza el ciclo de vida completo: inicialización de las 16.384 entradas de `hash_table` a -1, `ring_buf[128]` a 0, `ring_sample_idx[128]` a 0, y estado previo al inicio del bloque.
   - Tratamiento explícito del primer valor ($v_0$) emitido como literal de 64 bits IEEE 754.
   - Reglas de actualización del estado (`ring_buf`, `ring_sample_idx`, `hash_table`, `prev_val`) en cada muestra.
   - Mapeo unívoco de las ramas de la Figura 8 de Liakos et al. (VLDB 2022).

2. **NC-R6-02 (Contrato Temporal Común y Paridad Estricta de Carga Útil):**
   - Se formaliza el contrato temporal idéntico para los cuatro motores evaluados (`Gorilla`, `Chimp128`, `BinPack_Transpose`, `Zstd`): todos comprimen y reconstruyen la tupla completa $(t_i, v_{i, 0}, \dots, v_{i, K-1})$.
   - Los timestamps se representan como enteros de 64 bits (`int64_t`, microsegundos o ciclos enteros) codificados mediante el protocolo R15.10.
   - El cómputo del tamaño comprimido total contabiliza incondicionalmente todos los bytes:
     $$B_{\text{total}} = B_{\text{header}} + B_{\text{meta}} + B_{\text{time}} + B_{\text{values}}$$

3. **NC-R6-03 (Recuperación Íntegra de las Garantías de Seguridad de R5):**
   - Se restaura íntegramente la implementación en C de `bp011_validate_header` y `bp011_verify_series_directory`.
   - Se garantizan comprobaciones contra desbordamiento aritmético, validación obligatoria de presencia de streams (`bytes > 0`, `bits > 0`), consistencia $\text{bits} \le 8 \times \text{bytes}$, verificación de padding en cero, rechazo estricto de flags desconocidos y algoritmo de no solapamiento por pares.

4. **NC-R6-04 (Métricas Comerciales Formales y Diferenciación de Datasets):**
   - Fórmulas analíticas exactas para Ratio de Compresión ($CR$), Mejora Porcentual ($\Delta CR$), Throughput de Ingestión ($TP_{\text{comp}}$), Throughput de Recuperación ($TP_{\text{decomp}}$), Latencia de Consulta Selectiva ($T_{\text{select}}$), Speedup ($S_{\text{select}}$), Amplificación ($A_{\text{decomp}}$) y Límite de Memoria RAM ($\le 16\text{ MB}$).
   - Distinción taxonómica formal entre los 24 canales de variables físicas operativas de C-MAPSS y las 48 características estadísticas derivadas de condición de IMS Bearings.

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
4. **Cierre de Ciclo Documental:** R6.1 es la última revisión documental. Tras su aprobación, se inicia la codificación.

---

## 2. Matriz de Competidores y Corpus Mínimo Certificado

### 2.1 Matriz de Competidores
1. **Gorilla (Pelkonen et al. 2015):** Referencia fundacional especializada en series temporales.
2. **Chimp128 (Liakos et al. VLDB 2022):** Referencia moderna de compresión temporal de alta densidad.
3. **BinPack Transpose:** Nuestra propuesta central (transposición por planos de bytes + códec BinPack con acceso selectivo).
4. **Zstd (Baseline Secundario):** Compresor general consolidado en la industria.

### 2.2 Corpus Mínimo Certificado (Fase B)
1. **NASA Turbofan C-MAPSS (FD001):** 24 variables físicas y operativas por registro (3 operacionales + 21 sensores), 20.631 registros temporales repartidos en 100 motores.
2. **NASA Bearing IMS:** 48 características estadísticas derivadas de condición (media, RMS, kurtosis, etc., 4 rodamientos $\times$ 12 métricas), 2.156 ventanas temporales continuas.

---

## 3. Contrato Temporal Común y Paridad de Carga Útil (NC-R6-02)

Para garantizar una comparación simétrica y equitativa, **los cuatro motores comprimen y reconstruyen exactamente la misma información**:

### 3.1 Definición de la Tupla de Datos
Cada registro $i$ ($0 \le i < N$) contiene:
$$\mathcal{T}_i = \left( t_i, \ v_{i, 0}, \ v_{i, 1}, \ \dots, \ v_{i, K-1} \right)$$
- $t_i \in \text{int64\_t}$: Timestamp en microsegundos (IMS) o ciclo operativo entero (C-MAPSS).
- $v_{i, k} \in \text{double}$ (IEEE 754 Float64): Valor físico o estadístico del canal $k$.

### 3.2 Codificación Temporal Unificada R15.10
Todos los motores utilizan el formato temporal canónico de R15.10:
- Primer timestamp ($t_0$): literal de 64 bits (`int64_t`).
- Primer delta ($D_0 = t_1 - t_0$): codificado en 14 bits.
- Deltas subsecuentes ($D_i = t_i - t_{i-1}$): codificados mediante delta-of-delta ($\Delta_i = D_i - D_{i-1}$) con el prefijo binario de R15.10:
  - $\Delta = 0$: bit `0` (1 bit).
  - $-63 \le \Delta \le 64$: bits `10` + 7 bits (9 bits).
  - $-255 \le \Delta \le 256$: bits `110` + 9 bits (12 bits).
  - $-2047 \le \Delta \le 2048$: bits `1110` + 12 bits (16 bits).
  - Fuera de rango: bits `1111` + 32 bits (36 bits).

### 3.3 Cómputo Estricto de Carga Útil Comprimida
El tamaño comprimido total $B_{\text{total}}$ para **todos los motores sin excepción** se calcula como:
$$B_{\text{total}} = B_{\text{header}} + B_{\text{meta}} + B_{\text{time}} + B_{\text{values}}$$
- $B_{\text{header}} = 64$ bytes (cabecera fija Little-Endian).
- $B_{\text{meta}}$: Bytes de la tabla de canales (M7) o directorio de series (C-MAPSS).
- $B_{\text{time}}$: Bytes ocupados por el stream temporal R15.10.
- $B_{\text{values}}$: Bytes ocupados por el stream de valores del motor respectivo.

---

## 4. Especificación Rigurosa de Códecs de Valores

### 4.1 Gorilla F64 Canónico (Pelkonen et al. 2015)
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

### 4.2 Chimp128 Canónico (Liakos et al., VLDB 2022, Figura 8) (NC-R6-01)
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
  }
  ```

- **Primer Valor ($v_0$):**
  Se escribe como literal de 64 bits (`uint64_t`). Se inicializa:
  `st->prev_val = v_0; st->ring_buf[0] = v_0; st->ring_sample_idx[0] = 0; st->hash_table[v_0 & 0x3FFF] = 0;`

- **Muestras Subsecuentes ($v_i, i \ge 1$):**
  1. Extraer clave hash: $\text{key} = \text{as\_uint64}(v_i) \ \& \ 0x3FFF$.
  2. Consultar candidato: $c = \text{st->hash\_table}[\text{key}]$.
  3. Validar ventana inclusiva:
     $$\text{is\_valid\_cand} = (c \ge 0) \ \land \ (1 \le (i - c) \le 128)$$
  4. Decisión de Umbral Estricto:
     Como coincidencia en clave garantiza $tz_c \ge 14 > 13$, si $\text{is\_valid\_cand}$ es verdadero, se selecciona incondicionalmente la referencia histórica ($v_{\text{ref}} = \text{st->ring\_buf}[c \bmod 128]$). Si no, $v_{\text{ref}} = \text{st->prev\_val}$.
  5. Codificación de Bits según Figura 8:
     - **Si usa referencia histórica:** Emite bit `1` + 7 bits de índice ($c \bmod 128$).
     - **Si usa referencia precedente:** Emite bit `0` (sin índice).
     - **Codificación del XOR ($XOR = v_i \oplus v_{\text{ref}}$):**
       - Si $XOR == 0$: emite bit `0`.
       - Si $XOR \ne 0$: emite bit `1`.  
         $lz = \text{clz64}(XOR)$, $tz = \text{ctz64}(XOR)$, $lz_{\text{quant}} = \min(7, \lfloor lz / 8 \rfloor)$.
         - Si $lz_{\text{quant}} == \text{st->prev\_lz\_quant} \land tz == \text{st->prev\_tz}$:
           Emite bit `0` + bits centrales de longitud $\text{st->prev\_len}$.
         - En otro caso:
           Emite bit `1` + $lz_{\text{quant}}$ (3b) + $(\text{len} - 1)$ (6b) + $\text{len}$ bits significativos, donde $\text{len} = 64 - (lz_{\text{quant}} \times 8) - tz$.  
           Actualiza $\text{st->prev\_lz\_quant} = lz_{\text{quant}}$, $\text{st->prev\_tz} = tz$, $\text{st->prev\_len} = \text{len}$.
  6. Actualización obligatoria de estado:
     `st->ring_buf[i % 128] = v_i; st->ring_sample_idx[i % 128] = i; st->hash_table[key] = i; st->prev_val = v_i;`

---

## 5. Contrato Binario y Garantías de Seguridad de R5 (NC-R6-03)

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

### 5.2 Algoritmo de Validación Incondicional de Cabecera y No Solapamiento
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

    // 1. Rechazo estricto de flags desconocidos
    if ((h->flags & ~BP011_KNOWN_FLAGS) != 0) return BP011_ERR_UNKNOWN_FLAGS;

    // 2. Presencia obligatoria de streams de datos
    if (h->sample_count == 0 || h->num_channels == 0) return BP011_ERR_INVALID_ARG;
    if (h->time_stream_bytes == 0 || h->time_stream_bits == 0) return BP011_ERR_INVALID_ARG;
    if (h->values_stream_bytes == 0 || h->values_stream_bits == 0) return BP011_ERR_INVALID_ARG;

    // 3. Coherencia bits/bytes
    if (h->time_stream_bits > (uint64_t)h->time_stream_bytes * 8ULL) return BP011_ERR_INVALID_ARG;
    if (h->values_stream_bits > (uint64_t)h->values_stream_bytes * 8ULL) return BP011_ERR_INVALID_ARG;

    // 4. Verificación de padding en ceros en el último byte
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

    // 5. Coherencia de secciones opcionales vinculadas a flags
    bool has_chan = (h->flags & BP011_FLAG_HAS_CHANNEL_TABLE) != 0;
    if (!has_chan && (h->channel_table_offset != 0 || h->channel_table_bytes != 0)) return BP011_ERR_INVALID_OFFSET;
    if (has_chan && (h->channel_table_offset < BP011_HEADER_SIZE_BYTES || h->channel_table_bytes == 0)) return BP011_ERR_INVALID_OFFSET;

    bool has_series = (h->flags & BP011_FLAG_HAS_SERIES_DIR) != 0;
    if (!has_series && (h->series_dir_offset != 0 || h->series_dir_bytes != 0)) return BP011_ERR_INVALID_OFFSET;
    if (has_series && (h->series_dir_offset < BP011_HEADER_SIZE_BYTES || h->series_dir_bytes == 0)) return BP011_ERR_INVALID_OFFSET;

    // 6. Validación de límites sin sumas desbordables
    #define CHECK_BOUNDS(off, bytes) do {         if ((off) < BP011_HEADER_SIZE_BYTES) return BP011_ERR_INVALID_OFFSET;         if ((off) > h->total_file_bytes) return BP011_ERR_INVALID_OFFSET;         if ((bytes) > h->total_file_bytes - (off)) return BP011_ERR_INVALID_OFFSET;     } while (0)

    if (has_chan) CHECK_BOUNDS(h->channel_table_offset, h->channel_table_bytes);
    if (has_series) CHECK_BOUNDS(h->series_dir_offset, h->series_dir_bytes);
    CHECK_BOUNDS(h->time_stream_offset, h->time_stream_bytes);
    CHECK_BOUNDS(h->values_stream_offset, h->values_stream_bytes);

    // 7. Validación de no solapamiento por pares
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
    return BP011_OK;
}
```

### 5.3 Directorio de Series C-MAPSS (32 Bytes / Segmento) y No Solapamiento
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

        if (seg->start_sample_idx < expected_sample_cursor) return BP011_ERR_OVERLAPPING_SEGMENT;
        if (seg->sample_count > h->sample_count - seg->start_sample_idx) return BP011_ERR_INVALID_ARG;
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

## 6. Fórmulas Matemáticas de Métricas y Criterios Comerciales (NC-R6-04)

### 6.1 Fórmulas Analíticas Exactas
1. **Ratio de Compresión ($CR$):**
   $$CR = \frac{B_{\text{raw}}}{B_{\text{total}}}$$
   donde $B_{\text{raw}} = N \times (8 + K \times 8)$ bytes para tuplas F64 con timestamps de 64 bits.
   
   **Mejora Porcentual frente al Baseline $X$ ($\Delta CR$):**
   $$\Delta CR_X = \left( \frac{CR_{\text{BinPack}} - CR_X}{CR_X} \right) \times 100\% = \left( \frac{B_{\text{total}, X} - B_{\text{total}, \text{BinPack}}}{B_{\text{total}, \text{BinPack}}} \right) \times 100\%$$

2. **Throughput de Ingestión / Compresión ($TP_{\text{comp}}$):**
   $$TP_{\text{comp}} = \frac{B_{\text{raw}}}{10^6 \times T_{\text{comp}}} \quad (\text{MB/s})$$

3. **Throughput de Recuperación / Descompresión ($TP_{\text{decomp}}$):**
   $$TP_{\text{decomp}} = \frac{B_{\text{raw}}}{10^6 \times T_{\text{decomp}}} \quad (\text{MB/s})$$

4. **Eficiencia de Consulta Selectiva (Sensor Individual $k$):**
   - Latencia: $T_{\text{select}}(k)$ en milisegundos.
   - Speedup de Consulta frente a Gorilla:
     $$S_{\text{select}} = \frac{T_{\text{select}, \text{Gorilla}}}{T_{\text{select}, \text{BinPack}}}$$
   - Amplificación de Descompresión:
     $$A_{\text{decomp}} = \frac{B_{\text{decomp}}}{B_{\text{target}}} = \frac{B_{\text{decomp}}}{N \times 8}$$
     *(En Gorilla entrelazado $A_{\text{decomp}} \approx K$; en BinPack Transpose selectivo $A_{\text{decomp}} \approx 1.0$).*

5. **Huella de Memoria Pico (RAM):**
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

1. **Consulta Dinámica Asociada al Volumen del Archivo:**  
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
│ Paso 1: Aprobación Formal de R6.1 (Última Revisión Documental de Diseño).              │
│ Paso 2: Fase C (Codificación) en tests/experiments/bp011/ de los 3 motores y arnés.  │
│ Paso 3: Fase D (Auditoría Estática de Arneses y Compilación Segura).                  │
│ Paso 4: Fase E (Campaña Experimental Única sobre C-MAPSS e IMS).                       │
│ Paso 5: Dictamen Comercial: Decisión definitiva (Continuar / Reposicionar / Congelar). │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 9. Conclusión y Petición Formal

El diseño técnico presentado en esta **Revisión R6.1** cierra todos los contratos pendientes:
1. Ciclo de vida e inicialización completa de Chimp128 canónico según Figura 8.
2. Contrato temporal idéntico R15.10 y cómputo simétrico de carga útil completa para los cuatro motores.
3. Restauración íntegra de las comprobaciones de seguridad y no solapamiento de R5.
4. Fórmulas analíticas exactas y umbrales comerciales objetivos para decidir el futuro de BinPack.

Por todo lo expuesto, se solicita formalmente al Auditor Líder:
1. **Aprobación incondicional de la Especificación y Diseño Técnico R6.1 como documento definitivo de la Fase C.**
2. **Autorización formal para iniciar la codificación experimental** exclusivamente en [`tests/experiments/bp011/`](file:///C:/Users/sgi.lm/Desktop/binpack-SDK/tests/experiments/bp011/), manteniendo intacto el *code freeze* en `src/`, `include/` y `deps/`.
