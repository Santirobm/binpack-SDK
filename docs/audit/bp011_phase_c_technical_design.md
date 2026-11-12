# Especificación y Diseño Técnico R5 — Hito BP-011 / Fase C
## Arquitectura de Instrumentación, Baselines Composicionales y Arnés de Medición

**Proyecto:** BinPack Enterprise SDK  
**Hito:** BP-011  
**Fase:** Fase C — Auditoría de Instrumentación y Diseño de Arneses (Revisión R5)  
**Documento Rector:** `docs/audit/bp011_phase_c_technical_design.md`  
**Rama:** `audit/bp1-core-safety`  
**Fecha:** 12 de noviembre de 2026  
**Estado:** ESPECIFICACIÓN TÉCNICA DEFINITIVA DE DISEÑO DE FASE C (REVISIÓN R5)  
**Gobernanza:** *Code Freeze* preservado al 100% (cero modificaciones en `src/`, `include/` y `deps/`; sin compilación ni ejecución en esta fase).

---

## Control de Cambios de la Revisión R5 (Subsanación Integral de Observaciones)

En estricta conformidad con el dictamen de auditoría emitido sobre la Revisión R4, la presente **Revisión R5** resuelve de forma exhaustiva, matemática y cerrada los cinco requerimientos formulados por el Auditor Líder:

1. **NC-C01 (Gorilla F64 Robusto y Cerrado):**
   - **Máscara Segura de 64 Bits:** Implementación de la función auxiliar `bp011_make_mask64(len)` que elimina incondicionalmente el comportamiento indefinido (*Undefined Behavior*) de desplazamientos $\ge 64$ (`(1ULL << len) - 1ULL`).
   - **Convención Unívoca de Longitud Significativa:** Se fija la norma exacta: $\text{len} \in [1, 64]$ se codifica biyectivamente como $\text{len\_code} = \text{len} - 1$ en 6 bits (rango $[0, 63]$), y el decodificador reconstruye $\text{len} = \text{len\_code} + 1$.
   - **Tratamiento Consistente de Ceros Iniciales Acotados:** Se formaliza $lz_{\text{eff}} = \min(lz, 31)$ (5 bits). La longitud significativa se define respecto al bloque efectivamente codificado: $\text{len} = 64 - lz_{\text{eff}} - tz$, garantizando simetría matemática perfecta entre el emisor y el receptor.

2. **NC-C02 (Chimp128 Canónico Estricto según Figura 8 de Liakos et al., VLDB 2022):**
   - **Umbral Canónico Estricto:** Se formaliza la condición $tz_c > 13$ (estrictamente superior a 13 bits), demostrando su correlación directa con la clave hash de 14 bits menos significativos ($v_i \& 0x3FFF$), la cual garantiza $tz_c \ge 14 > 13$ en caso de coincidencia en la tabla hash.
   - **Máquina de Estados de la Figura 8:** Se implementa de forma literal la bifurcación canónica:
     * Rama A (Referencia Histórica $v_c$ en ventana $\le 128$ con $tz_c > 13$ o $XOR_c = 0$): Bit `1` + 7 bits de índice ($c \bmod 128$) + codificación específica del $XOR_c$.
     * Rama B (Fallback a Valor Inmediatamente Anterior $v_{i-1}$): Bit `0` (sin índice) + codificación específica del $XOR_{\text{prev}}$.
   - **Campos y Estados Específicos:** Reutilización de parámetros ($lz_{\text{quant}}$ y $tz$) con bit de control `0` o nuevos parámetros con bit `1` + 3b $lz_{\text{quant}}$ + 6b $\text{len}$ + bits significativos.

3. **NC-C03 (Cabecera y Validación Exhaustiva de Contenedor):**
   - **Presencia Obligatoria de Streams:** Validación incondicional de que `time_stream_bytes > 0`, `time_stream_bits > 0`, `values_stream_bytes > 0` y `values_stream_bits > 0`.
   - **Consistencia de Bits y Bytes:** Verificación estricta de que $\text{bits} \le 8 \times \text{bytes}$ y que los bits de padding final en el último byte sean estrictamente ceros (`BP011_ERR_CORRUPT_PADDING`).
   - **Rechazo de Flags Desconocidos:** Comprobación estricta `(flags & ~BP011_KNOWN_FLAGS) == 0`.
   - **Validación Bidireccional de No Solapamiento:** Algoritmo par a par verificado para todas las secciones activas.

4. **NC-C04 (Confinamiento y No Solapamiento Integral de Segmentos C-MAPSS):**
   - **Estructura Ampliada con Longitudes:** `bp011_series_segment_t` (32 bytes) incorpora campos dedicados `time_stream_bytes` y `values_stream_bytes`.
   - **Validación de Intervalos Completos:** Se comprueba el confinamiento de cada intervalo $[O, O + L)$ dentro de los streams globales.
   - **No Solapamiento Inter-Segmento:** Validación estricta de que ningún segmento se solape con otro en muestras ($S_m \ge S_{m-1} + N_{m-1}$), en bytes temporales ni en bytes de valores.
   - **Prohibición de Series Vacías:** Se rechaza cualquier segmento con `sample_count == 0` con `BP011_ERR_EMPTY_SERIES`.

5. **NC-C05B (Alineación Real de I/O en Windows y Gestión del Último Bloque):**
   - **Alineación de Buffer y Handle:** Comprobación explícita de alineación del puntero `VirtualAlloc` contra el requisito del handle y del dispositivo.
   - **Fallback Asociado al Volumen del Archivo:** Consulta de espacio y sector basada en la ruta del volumen específico del archivo (`GetVolumePathNameA`) y no en la unidad de trabajo actual.
   - **Tratamiento del Último Bloque:** Procedimiento formal de redondeo acotado al tamaño del archivo alineado a sector ($F_{\text{aligned}}$), truncando la longitud efectiva leída a los bytes reales disponibles para evitar errores de fin de archivo con `FILE_FLAG_NO_BUFFERING`.

---

## 1. Introducción, Mandato y Principios Rectores

El presente documento constituye la especificación técnica definitiva para la instrumentación experimental del benchmark de series temporales industriales en el marco del Hito BP-011.

### 1.1 Principios Rectores Innegociables
1. **Simetría y Paridad Estricta de Carga Útil:** Todos los motores evaluados comprimen, empaquetan y reconstruyen exactamente las mismas tuplas $(t_i, v_{i, 0}, \dots, v_{i, K-1})$ con sus metadatos y delimitadores de bloque.
2. **Aislamiento Experimental Absoluto:** Todo el código experimental reside exclusivamente en [`tests/experiments/bp011/`](file:///C:/Users/sgi.lm/Desktop/binpack-SDK/tests/experiments/bp011/). El código productivo en `src/`, `include/` y librerías en `deps/` permanece bajo estricto congelamiento (*code freeze*).
3. **Seguridad y Contención de Memoria:** Todo el manejo de flujos de bits se implementa con comprobaciones libres de sumas y multiplicaciones potencialmente desbordables, sin comportamiento indefinido.
4. **Cero Compilación y Cero Ejecución Prematura:** Durante la Fase C únicamente se diseña y codifica el arnés. La compilación y ejecución experimental se reservan para las Fases D y E.

---

## 2. Códec Canónico Gorilla F64 (NC-C01)

### 2.1 Máscara Segura de 64 Bits (Eliminación de Undefined Behavior)
En C99, desplazar un entero de 64 bits por 64 posiciones (`1ULL << 64`) constituye comportamiento indefinido. Para garantizar la seguridad aritmética absoluta, toda operación de enmascaramiento se realiza mediante:

```c
static inline uint64_t bp011_make_mask64(uint8_t len) {
    if (len == 0) return 0ULL;
    if (len >= 64) return ~0ULL;
    return (1ULL << len) - 1ULL;
}
```

### 2.2 Máquina de Estados Canónica de Pelkonen et al. (VLDB 2015)
El códec Gorilla para valores Float64 opera según las siguientes reglas matemáticas cerradas:

1. **Primer Valor ($v_0$):** Se almacena como un valor literal IEEE 754 de 64 bits (`uint64_t`). Se inicializan:
   $$\text{prev\_val} = v_0, \quad \text{prev\_lz\_eff} = 64, \quad \text{prev\_tz} = 64, \quad \text{prev\_len} = 0$$

2. **Valores Subsecuentes ($v_i, i \ge 1$):**  
   Se calcula la diferencia a nivel de bits: $XOR = v_i \oplus v_{i-1}$.
   
   - **Caso 1: $XOR == 0$ (Valor Idéntico):**  
     Se emite un único bit `0` (1 bit).
   
   - **Caso 2: $XOR \ne 0$ (Valor Diferente):**  
     Se emite el bit `1`.  
     Se calculan:
     $$lz = \text{clz64}(XOR), \qquad tz = \text{ctz64}(XOR)$$
     
     **Acotación de Ceros Iniciales a 5 Bits:**  
     Como el formato original reserva 5 bits para ceros iniciales (capacidad $0 \le \text{code} \le 31$), si $lz \ge 32$ se fija $lz_{\text{eff}} = 31$; en caso contrario, $lz_{\text{eff}} = lz$.  
     La longitud de la ventana significativa se deriva estrictamente a partir de $lz_{\text{eff}}$:
     $$\text{len} = 64 - lz_{\text{eff}} - tz$$
     *(Nótese que $\text{len} \in [1, 64]$. Si $lz > 31$, los ceros iniciales adicionales entre 31 y $lz$ se transportan como ceros dentro de la ventana de $\text{len}$ bits).*
     
     - **Subcaso 2.A (Reutilización de Ventana Previa):**  
       La ventana previa se reutiliza si los bits significativos actuales caben dentro de ella:
       $$lz_{\text{eff}} \ge \text{prev\_lz\_eff} \quad \land \quad tz \ge \text{prev\_tz}$$
       En este caso:
       - Se emite el bit de control `0`.
       - Se emiten los $\text{prev\_len}$ bits de la ventana previa:
         $$\text{bits} = (XOR \gg \text{prev\_tz}) \ \& \ \text{bp011\_make\_mask64}(\text{prev\_len})$$
       - No se actualiza el estado de la ventana.
     
     - **Subcaso 2.B (Nueva Ventana Significativa):**  
       Si no se puede reutilizar la ventana previa:
       - Se emite el bit de control `1`.
       - Se emite $lz_{\text{eff}}$ en 5 bits (rango $[0, 31]$).
       - Se emite $\text{len}$ en 6 bits mediante la convención unívoca:
         $$\text{len\_code} = \text{len} - 1 \quad (\text{rango } [0, 63] \text{ codificado en 6 bits})$$
       - Se emiten los $\text{len}$ bits significativos:
         $$\text{bits} = (XOR \gg tz) \ \& \ \text{bp011\_make\_mask64}(\text{len})$$
       - Se actualiza el estado: $\text{prev\_lz\_eff} = lz_{\text{eff}}$, $\text{prev\_tz} = tz$, $\text{prev\_len} = \text{len}$.

3. **Reconstrucción en el Decodificador:**
   - Si lee bit `0`: $v_i = v_{i-1}$.
   - Si lee bit `1`:
     - Si lee bit de control `0`: lee $\text{prev\_len}$ bits, calcula $XOR = \text{bits} \ll \text{prev\_tz}$, y obtiene $v_i = v_{i-1} \oplus XOR$.
     - Si lee bit de control `1`: lee 5 bits para $lz_{\text{eff}}$, lee 6 bits para $\text{len\_code}$, calcula $\text{len} = \text{len\_code} + 1$, calcula $tz = 64 - lz_{\text{eff}} - \text{len}$, lee $\text{len}$ bits, calcula $XOR = \text{bits} \ll tz$, actualiza el estado previo y obtiene $v_i = v_{i-1} \oplus XOR$.

---

## 3. Chimp128 Canónico Estricto según Figura 8 de Liakos et al. (VLDB 2022) (NC-C02)

Para garantizar total conformidad con el artículo fundador de Chimp128, se implementa de manera estricta y literal el diagrama de flujo de la **Figura 8** (VLDB 2022, Sección 4).

### 3.1 Estructura de Estado Canónica
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
    uint8_t  prev_len;                           // Longitud previa
} chimp128_f64_state_t;
```

### 3.2 Lógica de Selección y Umbral Canónico Estricto
Para cada muestra $i$ con valor actual $v_i$:

1. **Extracción de Clave Hash:**  
   $$\text{key} = \text{as\_uint64}(v_i) \ \& \ 0x3FFF \quad (14 \text{ bits menos significativos})$$

2. **Búsqueda en Tabla Hash y Distancia Inclusiva:**  
   Se consulta el índice $c = \text{hash\_table}[\text{key}]$.
   El candidato histórico es válido si reside dentro de la ventana deslizante:
   $$c \ge 0 \quad \land \quad 1 \le (i - c) \le 128$$

3. **Criterio de Decisión Canónico de la Figura 8:**  
   Si el candidato $c$ es válido:
   - Se toma $v_c = \text{ring\_buf}[c \bmod 128]$ y se evalúa $XOR_c = v_i \oplus v_c$.
   - Se calculan los ceros finales: $tz_c = (XOR_c == 0) ? 64 : \text{ctz64}(XOR_c)$.
   - **Condición Estricta:** Como la clave de $2^{14}$ posiciones asegura que $v_i$ y $v_c$ comparten al menos 14 bits finales, cuando hay coincidencia de clave se tiene $tz_c \ge 14$. El paper establece formalmente el umbral:
     $$tz_c > \log_2(128) + \log_2(64) = 7 + 6 = 13 \quad \lor \quad XOR_c == 0$$
     Dado que $14 > 13$, la referencia histórica **supera estrictamente el umbral** y se utiliza como referencia de compresión ($v_{\text{ref}} = v_c$).
   - Si no hay coincidencia en la tabla o el candidato tiene $i - c > 128$, se utiliza la referencia inmediatamente anterior: $v_{\text{ref}} = v_{i-1}$.

### 3.3 Flujo Literal de Codificación de Bits (Figura 8)

```
                            ┌────────────────────────┐
                            │    Muestra Actual vi   │
                            └───────────┬────────────┘
                                        │
                         ¿Candidato vc válido en ventana?
                                        │
                   ┌────────────────────┴────────────────────┐
                   │ SÍ (vc válido)                          │ NO (fallback)
                   ▼                                         ▼
         ┌───────────────────┐                     ┌───────────────────┐
         │   Emitir bit '1'  │                     │   Emitir bit '0'  │
         │   Emitir 7b index │                     │    (sin índice)   │
         │   XOR = vi ^ vc   │                     │  XOR = vi ^ v_{i-1}│
         └─────────┬─────────┘                     └─────────┬─────────┘
                   │                                         │
                   └────────────────────┬────────────────────┘
                                        │
                               ¿Es XOR == 0?
                                        │
                   ┌────────────────────┴────────────────────┐
                   │ SÍ                                      │ NO
                   ▼                                         ▼
         ┌───────────────────┐                     ┌───────────────────┐
         │   Emitir bit '0'  │                     │   Emitir bit '1'  │
         └───────────────────┘                     └─────────┬─────────┘
                                                             │
                                        ¿lz_quant == prev_lz_quant Y tz == prev_tz?
                                                             │
                                        ┌────────────────────┴────────────────────┐
                                        │ SÍ                                      │ NO
                                        ▼                                         ▼
                              ┌───────────────────┐                     ┌───────────────────┐
                              │   Emitir bit '0'  │                     │   Emitir bit '1'  │
                              │   Emitir bits     │                     │   Emitir lz_quant │
                              │   en ventana      │                     │   en 3 bits       │
                              │   prev_len        │                     │   Emitir len (6b) │
                              └───────────────────┘                     │   Emitir len bits │
                                                                        └───────────────────┘
```

#### Reglas de Emisión para Cada Rama:
1. **Selector de Referencia:**
   - **Rama Histórica:** Bit `1` + 7 bits de índice ($c \bmod 128$).
   - **Rama Precedente:** Bit `0` (1 bit en total).

2. **Codificación de XOR:**
   - Si $XOR == 0$: se emite el bit `0`.  
     *(Coste total: con histórica $1 + 7 + 1 = 9$ bits; con precedente $1 + 1 = 2$ bits).*
   - Si $XOR \ne 0$: se emite el bit `1`.  
     Se calcula $lz = \text{clz64}(XOR)$, $tz = \text{ctz64}(XOR)$ y $lz_{\text{quant}} = \min(7, \lfloor lz / 8 \rfloor)$ (3 bits).  
     - **Reutilización de Parámetros:** Si $lz_{\text{quant}} == \text{prev\_lz\_quant} \land tz == \text{prev\_tz}$:
       - Se emite el bit de control `0`.
       - Se emiten los $\text{prev\_len}$ bits significativos:
         $$\text{bits} = (XOR \gg \text{prev\_tz}) \ \& \ \text{bp011\_make\_mask64}(\text{prev\_len})$$
     - **Nuevos Parámetros:** En caso contrario:
       - Se emite el bit de control `1`.
       - Se emite $lz_{\text{quant}}$ en 3 bits (rango $[0, 7]$).
       - Longitud significativa: $\text{len} = 64 - (lz_{\text{quant}} \times 8) - tz$.
       - Se emite $\text{len}$ en 6 bits: $\text{len\_code} = \text{len} - 1$ en 6 bits (rango $[0, 63]$).
       - Se emiten los $\text{len}$ bits significativos:
         $$\text{bits} = (XOR \gg tz) \ \& \ \text{bp011\_make\_mask64}(\text{len})$$
       - Se actualiza el estado: $\text{prev\_lz\_quant} = lz_{\text{quant}}$, $\text{prev\_tz} = tz$, $\text{prev\_len} = \text{len}$.

3. **Actualización de Tablas:**  
   `ring_buf[i % 128] = v_i`, `ring_sample_idx[i % 128] = i`, `hash_table[key] = i`, `prev_val = v_i`.

---

## 4. Contrato Binario y Validación Exhaustiva de Contenedor (NC-C03)

### 4.1 Definición de la Cabecera Canónica (64 Bytes Exactos)
```c
#define BP011_BLOCK_MAGIC             0x4B504E42 // "BNPK" en Little-Endian
#define BP011_FORMAT_VERSION          0x0001
#define BP011_HEADER_SIZE_BYTES       64

#define BP011_FLAG_HAS_CHANNEL_TABLE  (1U << 0) // Bit 0: incluye tabla de saltos M7
#define BP011_FLAG_HAS_SERIES_DIR     (1U << 1) // Bit 1: incluye directorio de series C-MAPSS
#define BP011_KNOWN_FLAGS             (BP011_FLAG_HAS_CHANNEL_TABLE | BP011_FLAG_HAS_SERIES_DIR)
```

#### Mapa de Memoria de la Cabecera (Offset 0x00 a 0x3F, 64 Bytes)
| Offset | Tipo | Nombre de Campo | Descripción y Validación |
| :---: | :---: | :--- | :--- |
| `0x00` | `uint32_t` | `magic` | 0x4B504E42 ("BNPK") |
| `0x04` | `uint16_t` | `version` | 0x0001 |
| `0x06` | `uint16_t` | `flags` | Rechaza bits fuera de `BP011_KNOWN_FLAGS` |
| `0x08` | `uint32_t` | `num_channels` | Cantidad de canales $K \ge 1$ |
| `0x0C` | `uint32_t` | `sample_count` | Muestras temporales $N \ge 1$ |
| `0x10` | `uint32_t` | `channel_table_offset` | Offset de tabla M7 (0 si ausente, $\ge 64$ si presente) |
| `0x14` | `uint32_t` | `channel_table_bytes` | Tamaño de tabla M7 ($K \times 12$ si presente) |
| `0x18` | `uint32_t` | `series_dir_offset` | Offset directorio C-MAPSS (0 si ausente, $\ge 64$ si presente) |
| `0x1C` | `uint32_t` | `series_dir_bytes` | Tamaño directorio ($M \times 32$ si presente) |
| `0x20` | `uint32_t` | `time_stream_offset` | Offset obligatorio del stream de tiempo ($\ge 64$) |
| `0x24` | `uint32_t` | `time_stream_bytes` | Longitud obligatoria de tiempo ($> 0$) |
| `0x28` | `uint32_t` | `time_stream_bits` | Bits válidos obligatorios ($1 \le \text{bits} \le 8 \times \text{bytes}$) |
| `0x2C` | `uint32_t` | `values_stream_offset` | Offset obligatorio del stream de valores ($\ge 64$) |
| `0x30` | `uint32_t` | `values_stream_bytes` | Longitud obligatoria de valores ($> 0$) |
| `0x34` | `uint32_t` | `values_stream_bits` | Bits válidos obligatorios ($1 \le \text{bits} \le 8 \times \text{bytes}$) |
| `0x38` | `uint32_t` | `total_file_bytes` | Tamaño total del bloque ($\ge 64$) |
| `0x3C` | `uint32_t` | `crc32` | CRC-32 (IEEE 802.3) del archivo con crc32=0 |

### 4.2 Algoritmo de Validación Riguroso y No Solapamiento
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

    // 1. Rechazar flags desconocidos
    if ((h->flags & ~BP011_KNOWN_FLAGS) != 0) return BP011_ERR_UNKNOWN_FLAGS;

    // 2. Validación de presencia obligatoria de streams de datos
    if (h->sample_count == 0 || h->num_channels == 0) return BP011_ERR_INVALID_ARG;
    if (h->time_stream_bytes == 0 || h->time_stream_bits == 0) return BP011_ERR_INVALID_ARG;
    if (h->values_stream_bytes == 0 || h->values_stream_bits == 0) return BP011_ERR_INVALID_ARG;

    // 3. Coherencia estricta entre bits y bytes
    if (h->time_stream_bits > (uint64_t)h->time_stream_bytes * 8ULL) return BP011_ERR_INVALID_ARG;
    if (h->values_stream_bits > (uint64_t)h->values_stream_bytes * 8ULL) return BP011_ERR_INVALID_ARG;

    // 4. Verificación de padding cero en los últimos bytes de los streams
    uint8_t time_pad_bits = (uint8_t)((h->time_stream_bytes * 8ULL) - h->time_stream_bits);
    if (time_pad_bits > 0 && time_pad_bits < 8) {
        uint8_t last_byte = file_buf[h->time_stream_offset + h->time_stream_bytes - 1];
        uint8_t pad_mask = (1U << time_pad_bits) - 1U;
        if ((last_byte & pad_mask) != 0) return BP011_ERR_CORRUPT_PADDING;
    }

    uint8_t val_pad_bits = (uint8_t)((h->values_stream_bytes * 8ULL) - h->values_stream_bits);
    if (val_pad_bits > 0 && val_pad_bits < 8) {
        uint8_t last_byte = file_buf[h->values_stream_offset + h->values_stream_bytes - 1];
        uint8_t pad_mask = (1U << val_pad_bits) - 1U;
        if ((last_byte & pad_mask) != 0) return BP011_ERR_CORRUPT_PADDING;
    }

    // 5. Validación de secciones opcionales vinculadas a flags
    bool has_chan = (h->flags & BP011_FLAG_HAS_CHANNEL_TABLE) != 0;
    if (!has_chan && (h->channel_table_offset != 0 || h->channel_table_bytes != 0)) return BP011_ERR_INVALID_OFFSET;
    if (has_chan && (h->channel_table_offset < BP011_HEADER_SIZE_BYTES || h->channel_table_bytes == 0)) return BP011_ERR_INVALID_OFFSET;

    bool has_series = (h->flags & BP011_FLAG_HAS_SERIES_DIR) != 0;
    if (!has_series && (h->series_dir_offset != 0 || h->series_dir_bytes != 0)) return BP011_ERR_INVALID_OFFSET;
    if (has_series && (h->series_dir_offset < BP011_HEADER_SIZE_BYTES || h->series_dir_bytes == 0)) return BP011_ERR_INVALID_OFFSET;

    // 6. Validación de límites sin sumas desbordables
    #define CHECK_SECTION_BOUNDS(off, bytes) do {         if ((off) < BP011_HEADER_SIZE_BYTES) return BP011_ERR_INVALID_OFFSET;         if ((off) > h->total_file_bytes) return BP011_ERR_INVALID_OFFSET;         if ((bytes) > h->total_file_bytes - (off)) return BP011_ERR_INVALID_OFFSET;     } while (0)

    if (has_chan) CHECK_SECTION_BOUNDS(h->channel_table_offset, h->channel_table_bytes);
    if (has_series) CHECK_SECTION_BOUNDS(h->series_dir_offset, h->series_dir_bytes);
    CHECK_SECTION_BOUNDS(h->time_stream_offset, h->time_stream_bytes);
    CHECK_SECTION_BOUNDS(h->values_stream_offset, h->values_stream_bytes);

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

---

## 5. Directorio de Series C-MAPSS y Validación de Intervalos Completos (NC-C04)

### 5.1 Estructura del Segmento con Longitudes Explícitas (32 Bytes)
Para permitir la validación completa de límites y el acceso selectivo sin dependencias, `bp011_series_segment_t` incluye longitudes de bytes independientes para ambos flujos:

```c
typedef struct {
    uint32_t unit_id;                  // Identificador único del motor (1 a M)
    uint32_t start_sample_idx;         // Muestra inicial absoluta (0 a N-1)
    uint32_t sample_count;             // Cantidad de ciclos de este motor (> 0)
    uint32_t start_cycle;              // Ciclo operativo inicial real (ej. 1)
    uint32_t time_stream_byte_offset;  // Offset donde inicia el stream temporal del motor
    uint32_t time_stream_bytes;        // Longitud en bytes del stream temporal del motor
    uint32_t values_stream_byte_offset;// Offset donde inicia el stream de valores del motor
    uint32_t values_stream_bytes;      // Longitud en bytes del stream de valores del motor
} bp011_series_segment_t;
```

### 5.2 Validación Estricta de Segmentos e Intervalos
El arnés verifica obligatoriamente durante la carga del directorio:

```c
bp011_status_t bp011_verify_series_directory(const bp011_block_header_t *h, const bp011_series_segment_t *dir, uint32_t num_series) {
    if (!h || !dir) return BP011_ERR_NULL_POINTER;
    if (num_series == 0) return BP011_ERR_INVALID_ARG;

    uint32_t expected_sample_cursor = 0;
    uint32_t expected_time_cursor = h->time_stream_offset;
    uint32_t expected_val_cursor = h->values_stream_offset;

    for (uint32_t m = 0; m < num_series; m++) {
        const bp011_series_segment_t *seg = &dir[m];

        // 1. Prohibición de series vacías
        if (seg->sample_count == 0) return BP011_ERR_EMPTY_SERIES;
        if (seg->time_stream_bytes == 0 || seg->values_stream_bytes == 0) return BP011_ERR_INVALID_ARG;

        // 2. Orden monótono estricto de unit_id
        if (m > 0 && seg->unit_id <= dir[m - 1].unit_id) return BP011_ERR_CORRUPT_DIRECTORY;

        // 3. Comprobación de no solapamiento secuencial de muestras
        if (seg->start_sample_idx < expected_sample_cursor) return BP011_ERR_OVERLAPPING_SEGMENT;
        if (seg->sample_count > h->sample_count - seg->start_sample_idx) return BP011_ERR_INVALID_ARG;
        expected_sample_cursor = seg->start_sample_idx + seg->sample_count;

        // 4. Confinamiento y no solapamiento en el stream de timestamps
        if (seg->time_stream_byte_offset < expected_time_cursor) return BP011_ERR_OVERLAPPING_SEGMENT;
        if (seg->time_stream_byte_offset > h->time_stream_offset + h->time_stream_bytes) return BP011_ERR_INVALID_OFFSET;
        if (seg->time_stream_bytes > (h->time_stream_offset + h->time_stream_bytes) - seg->time_stream_byte_offset) {
            return BP011_ERR_INVALID_OFFSET;
        }
        expected_time_cursor = seg->time_stream_byte_offset + seg->time_stream_bytes;

        // 5. Confinamiento y no solapamiento en el stream de valores
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

## 6. Seguridad Aritmética de Bitstream (NC-C05 Parte A)

### 6.1 Códigos de Estado Formales
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
    BP011_ERR_INVALID_OFFSET     = -9,
    BP011_ERR_UNKNOWN_FLAGS      = -10,
    BP011_ERR_EMPTY_SERIES       = -11,
    BP011_ERR_OVERLAPPING_SEGMENT= -12,
    BP011_ERR_UNALIGNED_BUFFER   = -13
} bp011_status_t;
```

### 6.2 Inicialización y Operaciones Seguras
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

bp011_status_t bw_safe_write_bits(bit_writer_safe_t *bw, uint64_t val, uint8_t n_bits) {
    if (!bw || !bw->buf) return BP011_ERR_NULL_POINTER;
    if (n_bits > 64) return BP011_ERR_INVALID_ARG;
    if (n_bits == 0) return BP011_OK;

    uint64_t max_bits = bw->cap_bytes * 8ULL;
    if (bw->bit_pos > max_bits || n_bits > max_bits - bw->bit_pos) return BP011_ERR_BUFFER_OVERFLOW;

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

bp011_status_t br_safe_init(bit_reader_safe_t *br, const uint8_t *buf, uint64_t size_bytes) {
    if (!br || !buf) return BP011_ERR_NULL_POINTER;
    if (size_bytes == 0 || size_bytes > BP011_MAX_STREAM_BYTES) return BP011_ERR_INVALID_ARG;
    br->buf = buf;
    br->size_bytes = size_bytes;
    br->bit_pos = 0;
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

## 7. Windows Unbuffered I/O y Tratamiento del Último Bloque (NC-C05B)

### 7.1 Consulta de Alineación Asociada al Volumen del Archivo
Para evitar consultar la unidad de trabajo incorrecta, se extrae la ruta del volumen del archivo específico mediante `GetVolumePathNameA`:

```c
typedef struct {
    uint32_t sector_size;      // Tamaño mínimo de transferencia (BytesPerLogicalSector)
    uint32_t physical_sector;  // Tamaño físico (BytesPerPhysicalSector)
    uint32_t buffer_align;     // Alineación requerida para punteros de memoria
} bp011_device_alignment_t;

bp011_status_t bp011_query_device_alignment(const char *filepath, HANDLE hFile, bp011_device_alignment_t *align) {
    if (!filepath || !align) return BP011_ERR_NULL_POINTER;

    // 1. Consulta dinámica a través del handle
    STORAGE_PROPERTY_QUERY query;
    memset(&query, 0, sizeof(query));
    query.PropertyId = StorageAccessAlignmentProperty;
    query.QueryType = PropertyStandardQuery;

    STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR desc;
    DWORD bytes_returned = 0;

    if (hFile != INVALID_HANDLE_VALUE &&
        DeviceIoControl(hFile, IOCTL_STORAGE_QUERY_PROPERTY,
                        &query, sizeof(query),
                        &desc, sizeof(desc),
                        &bytes_returned, NULL)) {
        align->sector_size = desc.BytesPerLogicalSector ? desc.BytesPerLogicalSector : 512;
        align->physical_sector = desc.BytesPerPhysicalSector ? desc.BytesPerPhysicalSector : 4096;
        align->buffer_align = desc.BytesPerPhysicalSector ? desc.BytesPerPhysicalSector : 4096;
        return BP011_OK;
    }

    // 2. Fallback riguroso consultando el volumen real del archivo
    char volume_path[MAX_PATH];
    if (GetVolumePathNameA(filepath, volume_path, MAX_PATH)) {
        DWORD sectors_per_cluster, bytes_per_sector, free_clusters, total_clusters;
        if (GetDiskFreeSpaceA(volume_path, &sectors_per_cluster, &bytes_per_sector, &free_clusters, &total_clusters)) {
            align->sector_size = bytes_per_sector;
            align->physical_sector = bytes_per_sector;
            align->buffer_align = bytes_per_sector;
            return BP011_OK;
        }
    }

    align->sector_size = 4096;
    align->physical_sector = 4096;
    align->buffer_align = 4096;
    return BP011_OK;
}
```

### 7.2 Comprobación de Alineación de Memoria
El arnés verifica explícitamente que la dirección devuelta por `VirtualAlloc` satisfaga la alineación de buffer exigida por el handle:
```c
uint8_t *raw_buf = (uint8_t*)VirtualAlloc(NULL, alloc_bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
if (!raw_buf) return BP011_ERR_NULL_POINTER;
if (((uintptr_t)raw_buf & (align.buffer_align - 1)) != 0) {
    VirtualFree(raw_buf, 0, MEM_RELEASE);
    return BP011_ERR_UNALIGNED_BUFFER;
}
```

### 7.3 Procedimiento Seguro para el Último Bloque de Archivo
Con `FILE_FLAG_NO_BUFFERING`, `ReadFile` exige que la longitud de lectura sea un múltiplo entero del tamaño del sector lógico. Si una lectura intenta cruzar el final del archivo, `ReadFile` falla con `ERROR_HANDLE_EOF`.

Para evitar este fallo:
1. Se consulta el tamaño físico exacto del archivo en bytes: $F_{\text{size}}$ (`GetFileSizeEx`).
2. Se calcula el límite de archivo redondeado hacia arriba al sector lógico:
   $$F_{\text{aligned}} = \left\lceil \frac{F_{\text{size}}}{A} \right\rceil \times A, \qquad \text{donde } A = \text{align.sector\_size}$$
3. Para una lectura en offset $O$ de longitud $L$:
   - Offset alineado hacia abajo: $O_{\text{aligned}} = \lfloor O / A \rfloor \times A$.
   - Desplazamiento relativo: $\Delta O = O - O_{\text{aligned}}$.
   - Longitud alineada hacia arriba: $L_{\text{aligned}} = \lceil (\Delta O + L) / A \rceil \times A$.
   - **Guarda Estricta de Fin de Archivo:**  
     Si $O_{\text{aligned}} + L_{\text{aligned}} > F_{\text{aligned}}$, se acota la lectura a:
     $$L_{\text{read}} = F_{\text{aligned}} - O_{\text{aligned}}$$
   - `ReadFile` lee exactamente $L_{\text{read}}$ bytes sin fallar.
   - La cantidad de bytes válidos disponibles en el buffer para el descompresor es:
     $$\text{valid\_bytes} = \min(L, F_{\text{size}} - O)$$
   - Los bytes sobrantes de padding en el sector final se descartan de forma segura.

---

## 8. Organización Modular y Plan de Aislamiento Experimental

Toda la implementación reside exclusivamente en `tests/experiments/bp011/`:

| Módulo / Archivo | Responsabilidad Técnica |
| :--- | :--- |
| `bp011_types.h` | Cabecera canónica de 64B, Little-Endian helpers, validaciones completas, `bp011_make_mask64` y `bp011_status_t` |
| `timeseries_bitstream_safe.h` | Mapeadores de bits seguros con guarda incondicional `cap <= UINT64_MAX / 8` |
| `timeseries_timestamp_codec.h` | Códec temporal unificado R15.10 (microsegundos en `int64_t`) |
| `gorilla_canonical.h` | Baseline M1 Gorilla canónico F64 (Pelkonen 2015) con máquina de estados completa y variante F32 |
| `chimp_canonical.h` | Baseline M2 Chimp canónico F64 (Liakos 2022) y variante F32 |
| `chimp128_canonical.h` | Baseline M3 Chimp128 canónico F64 según Figura 8 (tabla hash $2^{14}$, ventana $\le 128$, umbral $tz > 13$) |
| `binpack_timeseries_adapter.h` | Adaptador composicional de BinPack con transposición escalar/SIMD |
| `binpack_channel_dedicated.h` | Modelo M7 con tabla de saltos de canales y compresión de canales independientes |
| `bp011_benchmark_harness.c` | Arnés de medición con consulta de alineación por volumen y protocolo de último bloque |

---

## 9. Conclusión y Petición Formal

El diseño técnico presentado en esta **Revisión R5** resuelve de forma definitiva y matemáticamente concluyente cada una de las objeciones del Auditor Líder:
1. Máquina de estados de Gorilla con máscara segura sin UB (`bp011_make_mask64`), codificación biyectiva de `len` y ceros iniciales coherentes.
2. Chimp128 canónico modelado de forma literal según la Figura 8 de Liakos et al., con umbral estricto $tz_c > 13$ y flujo exacto de bits.
3. Validación exhaustiva del contenedor: presencia obligatoria de streams, coherencia bits/bytes, padding cero y flags desconocidos.
4. Confinamiento absoluto de segmentos C-MAPSS con longitudes independientes y comprobación de no solapamiento secuencial de muestras e intervalos de bytes.
5. Windows I/O robusto con consulta de alineación asociada al volumen del archivo, verificación explícita de `VirtualAlloc` y gestión matemática del último bloque de sector.

Por lo tanto, se solicita formalmente la **aprobación definitiva e incondicional del Diseño Técnico R5** y la **autorización para iniciar la fase de codificación experimental** exclusivamente en `tests/experiments/bp011/`.
