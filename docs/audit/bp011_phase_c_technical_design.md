# Especificación y Diseño Técnico R4 — Hito BP-011 / Fase C
## Arquitectura de Instrumentación, Baselines Composicionales y Arnés de Medición

**Proyecto:** BinPack Enterprise SDK  
**Hito:** BP-011  
**Fase:** Fase C — Auditoría de Instrumentación y Diseño de Arneses (Revisión R4)  
**Documento Rector:** `docs/audit/bp011_phase_c_technical_design.md`  
**Rama:** `audit/bp1-core-safety`  
**Fecha:** 12 de noviembre de 2026  
**Estado:** ESPECIFICACIÓN TÉCNICA DEFINITIVA DE DISEÑO DE FASE C (REVISIÓN R4)  
**Gobernanza:** *Code Freeze* preservado al 100% (cero modificaciones en `src/`, `include/` y `deps/`; sin compilación ni ejecución en esta fase).

---

## Control de Cambios de la Revisión R4 (Subsanación Integral de Observaciones)

En estricta conformidad con el dictamen técnico de auditoría emitido sobre la Revisión R3, la presente **Revisión R4** incorpora las siguientes correcciones matemáticas, algorítmicas y de seguridad:

1. **NC-C01 (Máquina de Estados Canónica de Gorilla F64):**  
   Se formaliza la máquina de estados completa de Gorilla para valores (Pelkonen et al., VLDB 2015, Sección 4.1.2), especificando explícitamente:
   - Caso XOR = 0 (1 bit `0`).
   - Caso XOR ≠ 0 (bit `1`) con bifurcación en:
     * Subcaso reutilización de ventana precedente (bit de control `0` + emisión exclusiva de bits significativos sobre la longitud previa $\text{prev\_len}$, sin retransmitir posiciones).
     * Subcaso nueva ventana (bit de control `1` + 5 bits para ceros iniciales + 6 bits para longitud significativa + bits significativos).

2. **NC-C02 (Chimp128 Canónico Estricto según Figura 8 de Liakos et al., VLDB 2022):**  
   Se adopta con fidelidad total el diagrama de flujo y la lógica de codificación de la Figura 8 del paper original:
   - **Distancia de Ventana Inclusiva:** Condición de pertenencia a la ventana de 128 muestras: $1 \le (i - c) \le 128$ (distancia inclusiva hasta 128 muestras, corrigiendo la exclusión indebida previa).
   - **Criterio de Selección por Umbral de Ceros Finales (Trailing Zeros):** Para el candidato de la tabla hash de $2^{14}$ entradas, la referencia histórica solo se selecciona si el número de ceros finales $tz_c \ge 13$ (compensando los 7 bits del índice más los 6 bits de la longitud significativa) o si $XOR_c = 0$.
   - **Estructura de Bits Canónica:** Se implementa el bit de control de referencia (bit `0` para valor precedente $v_{i-1}$, bit `1` para referencia histórica $v_c$ seguido inmediatamente de los 7 bits del índice), desacoplado de la codificación del XOR.

3. **NC-C03 (Corrección del Contrato Binario, Cabecera de 64 Bytes, No Solapamiento y Secciones Opcionales):**  
   - **Tamaño Real de Cabecera:** Se corrige el cálculo y se fija `#define BP011_HEADER_SIZE_BYTES 64` (offset 0x00 a 0x3F, 64 bytes exactos).
   - **Validación Bidireccional de No Solapamiento:** Algoritmo formal de comprobación par a par que verifica que ningún rango activo $[O_A, O_A + L_A)$ se intersecte con $[O_B, O_B + L_B)$ ni con la cabecera fija $[0, 64)$.
   - **Gestión Estricta de Secciones Opcionales:** Se vincula la validación a los flags (`BP011_FLAG_HAS_CHANNEL_TABLE` y `BP011_FLAG_HAS_SERIES_DIR`). Si un flag está inactivo, se exige estrictamente `offset == 0` y `bytes == 0`. Si está activo, se exige `offset >= 64` y `bytes > 0`.

4. **NC-C04 (Validación de Límites y Rangos para Directorio C-MAPSS):**  
   Se añade al contrato la validación incondicional de que cada entrada `bp011_series_segment_t` posea sus desplazamientos de stream (`time_stream_byte_offset` y `values_stream_byte_offset`) confinados estrictamente dentro de los límites de sus respectivos flujos declarados en la cabecera, verificando monotonicidad y ausencia de solapamiento inter-segmento.

5. **NC-C05A (Seguridad Aritmética Absoluta en Bitstream):**  
   Se reincorpora la guarda incondicional contra desbordamiento en la inicialización:
   $$\text{cap\_bytes} \le \lfloor \text{UINT64\_MAX} / 8 \rfloor, \qquad \text{size\_bytes} \le \lfloor \text{UINT64\_MAX} / 8 \rfloor$$
   garantizando que la multiplicación por 8 jamás desborde un entero de 64 bits. Todas las rutinas de lectura devuelven `bp011_status_t` con asignación en punteros de salida.

6. **NC-C05B (Alineación Real de I/O en Windows y Protocolo Cold-Cache):**  
   - Se elimina la suposición de 4096 bytes fijos. El arnés consulta dinámicamente los requisitos de alineación del dispositivo mediante `IOCTL_STORAGE_QUERY_PROPERTY` con `StorageAccessAlignmentProperty` (`BytesPerPhysicalSector`, `BytesPerLogicalSector`), con fallback a `GetDiskFreeSpaceA`.
   - Se especifican las fórmulas exactas para redondeo sectorial de lecturas (`FileOffset` alineado hacia abajo, tamaño alineado hacia arriba a múltiplos de sector) y la gestión del bloque final de archivo.

---

## 1. Introducción, Mandato y Principios Rectores

El presente documento establece la especificación formal de ingeniería para la instrumentación experimental del benchmark de series temporales industriales (Hito BP-011).

### 1.1 Principios Rectores Innegociables
1. **Simetría y Paridad Estricta de Carga Útil:** Todos los motores evaluados comprimen, encapsulan y reconstruyen exactamente las mismas tuplas $(t_i, v_{i, 0}, \dots, v_{i, K-1})$ con sus metadatos y delimitadores de bloque.
2. **Aislamiento Experimental Absoluto:** Todo el código experimental reside exclusivamente en [`tests/experiments/bp011/`](file:///C:/Users/sgi.lm/Desktop/binpack-SDK/tests/experiments/bp011/). El código productivo en `src/`, `include/` y librerías en `deps/` permanece bajo estricto congelamiento (*code freeze*).
3. **Seguridad y Contención de Memoria:** Todo el manejo de flujos de bits se implementa con comprobaciones libres de sumas y multiplicaciones potencialmente desbordables.
4. **Cero Compilación y Cero Ejecución Prematura:** Durante la Fase C únicamente se diseña y codifica el arnés. La compilación y ejecución experimental se reservan para las Fases D y E.

---

## 2. Arquitectura de Baselines y Máquina de Estados Gorilla (NC-C01)

### 2.1 Niveles de Evaluación Experimental
```
┌────────────────────────────────────────────────────────────────────────────────────────────────┐
│                         ARQUITECTURA DE EVALUACIÓN EXPERIMENTAL (BP-011 R4)                    │
├────────────────────────────────────────────────────────────────────────────────────────────────┤
│  NIVEL 1.A: Baselines Nativos Canónicos de Literatura (Float64 / Doble Precisión)              │
│    • M1_Gorilla_Canonical_F64: Pelkonen et al. (2015) [t_ref 64b + D0 14b + DoD + F64 Gorilla]│
│    • M2_Chimp_Canonical_F64:   Liakos et al. (2022) [Gorilla t_ref 64b + F64 Chimp]           │
│    • M3_Chimp128_Canonical_F64:Liakos et al. (2022) [Gorilla t_ref 64b + F64 Chimp128 (2^14)] │
├────────────────────────────────────────────────────────────────────────────────────────────────┤
│  NIVEL 1.B: Variantes Adaptadas de Precisión Simple (Float32)                                  │
│    • Gorilla_Float32_Variant:  Adaptación a Float32 con timestamp 32/14/DoD                    │
│    • Chimp_Float32_Variant:    Chimp32 según Liakos et al. (2022) sobre Float32                │
│    • Chimp128_Float32_Variant: Chimp128 adaptado a Float32 (ventana 128, tabla hash 2^14)     │
├────────────────────────────────────────────────────────────────────────────────────────────────┤
│  NIVEL 2: Baselines Normalizados Composicionales (Protocolo Industrial R15.10)                 │
│    • Timestamp Unificado: int64_t microsegundos con codificación R15.10 (prefijos 0/10/110...) │
│    • Códecs de Valores Evaluados:                                                              │
│        - Gorilla_Normalized, Chimp_Normalized, Chimp128_Normalized                             │
│        - BinPack_Raw, BinPack_Transpose_Scalar, BinPack_Transpose_SIMD                         │
│        - BinPack_Channel_Dedicated (M7) con saltos independientes                              │
└────────────────────────────────────────────────────────────────────────────────────────────────┘
```

### 2.2 Máquina de Estados Canónica de Gorilla F64 (Pelkonen et al. 2015)
El códec Gorilla de valores opera sobre palabras IEEE 754 Float64 (`uint64_t`) mediante la siguiente máquina de estados canónica:

```
                          ┌────────────────────────┐
                          │     Primer Valor v0    │
                          │   Escribir 64b literal │
                          └───────────┬────────────┘
                                      │
                                      ▼
                          ┌────────────────────────┐
                          │   Valor vi (i >= 1)    │
                          │   XOR = vi ^ v_{i-1}   │
                          └───────────┬────────────┘
                                      │
                 ┌────────────────────┴────────────────────┐
                 │                                         │
                 ▼                                         ▼
         ┌───────────────┐                        ┌──────────────────┐
         │   XOR == 0    │                        │     XOR != 0     │
         │   Emitir '0'  │                        │    Emitir '1'    │
         │    (1 bit)    │                        └────────┬─────────┘
         └───────────────┘                                 │
                                    ┌──────────────────────┴──────────────────────┐
                                    │                                             │
                                    ▼                                             ▼
                       ┌─────────────────────────┐                   ┌─────────────────────────┐
                       │  lz >= prev_lz  Y       │                   │      Otro Caso          │
                       │  tz >= prev_tz          │                   │    (Nueva Ventana)      │
                       │ (Reutilizar Ventana)    │                   │                         │
                       │                         │                   │ Emitir '1' (Control)    │
                       │ Emitir '0' (Control)    │                   │ Emitir lz en 5 bits     │
                       │ Emitir bits en ventana  │                   │ Emitir len en 6 bits    │
                       │ de longitud prev_len    │                   │ Emitir len bits signif. │
                       │ prev_len = 64-lz0-tz0   │                   │ prev_lz=lz, prev_tz=tz  │
                       └─────────────────────────┘                   └─────────────────────────┘
```

1. **Estado Inicial:** `prev_val = v_0`, `prev_lz = 64`, `prev_tz = 64`, `prev_len = 0`.
2. **Caso A (XOR == 0):** Valor idéntico al precedente. Se emite un único bit `0`.
3. **Caso B (XOR ≠ 0):** Se emite el bit `1` para indicar cambio de valor.
   - Se calculan los ceros a la izquierda y derecha: $lz = \text{clz64}(XOR)$, $tz = \text{ctz64}(XOR)$.
   - Longitud significativa actual: $\text{len} = 64 - lz - tz$.
   - **Subcaso B.1 (Reutilización de Ventana Previa):**  
     Si $lz \ge \text{prev\_lz} \land tz \ge \text{prev\_tz}$:
     - Se emite el bit de control `0`.
     - Se emiten los $\text{prev\_len}$ bits correspondientes a la ventana previa:
       $$\text{bits\_to\_write} = (XOR \gg \text{prev\_tz}) \ \& \ ((1ULL \ll \text{prev\_len}) - 1ULL)$$
     - No se actualizan $\text{prev\_lz}$, $\text{prev\_tz}$ ni $\text{prev\_len}$.
   - **Subcaso B.2 (Nueva Ventana Significativa):**  
     Si la condición anterior no se cumple:
     - Se emite el bit de control `1`.
     - Se emite $lz$ codificado en 5 bits (rango $[0, 31]$; si $lz > 31$, se acota a 31).
     - Se emite $\text{len}$ codificado en 6 bits (rango $[1, 64]$; se escribe $\text{len} - 1$ en 6 bits o $\text{len}$ según convención de Pelkonen).
     - Se emiten los $\text{len}$ bits significativos:
       $$\text{bits\_to\_write} = (XOR \gg tz) \ \& \ ((1ULL \ll \text{len}) - 1ULL)$$
     - Se actualiza el estado: $\text{prev\_lz} = lz$, $\text{prev\_tz} = tz$, $\text{prev\_len} = \text{len}$.

---

## 3. Chimp128 Canónico Estricto según Liakos et al. (VLDB 2022, Figura 8) (NC-C02)

Para asegurar total fidelidad al paper de Liakos et al., la especificación de `Chimp128_Canonical_F64` implementa exactamente el algoritmo de selección y la codificación de flujo de bits definidos en la **Figura 8** del artículo original.

### 3.1 Estructuras de Datos Canónicas
```c
#define CHIMP128_RING_SIZE        128
#define CHIMP128_HASH_TABLE_SIZE  16384 // 2^14 entradas

typedef struct {
    uint64_t ring_buf[CHIMP128_RING_SIZE];        // Buffer circular de valores (64 bits)
    uint32_t ring_sample_idx[CHIMP128_RING_SIZE]; // Índice absoluto de muestra
    int32_t  hash_table[CHIMP128_HASH_TABLE_SIZE];// Índice de muestra más reciente visto
    uint64_t prev_val;                           // v_{i-1}
    uint8_t  prev_lz_quant;                      // Cuantización de ceros iniciales previa
    uint8_t  prev_tz;                            // Ceros finales previos
} chimp128_f64_state_t;
```

### 3.2 Lógica de Selección según Figura 8
Para cada muestra $i$ con valor actual $v_i$:

1. **Extracción de Clave:**  
   $$\text{key} = \text{as\_uint64}(v_i) \ \& \ 0x3FFF \quad (14 \text{ bits menos significativos})$$

2. **Consulta de Tabla y Distancia Inclusiva:**  
   Se consulta $c = \text{hash\_table}[\text{key}]$.
   El candidato histórico reside dentro de la ventana de 128 muestras si y solo si:
   $$c \ge 0 \quad \land \quad 1 \le (i - c) \le 128$$
   *(Nótese la distancia inclusiva $\le 128$ según la especificación del paper).*

3. **Criterio de Selección por Umbral de Ceros Finales (Trailing Zeros $\ge 13$):**  
   Si el candidato $c$ es válido:
   - Se calcula $XOR_c = v_i \oplus v_c$, donde $v_c = \text{ring\_buf}[c \bmod 128]$.
   - Se calculan los ceros finales $tz_c = \text{ctz64}(XOR_c)$ si $XOR_c \ne 0$, o $tz_c = 64$ si $XOR_c = 0$.
   - **Regla Canónica de Decisión:**  
     La referencia histórica $v_c$ se utiliza **si y solo si** $tz_c \ge 13$ (o $XOR_c = 0$).  
     *(Justificación teórica de Liakos et al.: se requieren 7 bits para codificar el índice y 6 bits para la longitud; por ende, $7 + 6 = 13$ bits. Solo cuando $tz_c \ge 13$ el ahorro compensa el coste).*  
     Si $tz_c < 13$ (y $XOR_c \ne 0$), el candidato se descarta y se aplica el fallback a la referencia previa $v_{i-1}$.

### 3.3 Codificación de Bits según Figura 8
Una vez determinada la referencia ($v_{\text{ref}} \in \{v_{i-1}, v_c\}$):

1. **Bit de Selección de Referencia:**
   - Si se usa $v_{i-1}$: se emite el bit `0`. (No hay índice).
   - Si se usa $v_c$: se emite el bit `1`, seguido inmediatamente de los **7 bits del índice circular**:
     $$\text{index\_7bits} = (c \bmod 128)$$
2. **Codificación del XOR ($XOR = v_i \oplus v_{\text{ref}}$):**
   - **Caso 0 ($XOR == 0$):**  
     Se emite el bit `0`.  
     *(Si fue referencia histórica: bit `1` + 7b index + bit `0` = 9 bits en total).*
   - **Caso 1 ($XOR \ne 0$):**  
     Se emite el bit `1`.
     - Se calculan $lz = \text{clz64}(XOR)$, $tz = \text{ctz64}(XOR)$ y $lz_{\text{quant}} = \lfloor lz / 8 \rfloor$ (rango $[0, 7]$, 3 bits).
     - **Subcaso 1.1 (Reutilización de Parámetros):**  
       Si $lz_{\text{quant}} == \text{prev\_lz\_quant} \land tz == \text{prev\_tz}$:
       - Se emite el bit de control `0`.
       - Se emiten los bits significativos centrales sin retransmitir $lz$ ni $tz$.
     - **Subcaso 1.2 (Nuevos Parámetros):**  
       En caso contrario:
       - Se emite el bit de control `1`.
       - Se emiten 3 bits para $lz_{\text{quant}}$.
       - Se emiten 6 bits para la longitud significativa $\text{len} = 64 - (lz_{\text{quant}} \times 8) - tz$.
       - Se emiten los $\text{len}$ bits significativos.
       - Se actualizan $\text{prev\_lz\_quant} = lz_{\text{quant}}$ y $\text{prev\_tz} = tz$.

3. **Actualización de Estado:**  
   `ring_buf[i % 128] = v_i`, `ring_sample_idx[i % 128] = i`, `hash_table[key] = i`, `prev_val = v_i`.

---

## 4. Contrato Binario, Cabecera de 64 Bytes y Validación Estricta (NC-C03)

### 4.1 Definición Exacta de la Cabecera Canónica (64 Bytes)
Se fija el tamaño normativo de la cabecera en **64 bytes exactos** (`BP011_HEADER_SIZE_BYTES = 64`):

```c
#define BP011_BLOCK_MAGIC          0x4B504E42 // "BNPK" en Little-Endian
#define BP011_FORMAT_VERSION       0x0001
#define BP011_HEADER_SIZE_BYTES    64

#define BP011_FLAG_HAS_CHANNEL_TABLE  (1U << 0) // Bit 0: incluye tabla de saltos M7
#define BP011_FLAG_HAS_SERIES_DIR     (1U << 1) // Bit 1: incluye directorio de series C-MAPSS
```

#### Mapa de Memoria de la Cabecera (Offset 0x00 a 0x3F, 64 Bytes)
| Offset | Tipo | Nombre de Campo | Descripción |
| :---: | :---: | :--- | :--- |
| `0x00` | `uint32_t` | `magic` | 0x4B504E42 ("BNPK") |
| `0x04` | `uint16_t` | `version` | 0x0001 |
| `0x06` | `uint16_t` | `flags` | Bits de presencia de tablas opcionales |
| `0x08` | `uint32_t` | `num_channels` | Cantidad de canales $K$ |
| `0x0C` | `uint32_t` | `sample_count` | Cantidad de muestras temporales $N$ |
| `0x10` | `uint32_t` | `channel_table_offset` | Offset de la tabla de saltos M7 (0 si ausente) |
| `0x14` | `uint32_t` | `channel_table_bytes` | Tamaño de la tabla M7 ($K \times 12$, 0 si ausente) |
| `0x18` | `uint32_t` | `series_dir_offset` | Offset del directorio de series C-MAPSS (0 si ausente) |
| `0x1C` | `uint32_t` | `series_dir_bytes` | Tamaño del directorio ($M \times 24$, 0 si ausente) |
| `0x20` | `uint32_t` | `time_stream_offset` | Offset del stream de timestamps |
| `0x24` | `uint32_t` | `time_stream_bytes` | Longitud en bytes del stream de timestamps |
| `0x28` | `uint32_t` | `time_stream_bits` | Bits válidos en timestamps |
| `0x2C` | `uint32_t` | `values_stream_offset` | Offset del stream de valores |
| `0x30` | `uint32_t` | `values_stream_bytes` | Longitud en bytes del stream de valores |
| `0x34` | `uint32_t` | `values_stream_bits` | Bits válidos en valores |
| `0x38` | `uint32_t` | `total_file_bytes` | Tamaño total del contenedor (mínimo 64 bytes) |
| `0x3C` | `uint32_t` | `crc32` | CRC-32 (IEEE 802.3) del archivo completo |

### 4.2 Serialización Little-Endian Explícita
Todas las lecturas y escrituras se efectúan byte a byte:
```c
static inline void bp011_write_le16(uint8_t *d, uint16_t v) { d[0] = v & 0xFF; d[1] = (v >> 8) & 0xFF; }
static inline void bp011_write_le32(uint8_t *d, uint32_t v) {
    d[0] = v & 0xFF; d[1] = (v >> 8) & 0xFF; d[2] = (v >> 16) & 0xFF; d[3] = (v >> 24) & 0xFF;
}
static inline uint16_t bp011_read_le16(const uint8_t *s) { return (uint16_t)s[0] | ((uint16_t)s[1] << 8); }
static inline uint32_t bp011_read_le32(const uint8_t *s) {
    return (uint32_t)s[0] | ((uint32_t)s[1] << 8) | ((uint32_t)s[2] << 16) | ((uint32_t)s[3] << 24);
}
```

### 4.3 Validación Bidireccional de No Solapamiento y Secciones Opcionales
Para garantizar la integridad física de las secciones, el descompresor ejecuta el siguiente algoritmo estricto:

```c
typedef struct {
    uint32_t start;
    uint32_t end;
    bool active;
} bp011_range_t;

static inline bool bp011_ranges_overlap(const bp011_range_t *a, const bp011_range_t *b) {
    if (!a->active || !b->active) return false;
    // No solapamiento: a termina antes de que b empiece, o b termina antes de que a empiece
    return !(a->end <= b->start || b->end <= a->start);
}

bp011_status_t bp011_validate_header(const bp011_block_header_t *h, uint64_t actual_file_size) {
    if (actual_file_size < BP011_HEADER_SIZE_BYTES) return BP011_ERR_BUFFER_UNDERFLOW;
    if (h->magic != BP011_BLOCK_MAGIC) return BP011_ERR_INVALID_ARG;
    if (h->version != BP011_FORMAT_VERSION) return BP011_ERR_INVALID_ARG;
    if (h->total_file_bytes > actual_file_size) return BP011_ERR_STREAM_TRUNCATED;
    if (h->total_file_bytes < BP011_HEADER_SIZE_BYTES) return BP011_ERR_BUFFER_UNDERFLOW;

    // Validación de secciones opcionales respecto a flags
    bool has_chan = (h->flags & BP011_FLAG_HAS_CHANNEL_TABLE) != 0;
    if (!has_chan && (h->channel_table_offset != 0 || h->channel_table_bytes != 0)) {
        return BP011_ERR_INVALID_OFFSET;
    }
    if (has_chan && (h->channel_table_offset < BP011_HEADER_SIZE_BYTES || h->channel_table_bytes == 0)) {
        return BP011_ERR_INVALID_OFFSET;
    }

    bool has_series = (h->flags & BP011_FLAG_HAS_SERIES_DIR) != 0;
    if (!has_series && (h->series_dir_offset != 0 || h->series_dir_bytes != 0)) {
        return BP011_ERR_INVALID_OFFSET;
    }
    if (has_series && (h->series_dir_offset < BP011_HEADER_SIZE_BYTES || h->series_dir_bytes == 0)) {
        return BP011_ERR_INVALID_OFFSET;
    }

    // Comprobación de límites individuales sin sumas desbordables
    #define CHECK_BOUNDS(off, bytes) do {         if ((bytes) > 0) {             if ((off) < BP011_HEADER_SIZE_BYTES) return BP011_ERR_INVALID_OFFSET;             if ((off) > h->total_file_bytes) return BP011_ERR_INVALID_OFFSET;             if ((bytes) > h->total_file_bytes - (off)) return BP011_ERR_INVALID_OFFSET;         }     } while (0)

    if (has_chan) CHECK_BOUNDS(h->channel_table_offset, h->channel_table_bytes);
    if (has_series) CHECK_BOUNDS(h->series_dir_offset, h->series_dir_bytes);
    CHECK_BOUNDS(h->time_stream_offset, h->time_stream_bytes);
    CHECK_BOUNDS(h->values_stream_offset, h->values_stream_bytes);

    // Verificación de no solapamiento por pares
    bp011_range_t ranges[5] = {
        { 0, BP011_HEADER_SIZE_BYTES, true }, // Cabecera
        { h->channel_table_offset, h->channel_table_offset + h->channel_table_bytes, has_chan },
        { h->series_dir_offset, h->series_dir_offset + h->series_dir_bytes, has_series },
        { h->time_stream_offset, h->time_stream_offset + h->time_stream_bytes, h->time_stream_bytes > 0 },
        { h->values_stream_offset, h->values_stream_offset + h->values_stream_bytes, h->values_stream_bytes > 0 }
    };

    for (int i = 0; i < 5; i++) {
        for (int j = i + 1; j < 5; j++) {
            if (bp011_ranges_overlap(&ranges[i], &ranges[j])) {
                return BP011_ERR_INVALID_OFFSET; // Secciones superpuestas
            }
        }
    }
    return BP011_OK;
}
```

---

## 5. Directorio de Series C-MAPSS y Validación de Segmentos (NC-C04)

### 5.1 Estructura Formal del Segmento (24 Bytes)
```c
typedef struct {
    uint32_t unit_id;                  // Identificador único del motor (1 a M)
    uint32_t start_sample_idx;         // Muestra inicial absoluta (0 a N-1)
    uint32_t sample_count;             // Cantidad de ciclos de este motor
    uint32_t start_cycle;              // Ciclo operativo inicial real (ej. 1)
    uint32_t time_stream_byte_offset;  // Offset donde inicia el stream temporal de la serie
    uint32_t values_stream_byte_offset;// Offset donde inicia el stream de valores de la serie
} bp011_series_segment_t;
```

### 5.2 Validación Estricta de Segmentos
Durante la carga del directorio de series, el arnés verifica obligatoriamente:
1. **Orden y Unicidad Monótona Creciente:** $\text{unit\_id}[m] > \text{unit\_id}[m-1]$ para todo $m \ge 1$.
2. **Confinamiento de Streams:**  
   $$\text{time\_stream\_byte\_offset}[m] \ge h\text{->time\_stream\_offset} \quad \land \quad \text{time\_stream\_byte\_offset}[m] < h\text{->time\_stream\_offset} + h\text{->time\_stream\_bytes}$$
   $$\text{values\_stream\_byte\_offset}[m] \ge h\text{->values\_stream\_offset} \quad \land \quad \text{values\_stream\_byte\_offset}[m] < h\text{->values\_stream\_offset} + h\text{->values\_stream\_bytes}$$
3. **No Solapamiento y Cobertura de Muestras:**  
   $\text{start\_sample\_idx}[m] + \text{sample\_count}[m] \le h\text{->sample\_count}$.

---

## 6. Seguridad Aritmética de Bitstream (NC-C05 Parte A)

### 6.1 Códigos de Estado Unívocos
```c
typedef enum {
    BP011_OK                     =  0,
    BP011_ERR_NULL_POINTER       = -1,
    BP011_ERR_INVALID_ARG        = -2,
    BP011_ERR_BUFFER_OVERFLOW    = -3,
    BP011_ERR_STREAM_TRUNCATED   = -4,
    BP011_ERR_BUFFER_UNDERFLOW   = -5,
    BP011_ERR_CORRUPT_PADDING    = -6,
    BP011_ERR_CORRUPT_CRC        = -7,
    BP011_ERR_CORRUPT_DIRECTORY  = -8,
    BP011_ERR_INVALID_OFFSET     = -9
} bp011_status_t;
```

### 6.2 Inicialización con Guarda Anti-Desbordamiento
Para asegurar que `cap_bytes * 8ULL` o `size_bytes * 8ULL` no desborden `uint64_t`:

```c
#define BP011_MAX_STREAM_BYTES (UINT64_MAX / 8ULL)

bp011_status_t bw_safe_init(bit_writer_safe_t *bw, uint8_t *buf, uint64_t cap_bytes) {
    if (!bw || !buf) return BP011_ERR_NULL_POINTER;
    if (cap_bytes == 0 || cap_bytes > BP011_MAX_STREAM_BYTES) return BP011_ERR_INVALID_ARG;
    bw->buf = buf;
    bw->cap_bytes = cap_bytes;
    bw->bit_pos = 0;
    return BP011_OK;
}

bp011_status_t br_safe_init(bit_reader_safe_t *br, const uint8_t *buf, uint64_t size_bytes) {
    if (!br || !buf) return BP011_ERR_NULL_POINTER;
    if (size_bytes == 0 || size_bytes > BP011_MAX_STREAM_BYTES) return BP011_ERR_INVALID_ARG;
    br->buf = buf;
    br->size_bytes = size_bytes;
    br->bit_pos = 0;
    return BP011_OK;
}
```

### 6.3 Operaciones de Lectura/Escritura Seguras y Unívocas
```c
bp011_status_t bw_safe_write_bits(bit_writer_safe_t *bw, uint64_t val, uint8_t n_bits) {
    if (!bw || !bw->buf) return BP011_ERR_NULL_POINTER;
    if (n_bits > 64) return BP011_ERR_INVALID_ARG;
    if (n_bits == 0) return BP011_OK;

    uint64_t max_bits = bw->cap_bytes * 8ULL;
    if (bw->bit_pos > max_bits || n_bits > max_bits - bw->bit_pos) {
        return BP011_ERR_BUFFER_OVERFLOW;
    }

    for (int i = n_bits - 1; i >= 0; i--) {
        uint8_t bit = (uint8_t)((val >> i) & 1ULL);
        uint64_t byte_idx = bw->bit_pos >> 3;
        uint8_t bit_idx = 7 - (bw->bit_pos & 7);
        if (bit) bw->buf[byte_idx] |= (1U << bit_idx);
        else     bw->buf[byte_idx] &= ~(1U << bit_idx);
        bw->bit_pos++;
    }
    return BP011_OK;
}

bp011_status_t br_safe_read_bit(bit_reader_safe_t *br, uint8_t *out_bit) {
    if (!br || !br->buf || !out_bit) return BP011_ERR_NULL_POINTER;

    uint64_t max_bits = br->size_bytes * 8ULL;
    if (br->bit_pos >= max_bits) return BP011_ERR_STREAM_TRUNCATED;

    uint64_t byte_idx = br->bit_pos >> 3;
    uint8_t bit_idx = 7 - (br->bit_pos & 7);
    *out_bit = (br->buf[byte_idx] >> bit_idx) & 1U;
    br->bit_pos++;
    return BP011_OK;
}
```

---

## 7. Alineación Real de I/O en Windows y Protocolo Cold-Cache (NC-C05 Parte B)

### 7.1 Consulta Dinámica de Requisitos de Alineación del Dispositivo
Para no asumir un valor fijo de 4096 bytes, el arnés de medición en Windows determina la alineación obligatoria mediante la siguiente secuencia:

```c
typedef struct {
    uint32_t sector_size;      // Tamaño mínimo de transferencia (BytesPerLogicalSector)
    uint32_t physical_sector;  // Tamaño físico (BytesPerPhysicalSector)
    uint32_t buffer_align;     // Alineación requerida para punteros de memoria
} bp011_device_alignment_t;

bp011_status_t bp011_query_device_alignment(HANDLE hFile, bp011_device_alignment_t *align) {
    STORAGE_PROPERTY_QUERY query;
    memset(&query, 0, sizeof(query));
    query.PropertyId = StorageAccessAlignmentProperty;
    query.QueryType = PropertyStandardQuery;

    STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR desc;
    DWORD bytes_returned = 0;

    if (DeviceIoControl(hFile, IOCTL_STORAGE_QUERY_PROPERTY,
                        &query, sizeof(query),
                        &desc, sizeof(desc),
                        &bytes_returned, NULL)) {
        align->sector_size = desc.BytesPerLogicalSector ? desc.BytesPerLogicalSector : 512;
        align->physical_sector = desc.BytesPerPhysicalSector ? desc.BytesPerPhysicalSector : 4096;
        align->buffer_align = desc.BytesPerPhysicalSector ? desc.BytesPerPhysicalSector : 4096;
        return BP011_OK;
    }

    // Fallback estándar mediante GetDiskFreeSpaceA
    DWORD sectors_per_cluster, bytes_per_sector, free_clusters, total_clusters;
    if (GetDiskFreeSpaceA(NULL, &sectors_per_cluster, &bytes_per_sector, &free_clusters, &total_clusters)) {
        align->sector_size = bytes_per_sector;
        align->physical_sector = bytes_per_sector;
        align->buffer_align = bytes_per_sector;
        return BP011_OK;
    }
    align->sector_size = 4096;
    align->physical_sector = 4096;
    align->buffer_align = 4096;
    return BP011_OK;
}
```

### 7.2 Reglas de Lectura Redondeada y Gestión del Bloque Final
Al abrir archivos con `FILE_FLAG_NO_BUFFERING`:
1. **Puntero de Memoria:** Se asigna mediante `VirtualAlloc(NULL, alloc_bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)`. Al retornar memoria alineada al límite de página del sistema (típicamente 4096 o 64KB), satisface incondicionalmente `buffer_align`.
2. **Alineación de Desplazamiento (`FileOffset`):**  
   Para leer $L$ bytes en el offset $O$:
   $$O_{\text{aligned}} = \left\lfloor \frac{O}{A} \right\rfloor \times A, \qquad \text{donde } A = \text{align.sector\_size}$$
   Desplazamiento relativo interno en el buffer: $\Delta O = O - O_{\text{aligned}}$.
3. **Alineación de Longitud de Lectura:**  
   $$L_{\text{aligned}} = \left\lceil \frac{\Delta O + L}{A} \right\rceil \times A$$
4. **Gestión de Fin de Archivo:** Si $O_{\text{aligned}} + L_{\text{aligned}}$ excede el tamaño físico del archivo, la lectura se acota al límite del archivo redondeado al sector superior, evitando errores `ERROR_HANDLE_EOF`.

### 7.3 Delimitación de Capas y Desalojo de Caché CPU
- **In-Memory Benchmark (Primario):** Mide descompresión en RAM pura. El protocolo de desalojo de CPU asigna un buffer de $\max(256\text{ MB}, 2 \times \text{L3\_size})$ y realiza barrido volátil. Se reporta formalmente como *desalojo empírico por desplazamiento*.
- **End-to-End I/O Benchmark (Secundario):** Se ejecuta con `FILE_FLAG_NO_BUFFERING`. Se reconoce formalmente que la DRAM interna del controlador NVMe/SSD permanece fuera de control del software de usuario, mitigándose con $R = 20$ repeticiones y cálculo de desviación estándar e intervalos de confianza.

---

## 8. Organización Modular y Plan de Aislamiento Experimental

Toda la implementación reside exclusivamente en `tests/experiments/bp011/`:

| Módulo / Archivo | Responsabilidad Técnica |
| :--- | :--- |
| `bp011_types.h` | Cabecera canónica de 64B, Little-Endian helpers, validaciones de no solapamiento y `bp011_status_t` |
| `timeseries_bitstream_safe.h` | Mapeadores de bits seguros con guarda `cap <= UINT64_MAX / 8` |
| `timeseries_timestamp_codec.h` | Códec temporal unificado R15.10 (microsegundos en `int64_t`) |
| `gorilla_canonical.h` | Baseline M1 Gorilla canónico F64 (Pelkonen 2015) con máquina de estados completa y variante F32 |
| `chimp_canonical.h` | Baseline M2 Chimp canónico F64 (Liakos 2022) y variante F32 |
| `chimp128_canonical.h` | Baseline M3 Chimp128 canónico F64 según Figura 8 (tabla hash $2^{14}$, ventana $\le 128$, umbral $tz \ge 13$) |
| `binpack_timeseries_adapter.h` | Adaptador composicional de BinPack con transposición escalar/SIMD |
| `binpack_channel_dedicated.h` | Modelo M7 con tabla de saltos de canales y compresión de canales independientes |
| `bp011_benchmark_harness.c` | Arnés de medición in-memory y end-to-end con alineación dinámica de sector |

---

## 9. Conclusión y Petición Formal

El diseño técnico presentado en esta **Revisión R4** resuelve de manera exhaustiva y matemáticamente concluyente cada una de las observaciones del Auditor Líder:
1. Máquina de estados completa de Gorilla formalizada.
2. Chimp128 canónico ajustado fielmente a la Figura 8 de Liakos et al. (distancia inclusiva $\le 128$, umbral $tz \ge 13$, flujo canónico de bits).
3. Cabecera corregida a 64 bytes exactos, validación de no solapamiento bidireccional y control estricto de tablas opcionales.
4. Confinamiento y validación de rangos de segmentos para C-MAPSS.
5. Bitstream seguro contra overflow en multiplicación y sumas, con retornos de estado unívocos.
6. Consulta dinámica de alineación de almacenamiento en Windows para Unbuffered I/O.

Por lo tanto, se solicita respetuosamente la **aprobación incondicional del Diseño Técnico R4** y la **autorización para iniciar la fase de codificación experimental** exclusivamente en `tests/experiments/bp011/`.
