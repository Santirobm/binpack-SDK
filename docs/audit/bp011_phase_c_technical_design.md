# Especificación y Diseño Técnico R3 — Hito BP-011 / Fase C
## Arquitectura de Instrumentación, Baselines Composicionales y Arnés de Medición

**Proyecto:** BinPack Enterprise SDK  
**Hito:** BP-011  
**Fase:** Fase C — Auditoría de Instrumentación y Diseño de Arneses (Revisión R3)  
**Documento Rector:** `docs/audit/bp011_phase_c_technical_design.md`  
**Rama:** `audit/bp1-core-safety`  
**Fecha:** 12 de noviembre de 2026  
**Estado:** ESPECIFICACIÓN TÉCNICA DEFINITIVA DE DISEÑO DE FASE C (REVISIÓN R3)  
**Gobernanza:** *Code Freeze* preservado al 100% (cero modificaciones en `src/`, `include/` y `deps/`; sin compilación ni ejecución en esta fase).

---

## Control de Cambios de la Revisión R3 (Subsanación Integral de Observaciones)

En estricta conformidad con el dictamen de auditoría emitido sobre la Revisión R2, la presente **Revisión R3** incorpora las siguientes correcciones metodológicas, algorítmicas y de seguridad:

1. **NC-C01 (Desambiguación Rigurosa de Baselines en Tres Niveles):**
   - **Nivel 1.A (Baselines Nativos Canónicos de Literatura - Float64):** Se define formalmente la reproducción exacta de los formatos originales de la literatura en doble precisión (IEEE 754 Float64). Gorilla utiliza una referencia temporal de bloque de 64 bits ($t_{\text{ref}}$), un delta inicial de 14 bits ($D_0 = t_0 - t_{\text{ref}}$) y deltas-of-deltas con prefijos variables según Pelkonen et al. (2015). Chimp y Chimp128 operan sobre Float64 según Liakos et al. (VLDB 2022).
   - **Nivel 1.B (Variantes Adaptadas de Precisión Simple - Float32):** Se identifican y reportan explícitamente como adaptaciones (`Gorilla_Float32_Variant`, `Chimp_Float32_Variant`, `Chimp128_Float32_Variant`), documentando todas sus desviaciones respecto a los papers canónicos.
   - **Nivel 2 (Baselines Normalizados Composicionales):** Evaluación equitativa de todos los códecs de valores bajo el formato temporal común industrial R15.10 (microsegundos en `int64_t`).

2. **NC-C02 (Chimp128 Canónico Fiel a Liakos et al. 2022 y Delimitación de Variantes):**
   - **Chimp128 Canónico:** Implementación fiel con tabla hash auxiliar de tamaño $2^{14} = 16,384$ entradas indexada por los 14 bits menos significativos del valor flotante.
   - **Condición de Ventana y Fallback:** Se valida si la entrada de la tabla hash reside dentro de la ventana de 128 muestras ($i - c < 128$). De no ser así, se recurre automáticamente al valor inmediatamente anterior ($v_{i-1}$).
   - **Decisión de Umbral de Coste:** Solo se paga el índice de 7 bits si el ahorro de bits de XOR compensa dicho coste, o en caso de coincidencia exacta ($XOR = 0$, caso `00` en 9 bits).
   - **Variante Exhaustiva:** Cualquier implementación que escanee los 128 candidatos exhaustivamente se denomina y aísla formalmente como `Chimp128_ExhaustiveVariant` (estudio de ablación no canónico).

3. **NC-C03 (Contrato Binario Completo, Endianness, Tamaños y CRC32):**
   - **Serialización Little-Endian Explícita:** Se desacopla el formato en memoria de la representación binaria mediante rutinas explícitas de serialización byte a byte (`bp011_write_le16/32/64`, `bp011_read_le16/32/64`).
   - **Cabecera Canónica de 56 Bytes:** `bp011_block_header_t` define offsets y longitudes independientes para la tabla de canales (`channel_table`), directorio de series (`series_dir`), stream de tiempo y streams de valores.
   - **Prevención de Underflow:** Verificación estricta `total_size >= BP011_HEADER_SIZE_BYTES` antes de cualquier sustracción o cálculo de offsets.
   - **Definición Inequívoca de CRC32:** CRC-32 (IEEE 802.3, poly 0xEDB88320) calculado sobre los bytes $[0, \text{total\_file\_bytes}-1]$ con el campo `crc32` de la cabecera forzado a cero durante el cómputo.

4. **NC-C04 (Directorio de Series C-MAPSS, Complejidad y Timestamps):**
   - **Estructura Formal:** `bp011_series_segment_t` (24 bytes) con ordenamiento estricto y validación de unicidad monótona creciente ($\text{unit\_id}[k] < \text{unit\_id}[k+1]$).
   - **Complejidad Demostrada:** Acceso $O(1)$ garantizado para identificadores continuos densos ($1 \le \text{unit\_id} \le M$) mediante indexación directa con comprobación de límites; búsqueda binaria $O(\log M)$ para identificadores arbitrarios o dispersos.
   - **Semántica de `start_cycle`:** Corresponde al ciclo operativo inicial del motor; la descompresión selectiva parte de `start_cycle` reiniciando el estado temporal (`has_prev_delta = false`), permitiendo reconstrucción completa sin decodificar series previas.

5. **NC-C05 (Seguridad Aritmética de Bitstream y Medición Multicapa Cold-Cache):**
   - **Aritmética Segura sin Sumas Desbordables:** Sustitución de `pos + n <= max` por `n > max - pos`. Retorno unívoco de códigos de estado `bp011_status_t` en lectura de bits (`br_safe_read_bit`), eliminando la ambigüedad entre un bit 0 y un error.
   - **Metodología Multicapa de Caché:** Taxonomía formal en Nivel 0 (Caché CPU L1/L2/L3), Nivel 1 (Page Cache del SO) y Nivel 2 (Controlador y DRAM del SSD/NVMe).
   - **Procedimiento de Medición:** In-Memory Throughput como métrica primaria; End-to-End I/O como métrica secundaria mediante `FILE_FLAG_NO_BUFFERING` con buffers alineados a 4096 bytes (`VirtualAlloc`). Desalojo empírico de CPU mediante barrido de $2 \times \text{L3\_size}$ (mínimo 256 MB), documentando explícitamente los límites de control sobre cachés de hardware.

---

## 1. Introducción, Mandato y Principios Rectores

El presente documento constituye la especificación de ingeniería completa para la instrumentación experimental del benchmark de series temporales industriales en el marco del Hito BP-011.

### 1.1 Principios Rectores Innegociables
1. **Simetría y Paridad Estricta de Carga Útil:**  
   Queda terminantemente prohibido comparar un motor que solo comprime valores flotantes frente a otro que almacena timestamps, cabeceras e índices. Todos los motores evaluados deberán comprimir, empaquetar y reconstruir **exactamente la misma información**: tuplas completas $(t_i, v_{i, 0}, \dots, v_{i, K-1})$ con sus metadatos y delimitadores de bloque.
2. **Aislamiento Experimental Absoluto:**  
   Toda la implementación residirá exclusivamente en el directorio experimental aislado [`tests/experiments/bp011/`](file:///C:/Users/sgi.lm/Desktop/binpack-SDK/tests/experiments/bp011/). El código productivo en `src/`, `include/` y las librerías en `deps/` permanecerán inalterados bajo congelamiento estricto.
3. **Seguridad y Contención de Memoria:**  
   Todo el manejo de flujos de bits se implementará con envoltorios seguros que impidan desbordamientos de buffer en lectura y escritura, sin sumas aritméticamente desbordables.
4. **Cero Compilación y Cero Ejecución Prematura:**  
   Durante la Fase C solo se diseñará y codificará el arnés. La compilación, verificación estática y ejecución experimental requerirán sus respectivas aprobaciones en las Fases D y E.

---

## 2. Arquitectura de Baselines y Desambiguación (NC-C01)

Para garantizar una comparabilidad científica rigurosa y libre de sesgos metodológicos, la evaluación experimental se estructura en **tres niveles formalmente diferenciados**:

```
┌────────────────────────────────────────────────────────────────────────────────────────────────┐
│                         ARQUITECTURA DE EVALUACIÓN EXPERIMENTAL (BP-011)                       │
├────────────────────────────────────────────────────────────────────────────────────────────────┤
│  NIVEL 1.A: Baselines Nativos Canónicos de Literatura (Float64 / Doble Precisión)              │
│    • M1_Gorilla_Canonical_F64: Pelkonen et al. (2015) [t_ref 64b + D0 14b + DoD + F64 Gorilla]│
│    • M2_Chimp_Canonical_F64:   Liakos et al. (2022) [Gorilla t_ref 64b + F64 Chimp]           │
│    • M3_Chimp128_Canonical_F64:Liakos et al. (2022) [Gorilla t_ref 64b + F64 Chimp128 (2^14)] │
├────────────────────────────────────────────────────────────────────────────────────────────────┤
│  NIVEL 1.B: Variantes Adaptadas de Precisión Simple (Float32)                                  │
│    • Gorilla_Float32_Variant:  Adaptación de Gorilla a Float32 con timestamp 32/14/DoD         │
│    • Chimp_Float32_Variant:    Chimp32 según Liakos et al. (2022) sobre Float32                │
│    • Chimp128_Float32_Variant: Chimp128 adaptado a Float32 (ventana 128, tabla hash 2^14/2^8)  │
├────────────────────────────────────────────────────────────────────────────────────────────────┤
│  NIVEL 2: Baselines Normalizados Composicionales (Protocolo Industrial R15.10)                 │
│    • Timestamp Unificado: int64_t microsegundos con codificación R15.10 (prefijos 0/10/110...) │
│    • Códecs de Valores Evaluados:                                                              │
│        - Gorilla_Normalized, Chimp_Normalized, Chimp128_Normalized                             │
│        - BinPack_Raw, BinPack_Transpose_Scalar, BinPack_Transpose_SIMD                         │
│        - BinPack_Channel_Dedicated (M7) con saltos independientes                              │
└────────────────────────────────────────────────────────────────────────────────────────────────┘
```

### 2.1 Nivel 1.A: Baselines Nativos Canónicos de Literatura (Float64)
Reproducen de forma fidedigna las especificaciones publicadas en los artículos originales:

1. **`M1_Gorilla_Canonical_F64` (Pelkonen et al., VLDB 2015):**
   - **Timestamp:** Cada bloque contiene un timestamp de referencia de 64 bits ($t_{\text{ref}}$) alineado a la cabecera. El primer timestamp se codifica como un delta de 14 bits ($D_0 = t_0 - t_{\text{ref}}$). Los timestamps subsecuentes se codifican mediante delta-of-delta $D = (t_i - t_{i-1}) - (t_{i-1} - t_{i-2})$:
     - $D = 0$: bit `0` (1 bit).
     - $-63 \le D \le 64$: bits `10` + valor en 7 bits (9 bits).
     - $-255 \le D \le 256$: bits `110` + valor en 9 bits (12 bits).
     - $-2047 \le D \le 2048$: bits `1110` + valor en 12 bits (16 bits).
     - Fuera de rango: bits `1111` + valor en 32 bits (36 bits).
   - **Valores:** IEEE 754 Float64 (64 bits). El primer valor se escribe en 64 bits literales. Los siguientes mediante XOR con el valor precedente $v_{i-1}$, registrando ceros iniciales (5 bits) y longitud de bits significativos (6 bits).

2. **`M2_Chimp_Canonical_F64` (Liakos et al., VLDB 2022, Sección 3):**
   - **Timestamp:** Idéntico al formato canónico de Gorilla (Pelkonen 2015) para aislar el rendimiento del códec de valores.
   - **Valores:** IEEE 754 Float64 comprimidos con el algoritmo Chimp original: supresión de ceros iniciales y finales, cuantización de ceros iniciales a múltiplos de 8 bits (3 bits para $lz_{\text{quant}}$) y ventana dinámica basada en el valor precedente $v_{i-1}$.

3. **`M3_Chimp128_Canonical_F64` (Liakos et al., VLDB 2022, Sección 4):**
   - **Timestamp:** Gorilla canónico F64.
   - **Valores:** IEEE 754 Float64 comprimidos con Chimp128 canónico mediante tabla hash auxiliar de tamaño $2^{14}$, buffer circular de 128 elementos y decisión de coste/beneficio (descrito en Sección 3).

### 2.2 Nivel 1.B: Variantes Adaptadas de Precisión Simple (Float32)
Dado que los datasets industriales de vibración de rodamientos (NASA IMS) y turbofanes (NASA C-MAPSS) se representan de forma nativa en precisión simple (Float32), se definen formalmente las variantes adaptadas para evitar penalizar la compresión duplicando artificialmente el espacio a 64 bits:

- **`Gorilla_Float32_Variant`:** Mantiene la lógica de Pelkonen et al. adaptando la codificación de valores a 32 bits (ceros iniciales en 5 bits, longitud en 5 bits).
- **`Chimp_Float32_Variant` (Chimp32):** Implementación de la variante single-precision descrita en la Sección 5.1 de Liakos et al., utilizando cuantización de ceros iniciales con múltiplos adaptados.
- **`Chimp128_Float32_Variant`:** Chimp128 operando sobre palabras de 32 bits, con buffer circular de 128 elementos (`uint32_t ring[128]`) y tabla hash auxiliar.
- **Declaración Normativa:** En todas las tablas de resultados, estas variantes se etiquetarán explícitamente como `Variant_F32` y no como reproducción canónica de la literatura.

### 2.3 Nivel 2: Baselines Composicionales Normalizados (Protocolo R15.10)
En este nivel se elimina cualquier disparidad debida al manejo temporal:
- **Timestamp Unificado:** Todos los motores comprimen timestamps de 64 bits en microsegundos según la codificación oficial de R15.10 (prefijo `0` para $\Delta=0$, prefijo `10` para $\Delta \in [1, 255]$, etc.).
- **Contenedor Unificado:** Todos los códecs se encapsulan bajo la misma cabecera binaria `bp011_block_header_t` (Sección 4).
- **Motores Evaluados:**
  1. `Gorilla_Normalized`: Timestamps R15.10 + Valores con códec Gorilla.
  2. `Chimp_Normalized`: Timestamps R15.10 + Valores con códec Chimp.
  3. `Chimp128_Normalized`: Timestamps R15.10 + Valores con códec Chimp128.
  4. `BinPack_Raw`: Timestamps R15.10 + Valores sin comprimir (empaquetado plano).
  5. `BinPack_Transpose_Scalar`: Timestamps R15.10 + Transposición escalar por planos de bytes + Compresión BinPack.
  6. `BinPack_Transpose_SIMD`: Timestamps R15.10 + Transposición vectorizada AVX2/NEON + Compresión BinPack.
  7. `BinPack_Channel_Dedicated` (M7): Timestamps R15.10 + Transposición + Compresión con Tabla de Saltos de Canales independientes.

---

## 3. Especificación Rigurosa y Trazabilidad de Chimp128 (NC-C02)

Para cerrar definitivamente la no conformidad NC-C02, se adopta de forma estricta la arquitectura y el mecanismo de selección descritos en el artículo original de **Liakos et al. (VLDB 2022, Sección 4: Chimp128)**.

### 3.1 Estructuras de Datos del Algoritmo Chimp128 Canónico
Chimp128 no realiza un escaneo exhaustivo lineal sobre el buffer de 128 elementos. Emplea una **estructura auxiliar de indexación hash** para localizar en $O(1)$ el valor más reciente con bits finales idénticos:

```c
#define CHIMP128_RING_SIZE        128
#define CHIMP128_HASH_TABLE_SIZE  16384 // 2^14 entradas

typedef struct {
    uint64_t ring_buf[CHIMP128_RING_SIZE];        // Últimos 128 valores históricos (64 bits)
    uint32_t ring_sample_idx[CHIMP128_RING_SIZE]; // Índice de muestra absoluto del valor
    int32_t  hash_table[CHIMP128_HASH_TABLE_SIZE];// Último sample_idx visto para cada clave de 14 bits
    uint64_t prev_val;                           // Valor inmediatamente anterior (v_{i-1})
    uint8_t  prev_lz_quant;                      // Cuantización de ceros iniciales previa
    uint8_t  prev_tz;                            // Ceros finales previos
} chimp128_f64_state_t;
```

Para la variante de precisión simple `Chimp128_Float32_Variant`, la estructura almacena `uint32_t ring_buf[128]` y una tabla hash de $2^{14} = 16,384$ entradas (o $2^{10}$ adaptada) indexada por los bits menos significativos del flotante de 32 bits.

### 3.2 Algoritmo de Búsqueda y Decisión de Coste/Beneficio (Liakos et al. 2022)
Para cada muestra $i$ con valor actual $v_i$:

1. **Extracción de Clave Hash:**  
   Se toman los 14 bits menos significativos del valor:
   $$\text{key} = \text{as\_uint64}(v_i) \ \& \ 0x3FFF$$

2. **Búsqueda del Candidato Histórico en $O(1)$:**  
   Se consulta el índice previo en la tabla hash:
   $$c = \text{hash\_table}[\text{key}]$$

3. **Verificación de Ventana Temporal (128 Muestras):**
   - El candidato es válido **si y solo si**:
     $$c \ge 0 \quad \land \quad (i - c) < 128$$
   - Si la condición no se cumple (la entrada estaba vacía o el valor quedó fuera de la ventana de 128 muestras), el candidato histórico **se descarta**. En tal caso, la referencia por defecto es el valor inmediatamente anterior:
     $$v_{\text{ref}} = v_{i-1}, \quad \text{is\_ring\_match} = \text{false}$$

4. **Evaluación de Coste/Beneficio y Umbral de Compresión:**
   Si el candidato $c$ es válido dentro de la ventana, se compara la compresión de $v_i$ referenciada a $v_c = \text{ring\_buf}[c \bmod 128]$ frente a la compresión referenciada a $v_{i-1}$:
   - **Caso Coincidencia Exacta ($v_i \oplus v_c = 0$):**  
     Se selecciona incondicionalmente el candidato histórico. Se emite el caso `00` (2 bits) seguido del índice circular de 7 bits:
     $$\text{coste} = 2 + 7 = 9 \text{ bits}$$
     (Enorme ahorro frente a recodificar el valor completo).
   - **Caso General ($v_i \oplus v_c \ne 0$):**  
     Se calcula el XOR con el candidato histórico ($XOR_c = v_i \oplus v_c$) y con el valor previo ($XOR_{\text{prev}} = v_i \oplus v_{i-1}$).
     Se cuantifican los ceros iniciales y finales de ambos. La referencia histórica $v_c$ **solo se selecciona si el ahorro de bits en los datos significativos supera el sobrecoste de 7 bits requerido para transmitir el índice**:
     $$\text{Bits}(XOR_c) + 7 < \text{Bits}(XOR_{\text{prev}})$$
     Si no supera este umbral, se utiliza $v_{i-1}$ como referencia, emitiendo el caso correspondiente sin pagar el índice de 7 bits.

5. **Actualización de Estado:**
   - Se almacena $v_i$ en `ring_buf[i % 128]` y su índice `ring_sample_idx[i % 128] = i`.
   - Se actualiza la tabla hash: `hash_table[key] = i`.
   - Se actualiza `prev_val = v_i`.

### 3.3 Delimitación de Variantes Algorítmicas
- **`Chimp128_Canonical`:** Implementación estricta del algoritmo de Liakos et al. con tabla hash de $2^{14}$ entradas, comprobación de ventana de 128 muestras y regla de decisión de umbral de coste.
- **`Chimp128_ExhaustiveVariant`:** Variante experimental de laboratorio que sustituye la tabla hash por un escaneo exhaustivo lineal sobre los 128 elementos históricos buscando maximizar $\text{clz32}$ o $\text{clz64}$. Esta variante se aísla exclusivamente como estudio de ablación y no se reporta como baseline oficial de la literatura.

### 3.4 Tabla de Trazabilidad Algorítmica con Liakos et al. (VLDB 2022)

| Parámetro / Componente | Especificación Canónica Chimp128 (VLDB 2022) | Implementación BinPack BP-011 (`Chimp128_Canonical`) |
| :--- | :--- | :--- |
| **Tipo de Dato Nativo** | Double Precision (IEEE 754 Float64) | `uint64_t` / `double` en Nivel 1.A; adaptado en Nivel 1.B |
| **Tamaño de Ventana** | 128 muestras anteriores ($W = 128$) | `CHIMP128_RING_SIZE = 128`, índice de 7 bits ($2^7 = 128$) |
| **Estructura Auxiliar** | Tabla de indexación directa de $2^{14}$ entradas | `int32_t hash_table[16384]` inicializada en -1 |
| **Clave de Acceso** | 14 bits menos significativos del valor | `key = as_uint64(val) & 0x3FFF` |
| **Condición de Ventana** | $current\_index - candidate\_index < 128$ | `(sample_idx - candidate_idx) < 128` |
| **Fallback ante Fallo** | Recurrir al valor anterior $v_{i-1}$ | Si condición falsa, `ref_val = prev_val`, sin coste de 7 bits |
| **Umbral de Decisión** | Ahorro en ceros/longitud > 7 bits | Evaluación de coste: `bits(XOR_c) + 7 < bits(XOR_prev)` |
| **Coincidencia Exacta** | Caso `00` + 7 bits de índice ($XOR = 0$) | Caso `00` (2 bits) + `index` (7 bits) = 9 bits en total |

---

## 4. Contrato Binario de Contenedor y Directorio de Series (NC-C03 y NC-C04)

### 4.1 Serialización Little-Endian Explícita
Para eliminar cualquier dependencia de la arquitectura del procesador o del comportamiento de `#pragma pack`, toda la serialización y deserialización de cabeceras binarias y tablas de metadatos se realiza **byte a byte en orden Little-Endian estricto**:

```c
static inline void bp011_write_le16(uint8_t *dst, uint16_t val) {
    dst[0] = (uint8_t)(val & 0xFF);
    dst[1] = (uint8_t)((val >> 8) & 0xFF);
}

static inline void bp011_write_le32(uint8_t *dst, uint32_t val) {
    dst[0] = (uint8_t)(val & 0xFF);
    dst[1] = (uint8_t)((val >> 8) & 0xFF);
    dst[2] = (uint8_t)((val >> 16) & 0xFF);
    dst[3] = (uint8_t)((val >> 24) & 0xFF);
}

static inline void bp011_write_le64(uint8_t *dst, uint64_t val) {
    for (int i = 0; i < 8; i++) dst[i] = (uint8_t)((val >> (i * 8)) & 0xFF);
}

static inline uint16_t bp011_read_le16(const uint8_t *src) {
    return (uint16_t)src[0] | ((uint16_t)src[1] << 8);
}

static inline uint32_t bp011_read_le32(const uint8_t *src) {
    return (uint32_t)src[0] | ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

static inline uint64_t bp011_read_le64(const uint8_t *src) {
    uint64_t val = 0;
    for (int i = 0; i < 8; i++) val |= ((uint64_t)src[i] << (i * 8));
    return val;
}
```

### 4.2 Cabecera Canónica de Bloque (56 Bytes)
La cabecera binaria `bp011_block_header_t` delimita con precisión nanométrica cada sección del contenedor, eliminando ambigüedades sobre la ubicación de metadatos:

```c
#define BP011_BLOCK_MAGIC          0x4B504E42 // "BNPK" en Little-Endian
#define BP011_FORMAT_VERSION       0x0001
#define BP011_HEADER_SIZE_BYTES    56

typedef struct {
    uint32_t magic;                 // 0x00: 0x4B504E42 ("BNPK")
    uint16_t version;               // 0x04: 0x0001
    uint16_t flags;                 // 0x06: Flags de configuración (bit 0: tiene tabla canales, bit 1: tiene dir series)
    uint32_t num_channels;          // 0x08: Cantidad de canales (K)
    uint32_t sample_count;          // 0x0C: Cantidad de muestras temporales (N)
    uint32_t channel_table_offset;  // 0x10: Desplazamiento en bytes de la tabla de saltos M7
    uint32_t channel_table_bytes;   // 0x14: Tamaño en bytes de la tabla de saltos (K * 12)
    uint32_t series_dir_offset;     // 0x18: Desplazamiento en bytes del directorio de series C-MAPSS
    uint32_t series_dir_bytes;      // 0x1C: Tamaño en bytes del directorio de series (M * 24)
    uint32_t time_stream_offset;    // 0x20: Desplazamiento en bytes del stream de timestamps
    uint32_t time_stream_bytes;     // 0x24: Longitud en bytes del stream de timestamps
    uint32_t time_stream_bits;      // 0x28: Cantidad exacta de bits válidos en timestamps
    uint32_t values_stream_offset;  // 0x2C: Desplazamiento en bytes del stream de valores
    uint32_t values_stream_bytes;   // 0x30: Longitud en bytes del stream de valores
    uint32_t values_stream_bits;    // 0x34: Cantidad exacta de bits válidos en valores
    uint32_t total_file_bytes;      // 0x38: Tamaño total del bloque serializado en bytes
    uint32_t crc32;                 // 0x3C: CRC-32 (IEEE 802.3) del bloque completo (calculado con crc32=0)
} bp011_block_header_t;
```

### 4.3 Layout Físico del Bloque Binario
El bloque binario se estructura secuencial y determinantemente de la siguiente forma:

```
┌────────────────────────────────────────────────────────────────────────┐
│                      LAYOUT FÍSICO DEL CONTENEDOR BP-011               │
├────────────────────────────────────────────────────────────────────────┤
│ 1. Cabecera Fija (56 Bytes) Little-Endian                              │
├────────────────────────────────────────────────────────────────────────┤
│ 2. Tabla de Saltos de Canales (M7) [Opcional si flags & 0x01]          │
│    Offset: channel_table_offset, Longitud: channel_table_bytes (K * 12B)│
├────────────────────────────────────────────────────────────────────────┤
│ 3. Directorio de Series C-MAPSS [Opcional si flags & 0x02]             │
│    Offset: series_dir_offset, Longitud: series_dir_bytes (M * 24B)     │
├────────────────────────────────────────────────────────────────────────┤
│ 4. Stream de Timestamps                                                │
│    Offset: time_stream_offset, Longitud: time_stream_bytes             │
├────────────────────────────────────────────────────────────────────────┤
│ 5. Stream de Valores (Canales entrelazados o dedicados)                │
│    Offset: values_stream_offset, Longitud: values_stream_bytes         │
└────────────────────────────────────────────────────────────────────────┘
```

### 4.4 Definición Inequívoca del Cálculo de CRC32
- **Algoritmo:** CRC-32 estándar (polinomio IEEE 802.3 `0xEDB88320`, valor inicial `0xFFFFFFFF`, XOR de salida `0xFFFFFFFF`).
- **Rango de Cobertura:** Cubre **todos los bytes del archivo binario** desde el byte 0 hasta `total_file_bytes - 1`.
- **Tratamiento del Campo CRC:**
  1. Durante la serialización: se escribe la cabecera completa con el campo `crc32` establecido temporalmente a `0x00000000`. Se calcula el CRC32 sobre todo el buffer serializado $[0, \text{total\_file\_bytes} - 1]$ y se escribe el valor resultante en `offset 0x3C` mediante `bp011_write_le32`.
  2. Durante la validación: el lector lee el valor `crc_expected`, sobrescribe temporalmente los 4 bytes del campo con cero en su copia de trabajo, calcula el CRC32 sobre el buffer completo y compara:
     ```c
     if (crc_computed != crc_expected) return BP011_ERR_CORRUPT_CRC;
     ```

### 4.5 Reglas Estrictas de Validación y Prevención de Underflow
Para impedir cualquier desbordamiento o subdesbordamiento aritmético, todo acceso a los buffers y offsets se sujeta a las siguientes comprobaciones incondicionales previas:

```c
// 1. Validación de tamaño mínimo del bloque
if (total_size_bytes < BP011_HEADER_SIZE_BYTES) {
    return BP011_ERR_BUFFER_UNDERFLOW;
}

// 2. Validación de coherencia de tamaño total declarado
if (header.total_file_bytes > total_size_bytes) {
    return BP011_ERR_STREAM_TRUNCATED;
}

// 3. Validación de no superposición y límites de secciones (sin sumas desbordables)
#define CHECK_SECTION_BOUNDS(offset, bytes, max_len)     do {         if ((offset) < BP011_HEADER_SIZE_BYTES) return BP011_ERR_INVALID_OFFSET;         if ((offset) > (max_len)) return BP011_ERR_INVALID_OFFSET;         if ((bytes) > (max_len) - (offset)) return BP011_ERR_INVALID_OFFSET;     } while (0)

CHECK_SECTION_BOUNDS(header.channel_table_offset, header.channel_table_bytes, header.total_file_bytes);
CHECK_SECTION_BOUNDS(header.series_dir_offset, header.series_dir_bytes, header.total_file_bytes);
CHECK_SECTION_BOUNDS(header.time_stream_offset, header.time_stream_bytes, header.total_file_bytes);
CHECK_SECTION_BOUNDS(header.values_stream_offset, header.values_stream_bytes, header.total_file_bytes);
```

### 4.6 Tabla de Saltos de Canales para M7 (`BinPack_Channel_Dedicated`)
Para el modelo M7, cada uno de los $K$ canales se comprime de forma independiente. Su tabla de localización reside inmediatamente tras la cabecera y consta de $K$ entradas de 12 bytes serializadas en Little-Endian:

```c
typedef struct {
    uint32_t byte_offset; // Offset absoluto dentro del bloque donde inicia el canal
    uint32_t byte_length; // Longitud en bytes del canal comprimido
    uint32_t bit_length;  // Cantidad exacta de bits válidos en el canal
} bp011_channel_entry_t;
```

**Regla de Imputación de Sobrecoste:** El tamaño de esta tabla ($K \times 12$ bytes) se incluye obligatoriamente en $B_{\text{container}}$ y $B_{\text{total}}$, penalizando de forma realista el ratio de compresión de M7 frente a los códecs entrelazados.

### 4.7 Directorio de Series para Telemetría Discontinua (C-MAPSS) (NC-C04)
Para gestionar de forma matemáticamente exacta las series temporales discontinuas de múltiples motores en NASA Turbofan C-MAPSS, el contenedor incluye un Directorio de Series compuesto por $M$ segmentos de 24 bytes:

```c
typedef struct {
    uint32_t unit_id;                  // Identificador del motor (1 a M)
    uint32_t start_sample_idx;         // Índice absoluto de muestra inicial (0 a N-1)
    uint32_t sample_count;             // Cantidad de ciclos/muestras de este motor
    uint32_t start_cycle;              // Ciclo operativo inicial real (ej. ciclo 1)
    uint32_t time_stream_byte_offset;  // Offset donde inicia el stream temporal del motor
    uint32_t values_stream_byte_offset;// Offset donde inicia el stream de valores del motor
} bp011_series_segment_t;
```

#### Validación de Unicidad y Ordenamiento
El directorio se almacena **estrictamente ordenado por `unit_id` en orden monótono creciente**:
$$\text{unit\_id}[k] < \text{unit\_id}[k+1], \quad \forall k \in [0, M-2]$$
Durante la carga de la cabecera, el descompresor valida esta invariante; cualquier duplicidad o desorden devuelve inmediatamente `BP011_ERR_CORRUPT_DIRECTORY`.

#### Complejidad de Búsqueda Formalmente Demostrada
1. **Acceso $O(1)$ para Conjuntos Continuos Densos (C-MAPSS Canónico):**  
   En C-MAPSS FD001 a FD004, los identificadores de motores son enteros densos en el rango continuo $[1, M]$ (por ejemplo, $1 \le \text{unit\_id} \le 100$). Para estos casos, el acceso a la entrada del motor $u$ se realiza en tiempo constante $O(1)$ mediante indexación directa:
   $$\text{idx} = u - 1$$
   previa validación de límites ($1 \le u \le M$) y comprobación de aserción $\text{dir}[\text{idx}].\text{unit\_id} == u$.
2. **Acceso $O(\log M)$ para Conjuntos Dispersos o Arbitrarios:**  
   Si los identificadores no son continuos o presentan saltos, la búsqueda se ejecuta mediante Búsqueda Binaria sobre el array ordenado, garantizando un tiempo de resolución acotado estrictamente por $O(\log M)$ comparaciones.

#### Semántica de `start_cycle` y Reconstrucción Temporal Aislada
- `start_cycle` representa el ciclo operativo físico de la primera muestra de dicho motor (típicamente ciclo 1 en turbofanes).
- Al solicitar la descompresión selectiva del motor $u$, el motor busca la entrada correspondiente, salta directamente a `time_stream_byte_offset` y `values_stream_byte_offset`, y **reinicia estrictamente el estado del decodificador temporal**:
  $$\text{has\_prev\_delta} = \text{false}, \quad \text{prev\_time} = \text{segment.start\_cycle}$$
- El timestamp de cada muestra $s$ ($0 \le s < \text{sample\_count}$) se reconstruye sumando los deltas decodificados a `start_cycle`. Esto permite la recuperación exacta de los ciclos de operación de cualquier motor individual con coste $O(\text{sample\_count}_u)$ **sin descomprimir ni una sola muestra de los motores precedentes**.

---

## 5. Bitstream Seguro y Manejo Robusto de Errores (NC-C05 Parte A)

Para mitigar los defectos estructurales observados en `timeseries_codecs.h` y asegurar cero desbordamientos de memoria en el arnés experimental, se diseñan las estructuras `bit_writer_safe_t` y `bit_reader_safe_t`.

### 5.1 Definición de Códigos de Error Unívocos
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

### 5.2 Estructura y Aritmética Segura del Bitstream
```c
typedef struct {
    uint8_t *buf;
    uint64_t cap_bytes;
    uint64_t bit_pos;
} bit_writer_safe_t;

typedef struct {
    const uint8_t *buf;
    uint64_t size_bytes;
    uint64_t bit_pos;
} bit_reader_safe_t;
```

#### Regla Anti-Desbordamiento en Escritura (Sin Sumas Potencialmente Desbordables)
```c
bp011_status_t bw_safe_write_bits(bit_writer_safe_t *bw, uint64_t val, uint8_t n_bits) {
    if (!bw || !bw->buf) return BP011_ERR_NULL_POINTER;
    if (n_bits > 64) return BP011_ERR_INVALID_ARG;
    if (n_bits == 0) return BP011_OK;

    uint64_t max_bits = bw->cap_bytes * 8ULL;
    // Comprobación segura contra overflow sin sumar bw->bit_pos + n_bits
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
```

#### Regla Anti-Desbordamiento en Lectura con Retorno Inequívoco
Para eliminar la ambigüedad donde un valor devuelto de cero podía representar tanto un bit legítimo 0 como un error de truncamiento, **las rutinas de lectura devuelven obligatoriamente `bp011_status_t` y escriben los datos en punteros de salida**:

```c
bp011_status_t br_safe_read_bit(bit_reader_safe_t *br, uint8_t *out_bit) {
    if (!br || !br->buf || !out_bit) return BP011_ERR_NULL_POINTER;

    uint64_t max_bits = br->size_bytes * 8ULL;
    if (br->bit_pos >= max_bits) {
        return BP011_ERR_STREAM_TRUNCATED;
    }

    uint64_t byte_idx = br->bit_pos >> 3;
    uint8_t bit_idx = 7 - (br->bit_pos & 7);
    *out_bit = (br->buf[byte_idx] >> bit_idx) & 1U;
    br->bit_pos++;
    return BP011_OK;
}

bp011_status_t br_safe_read_bits(bit_reader_safe_t *br, uint8_t n_bits, uint64_t *out_val) {
    if (!br || !br->buf || !out_val) return BP011_ERR_NULL_POINTER;
    if (n_bits > 64) return BP011_ERR_INVALID_ARG;
    if (n_bits == 0) {
        *out_val = 0;
        return BP011_OK;
    }

    uint64_t max_bits = br->size_bytes * 8ULL;
    if (br->bit_pos > max_bits || n_bits > max_bits - br->bit_pos) {
        return BP011_ERR_STREAM_TRUNCATED;
    }

    uint64_t val = 0;
    for (uint8_t i = 0; i < n_bits; i++) {
        uint64_t byte_idx = br->bit_pos >> 3;
        uint8_t bit_idx = 7 - (br->bit_pos & 7);
        uint8_t bit = (br->buf[byte_idx] >> bit_idx) & 1U;
        val = (val << 1) | bit;
        br->bit_pos++;
    }
    *out_val = val;
    return BP011_OK;
}
```

---

## 6. Metodología de Medición Multicapa y Protocolo Cold-Cache (NC-C05 Parte B)

Para satisfacer las exigencias de rigor científico formuladas por el Auditor Líder, se formaliza la taxonomía de la jerarquía de memoria y almacenamiento, delimitando expresamente qué capas son controlables y qué variables son intrínsecamente opacas al software.

### 6.1 Taxonomía de Jerarquía de Memoria
```
┌────────────────────────────────────────────────────────────────────────────────────────────────┐
│                          JERARQUÍA MULTICAPA DE ALMACENAMIENTO Y CACHÉ                         │
├────────────────────────────────────────────────────────────────────────────────────────────────┤
│ Nivel 0: Microarquitectura y Cachés de CPU (L1i/d ~32KB, L2 ~512KB-1MB, L3 ~8MB-64MB)          │
│   • Latencia: 1 a 30 ns.                                                                       │
│   • Control de Software: Parcial/Empírico (mediante desalojo por desplazamiento de memoria).    │
├────────────────────────────────────────────────────────────────────────────────────────────────┤
│ Nivel 1: Memoria Principal (RAM) y Page Cache del Sistema Operativo                            │
│   • Latencia: 50 a 100 ns (RAM) / 1 a 50 µs (Page fault / File mapping).                       │
│   • Control de Software: Totalmente determinista (Unbuffered I/O: FILE_FLAG_NO_BUFFERING/O_DIRECT).│
├────────────────────────────────────────────────────────────────────────────────────────────────┤
│ Nivel 2: Interfaz de Almacenamiento, Controlador y DRAM Integrada del SSD/NVMe                 │
│   • Latencia: 20 a 500 µs.                                                                     │
│   • Control de Software: NULO (Opaco). Los comandos de usuario no pueden vaciar la DRAM interna│
│     del controlador NVMe sin privilegios de firmware/ata-secure-erase.                         │
└────────────────────────────────────────────────────────────────────────────────────────────────┘
```

### 6.2 Separación Metodológica de Benchmarks
El protocolo de experimentación divide las pruebas en **dos arneses de medición disjuntos**:

#### Arnés Primario: Rendimiento Computacional Puro en Memoria (In-Memory Decompression)
- **Objetivo:** Medir el throughput algorítmico puro del códec ($\text{MB/s}$ de descompresión y $\text{Mops/s}$) sin interferencia de latencias de disco.
- **Entorno:** El bloque comprimido reside íntegramente en memoria RAM previamente asignada.
- **Protocolo Warm-Cache:** Se ejecutan $N_{\text{warmup}} = 5$ iteraciones de calentamiento antes de iniciar la captura de tiempos para estabilizar las cachés de instrucciones y datos.
- **Protocolo Cold-Cache de CPU (Empírico):**  
  Para evaluar el comportamiento ante fallo de caché de CPU:
  1. Se asigna un buffer de desalojo continuo cuyo tamaño es al menos el doble de la caché L3 del procesador bajo prueba:
     $$\text{evict\_size} = \max(256 \times 1024 \times 1024, \ 2 \times \text{L3\_size})$$
     (En Windows, el tamaño de L3 se consulta mediante `GetLogicalProcessorInformation`).
  2. Antes de cada medición, se realiza un barrido secuencial de lectura y escritura forzada mediante punteros volátiles:
     ```c
     volatile uint8_t *p = eviction_buffer;
     for (size_t i = 0; i < evict_size; i += 64) {
         p[i] ^= (uint8_t)i;
     }
     ```
  3. **Delimitación Normativa:** La documentación del benchmark registrará expresamente que este barrido constituye un procedimiento empírico de desalojo por desplazamiento forzado, reconociendo que los prefetchers de hardware y las políticas de reemplazo de línea no garantizan determinismo a nivel de ciclo de reloj.

#### Arnés Secundario: Latencia End-to-End I/O y Amplificación de Lectura
- **Objetivo:** Medir el impacto de la lectura selectiva desde almacenamiento secundario persistente.
- **Protocolo Unbuffered I/O en Windows:**
  1. Los archivos se abren estrictamente mediante la API nativa de Windows con el flag de bypass de Page Cache:
     ```c
     HANDLE hFile = CreateFileA(
         filepath,
         GENERIC_READ,
         FILE_SHARE_READ,
         NULL,
         OPEN_EXISTING,
         FILE_FLAG_NO_BUFFERING, // Bypass absoluto de la Page Cache del SO
         NULL
     );
     ```
  2. **Requisitos Estrictos de Alineación Sectorial:**
     - La especificación de Windows exige que todo buffer de lectura, desplazamiento de archivo (`FileOffset`) y longitud leída sean múltiplos enteros del tamaño del sector físico del disco (típicamente 4096 bytes).
     - El arnés consulta el tamaño del sector mediante `GetDiskFreeSpaceA`.
     - Todos los buffers de recepción se asignan obligatoriamente mediante `VirtualAlloc`:
       ```c
       uint8_t *aligned_buf = (uint8_t*)VirtualAlloc(
           NULL,
           aligned_bytes,
           MEM_COMMIT | MEM_RESERVE,
           PAGE_READWRITE // Garantiza alineación a límite de página de 4096 bytes
       );
       ```
     - Si la tabla de saltos o el offset de un canal no está alineado a 4096 bytes, el arnés redondea hacia abajo al múltiplo de 4096 más cercano, lee la cantidad de sectores requerida y extrae los datos del offset relativo interno.
  3. **Reconocimiento Expreso de Limitaciones de Hardware:**  
     Se hace constar formalmente que el flag `FILE_FLAG_NO_BUFFERING` garantiza el bypass de la Page Cache de Windows (Nivel 1), pero no puede controlar la memoria caché volátil interna del controlador SSD/NVMe (Nivel 2). Dicho factor se trata como variable física no controlable y se mitiga ejecutando un mínimo de $R = 20$ repeticiones aleatorizadas, reportando media, desviación estándar e intervalos de confianza del 95%.

### 6.3 Definición Matemática de Métricas de Amplificación
Para medir con precisión la eficiencia del acceso selectivo por canales (M7) y por motores (C-MAPSS), se definen formalmente las métricas analíticas de amplificación:

1. **Carga Útil Destino ($B_{\text{target}}$):**
   Representa el tamaño físico en bytes de los datos no comprimidos solicitados por la consulta:
   $$B_{\text{target}} = N_{\text{samples}} \times K_{\text{queried}} \times \text{sizeof}(\text{float})$$
   (Para un único canal flotante de 32 bits, $B_{\text{target}} = 4 \times N$).

2. **Amplificación de Lectura ($A_{\text{read}}$):**
   Razón entre los bytes físicamente leídos de disco (incluyendo alineación sectorial y cabeceras) y los datos destino solicitados:
   $$A_{\text{read}} = \frac{B_{\text{read}}}{B_{\text{target}}}$$

3. **Amplificación de Descompresión ($A_{\text{decomp}}$):**
   Razón entre los bytes totales descomprimidos por el pipeline computacional y los datos destino solicitados:
   $$A_{\text{decomp}} = \frac{B_{\text{decomp}}}{B_{\text{target}}}$$
   - En códecs entrelazados estándar (M1, M2, M3, M4): para recuperar un solo canal es obligatorio descomprimir los $K$ canales completos, por lo que $A_{\text{decomp}} \approx K$.
   - En el modelo M7 (`BinPack_Channel_Dedicated`): gracias a la tabla de saltos, solo se descomprime el canal solicitado, logrando idealmente $A_{\text{decomp}} \approx 1.0$.

---

## 7. Organización Modular y Plan de Aislamiento Experimental

Toda la implementación experimental se estructurará de forma estrictamente modular dentro del directorio aislado `tests/experiments/bp011/`:

| Módulo / Archivo | Responsabilidad Técnica | Dependencias Permitidas |
| :--- | :--- | :--- |
| `bp011_types.h` | Estructuras comunes, códigos de error `bp011_status_t`, cabeceras `bp011_block_header_t` y Little-Endian helpers | Estándar C99 (`stdint.h`, `stdbool.h`) |
| `timeseries_bitstream_safe.h` | Mapeadores de bits seguros con validación incondicional anti-overflow y retornos unívocos | `bp011_types.h` |
| `timeseries_timestamp_codec.h` | Códec temporal unificado R15.10 (microsegundos en `int64_t`) | `timeseries_bitstream_safe.h` |
| `gorilla_canonical.h` | Baseline M1 Gorilla canónico F64 (Pelkonen 2015) y variante `Gorilla_Float32_Variant` | `timeseries_bitstream_safe.h` |
| `chimp_canonical.h` | Baseline M2 Chimp canónico F64 (Liakos 2022) y variante `Chimp_Float32_Variant` | `timeseries_bitstream_safe.h` |
| `chimp128_canonical.h` | Baseline M3 Chimp128 canónico F64 con tabla hash $2^{14}$, ventana 128 y decisión de umbral | `timeseries_bitstream_safe.h` |
| `binpack_timeseries_adapter.h` | Adaptador composicional de BinPack con transposición escalar/SIMD | `timeseries_bitstream_safe.h` |
| `binpack_channel_dedicated.h` | Modelo M7 con tabla de saltos de canales y compresión de canales independientes | `timeseries_bitstream_safe.h` |
| `bp011_benchmark_harness.c` | Arnés de medición in-memory y end-to-end con protocolo de vaciado de caché multicapa | Windows API (`windows.h`) |

**Gobernanza:** Ningún archivo de este directorio incluirá o modificará cabeceras internas de `src/` o `include/` del SDK productivo salvo las APIs públicas documentadas.

---

## 8. Conclusión y Petición Formal

El diseño técnico presentado en esta **Revisión R3** subsana de raíz, de manera integral y formal, las seis objeciones formuladas por el Auditor Líder en el dictamen técnico de la Revisión R2:

1. Se delimitan con total rigor científico los Baselines Nativos Canónicos de literatura en Doble Precisión (Float64), las Variantes Adaptadas de Precisión Simple (Float32) y los Baselines Normalizados Composicionales (R15.10).
2. Se adopta sin ambigüedades la especificación canónica de Chimp128 según Liakos et al. (VLDB 2022), detallando su estructura auxiliar hash de $2^{14}$ entradas, la ventana deslizante de 128 muestras y el umbral de decisión de coste/beneficio.
3. Se cierra el contrato binario mediante serialización Little-Endian explícita, layout físico determinista con offsets dedicados, prevención estricta de underflow y definición inequívoca de CRC32.
4. Se formaliza el Directorio de Series para C-MAPSS, demostrando acceso $O(1)$ para índices densos y $O(\log M)$ para dispersos, con reconstrucción determinista de `start_cycle`.
5. Se garantiza la seguridad del bitstream mediante rutinas inmunes a desbordamientos aritméticos y retornos de estado inequívocos.
6. Se establece una metodología multicapa transparente para mediciones de caché y memoria, distinguiendo formalmente las capas controlables (RAM y Page Cache de SO con buffers sectoriales alineados) de las capas opacas de hardware.

Por todo lo anterior, se somete esta especificación técnica al Auditor Líder solicitando la **aprobación definitiva de la Fase C** y la consiguiente **autorización para iniciar la fase de codificación experimental** en `tests/experiments/bp011/`.
