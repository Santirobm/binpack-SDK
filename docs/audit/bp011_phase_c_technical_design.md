# Especificación y Diseño Técnico R2 — Hito BP-011 / Fase C
## Arquitectura de Instrumentación, Baselines Composicionales y Arnés de Medición

**Proyecto:** BinPack Enterprise SDK  
**Hito:** BP-011  
**Fase:** Fase C — Auditoría de Instrumentación y Diseño de Arneses (Revisión R2)  
**Documento Rector:** `docs/audit/bp011_phase_c_technical_design.md`  
**Rama:** `audit/bp1-core-safety`  
**Fecha:** 12 de noviembre de 2026  
**Estado:** ESPECIFICACIÓN TÉCNICA DEFINITIVA DE DISEÑO DE FASE C (REVISIÓN R2)  
**Gobernanza:** *Code Freeze* preservado al 100% (cero modificaciones en `src/`, `include/` y `deps/`; sin compilación ni ejecución en esta fase).

---

## Control de Cambios de la Revisión R2 (Subsanación Integral NC-C01 a NC-C05)

- **NC-C01 (Desambiguación de Baselines Nativos vs. Composicionales Normalizados):** Se establece formalmente una arquitectura en dos niveles disjuntos de comparación: Nivel 1 (Baselines Nativos Canónicos con su formato temporal original: Gorilla 32-bit de Pelkonen et al. 2015, Chimp con Gorilla timestamp según Liakos et al. 2022) y Nivel 2 (Baselines Normalizados Composicionales con timestamp industrial común R15.10 de 64 bits en microsegundos).
- **NC-C02 (Especificación Rigurosa y Trazabilidad de Chimp128):** Definición matemática exhaustiva del algoritmo Chimp128 (VLDB 2022), detallando la estructura de estado (`ring_buf[128]`, `ring_head`, `ring_count`), regla de selección óptima de ceros iniciales, criterio determinista de desempate en tres pasos, tratamiento de coincidencias exactas (XOR==0) en 9 bits, codificación de ventanas, semántica de inserción y tabla formal de correspondencia con el paper de Liakos et al.
- **NC-C03 (Contrato Binario Completo de Contenedores y Tablas de Saltos):** Especificación de cabeceras binarias empaquetadas de 32 bytes con endianness Little-Endian estricto, número de versión, delimitación exacta de bytes y bits (`time_bits`, `values_bits`), padding final validado con ceros, tabla de saltos de canales para M7 (`bp011_channel_entry_t`, 12B/canal) contabilizada incondicionalmente en $B_{\text{container}}$ y $B_{\text{total}}$, y comprobaciones aritméticas seguras contra overflow de offsets.
- **NC-C04 (Reconstrucción Integral de Series Discontinuas en C-MAPSS):** Incorporación de una Tabla de Directorio de Series (`bp011_series_segment_t`, 20B/serie) que preserva la identidad del motor (`unit_id`), el rango de muestras y el timestamp lógico de inicio (`cycle`), acoplada al reinicio estricto del decodificador (`has_prev_delta = false`) y habilitando consultas directas por motor sin escanear series ajenas.
- **NC-C05 (Seguridad de Bitstream, Manejo de Errores y Procedimiento Cold-Cache):** Diseño de `bit_writer_safe_t` y `bit_reader_safe_t` con propagación de códigos de error `bp011_status_t`, prevención de overflow en `bit_pos` de 64 bits y `byte_len * 8`, validación de buffers nulos o capacidad cero; procedimiento determinista de vaciado de caché en Windows/Linux para Cold-Cache; y formulación analítica de $B_{\text{target}}$, $B_{\text{read}}$ y $B_{\text{decomp}}$ para las amplificaciones $A_{\text{read}}$ y $A_{\text{decomp}}$.

---

## 1. Introducción, Mandato y Principios Rectores

En cumplimiento estricto del dictamen de apertura de la **Fase C** emitido por el Auditor Líder, el presente documento formula la especificación de ingeniería completa para la instrumentación experimental del benchmark de series temporales industriales.

### 1.1 Principios Rectores Innegociables
1. **Simetría y Paridad Estricta de Carga Útil:**  
   Queda terminantemente prohibido comparar un motor que solo comprime valores flotantes frente a otro que almacena timestamps, cabeceras e índices. Todos los motores evaluados deberán comprimir, empaquetar y reconstruir **exactamente la misma información**: tuplas completas $(t_i, v_{i, 0}, \dots, v_{i, K-1})$ con sus metadatos y delimitadores de bloque.
2. **Aislamiento Experimental Absoluto:**  
   Toda la implementación de la Fase C residirá exclusivamente en el directorio experimental aislado [`tests/experiments/bp011/`](file:///C:/Users/sgi.lm/Desktop/binpack-SDK/tests/experiments/bp011/). El código productivo en `src/`, `include/` y las librerías en `deps/` permanecerán inalterados y bajo estricto congelamiento.
3. **Seguridad y Contención de Memoria:**  
   Todo el manejo de flujos de bits se implementará con envoltorios seguros que impidan desbordamientos de buffer en lectura y escritura.
4. **Cero Compilación y Cero Ejecución Prematura:**  
   Durante la Fase C solo se diseñará, especificará y codificará el arnés. La compilación, verificación estática y ejecución experimental requerirán sus respectivas aprobaciones en las Fases D y E.

---

## 2. Sección C.1 — Arquitectura de Baselines y Desambiguación (NC-C01)

Para erradicar la sustitución silenciosa de formatos y preservar la fidelidad histórica sin renunciar a una comparación industrial estandarizada, se formulan dos niveles de evaluación complementarios:

### 2.1 Nivel 1: Baselines Nativos Canónicos (Referencia Histórica de Literatura)
Miden el rendimiento de las tecnologías tal como fueron concebidas en sus publicaciones originales:

1. **`M1_Gorilla_Native` (Facebook Gorilla Original - Pelkonen et al., VLDB 2015):**
   - **Timestamp Nativo:** Delta-of-Delta en segundos Unix (enteros de 32 bits).
     - $t_0$: 32 bits literales.
     - $\Delta t_1 = t_1 - t_0$: 14 bits literales ($[-8191, 8192]\text{ s}$).
     - Delta-of-Delta $D = \Delta t_i - \Delta t_{i-1}$:
       - $D = 0 \implies$ bit `0`.
       - $-63 \le D \le 64 \implies$ bits `10` + 7 bits sesgados ($D + 63$).
       - $-255 \le D \le 256 \implies$ bits `110` + 9 bits sesgados ($D + 255$).
       - $-2047 \le D \le 2048 \implies$ bits `1110` + 12 bits sesgados ($D + 2047$).
       - Resto ($|D| > 2048$) $\implies$ bits `1111` + 32 bits literales con signo.
   - **Valores Nativos:** Gorilla Float32 XOR adaptativo con ventana deslizante de ceros iniciales y finales.
   - **Contenedor:** Cabecera nativa con delimitador de fin de bloque de 32 bits (bits de escape con timestamp cero y EOF).
2. **`M2_Chimp_Native` (Chimp Original - Liakos et al., VLDB 2022):**
   - **Timestamp:** Implementa el esquema adoptado en la batería experimental de Chimp (VLDB 2022, Sec. 5), consistente en el codificador temporal nativo de Gorilla acoplado a Chimp.
   - **Valores Nativos:** Chimp Float32 con 1 valor previo y cuantización de ceros iniciales a pasos de 4.
3. **`M3_Chimp128_Native` (Chimp128 Canónico - Liakos et al., VLDB 2022):**
   - **Timestamp:** Formato temporal de Gorilla.
   - **Valores Nativos:** Chimp128 con buffer circular de 128 valores previos flotantes y emisión de 7 bits de índice.

### 2.2 Nivel 2: Baselines Composicionales Normalizados (Protocolo Industrial R15.10)
Permiten aislar con total pureza metodológica la eficiencia algorítmica de compresión de matrices de valores, sometiendo a todos los motores al mismo formato temporal robusto de microsegundos (`int64_t`):

```
┌────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                        NIVEL 2: BASELINES COMPOSICIONALES NORMALIZADOS                                 │
├─────────┬──────────────────────────────┬────────────────────────────┬──────────────────────────────────┤
│ ID      │ Denominación                 │ Codec Temporal Común       │ Codec de Valores                 │
├─────────┼──────────────────────────────┼────────────────────────────┼──────────────────────────────────┤
│ **M1**  │ `Gorilla_Normalized`         │ Canónico Seguro R15.10 64b │ Gorilla Float XOR (VLDB 2015)    │
│ **M2**  │ `Chimp_Normalized`           │ Canónico Seguro R15.10 64b │ Chimp Float32 Original (VLDB 22) │
│ **M3**  │ `Chimp128_Normalized`        │ Canónico Seguro R15.10 64b │ Chimp128 Ring-Buffer 128 (VLDB22)│
│ **M4**  │ `BinPack_Raw_Scalar`         │ Canónico Seguro R15.10 64b │ BinPack L19 Byte-Stream Escalar  │
│ **M5**  │ `BinPack_Transposed_Scalar`  │ Bloque Temporal R15.10 64b │ Transpose Stride-4K + Delta + L19│
│ **M6**  │ `BinPack_Transposed_SIMD`    │ Bloque Temporal R15.10 64b │ Transpose AVX2 + Delta + L19     │
│ **M7**  │ `BinPack_Channel_Dedicated`  │ Bloque Temporal R15.10 64b │ Columnar Aislado con Jump Table  │
└─────────┴──────────────────────────────┴────────────────────────────┴──────────────────────────────────┘
```

---

## 3. Sección C.2 — Especificación Rigurosa y Trazabilidad de Chimp128 (NC-C02)

Para garantizar total fidelidad con la publicación de referencia (Liakos et al., VLDB 2022, Sec. 3.2), se formaliza la implementación exacta en C de **Chimp128**:

### 3.1 Estructura de Estado por Canal
```c
typedef struct {
    uint32_t ring[128];      // Buffer circular de 128 valores flotantes de 32 bits
    uint8_t  head;           // Cursor de inserción circular (0 a 127)
    uint8_t  count;          // Cantidad de elementos válidos disponibles (0 a 128)
    uint8_t  prev_lz_quant;  // Factor cuantizado previo de ceros iniciales (0..7)
    uint8_t  prev_tz;        // Ceros finales previos (0..31)
} chimp128_state_t;
```

### 3.2 Algoritmo de Búsqueda y Regla Canónica de Desempate
Dado un nuevo valor flotante $v_i$ para el canal $k$:
1. **Inicialización ($i=0$):**  
   Se emiten 32 bits literales. Se almacena $v_0$ en `ring[0]`, fijando `head = 1`, `count = 1`, `prev_lz_quant = 0`, `prev_tz = 0`.
2. **Escaneo de Candidatos ($i > 0$):**  
   Para cada índice $j \in [0, \text{count}-1]$:
   - Se calcula el XOR: $x_j = v_i \oplus \text{ring}[j]$.
   - Ceros iniciales: $\text{lz}_j = \text{clz32}(x_j)$.
   - Factor cuantizado: $\text{lz\_quant}_j = \min(7, \; \lfloor \text{lz}_j / 4 \rfloor)$.
   - Ceros finales: $\text{tz}_j = (x_j == 0) ? 32 : \text{ctz32}(x_j)$.
3. **Regla Determinista de Desempate en Tres Pasos:**  
   Se selecciona el índice óptimo $j^*$ aplicando jerárquicamente:
   - **Criterio 1 (Maximizar Ceros Cuantizados):** Seleccionar $j$ con mayor $\text{lz\_quant}_j$.
   - **Criterio 2 (Maximizar Ceros Finales):** Ante igual $\text{lz\_quant}$, seleccionar $j$ con mayor $\text{tz}_j$ (minimiza el payload de bits significativos $32 - \text{actual\_lz} - \text{tz}$).
   - **Criterio 3 (Recencia Temporal):** Ante igual $\text{lz\_quant}$ y $\text{tz}$, seleccionar el índice $j$ más recientemente insertado en el ring buffer (menor distancia circular: $(\text{head} - 1 - j) \bmod 128$).

### 3.3 Codificación de Casos y Ventanas de Bits
Sea $x^* = v_i \oplus \text{ring}[j^*]$ la diferencia óptima seleccionada:
- **Caso 00 (Coincidencia Exacta, $x^* = 0$):**  
  Emite 2 bits `00` + 7 bits de índice $j^*$ (Total: 9 bits).
- **Caso 01 (Misma Ventana de Ceros):**  
  Si $\text{lz\_quant}_{j^*} == \text{prev\_lz\_quant} \land \text{tz}_{j^*} \ge \text{prev\_tz}$:  
  Emite 2 bits `01` + 7 bits de índice $j^*$ + bits significativos de longitud $(32 - (\text{prev\_lz\_quant} \times 4) - \text{prev\_tz})$ desplazados $\text{prev\_tz}$ posiciones.
- **Caso 10 (Nueva Ventana de Ceros):**  
  De lo contrario:  
  Emite 2 bits `10` + 7 bits de índice $j^*$ + 3 bits de $\text{lz\_quant}_{j^*}$ + 6 bits de longitud significativa $L = 32 - (\text{lz\_quant}_{j^*} \times 4) - \text{tz}_{j^*}$ + $L$ bits de payload desplazados $\text{tz}_{j^*}$ posiciones.  
  Actualiza: `prev_lz_quant = lz_quant_{j^*}`, `prev_tz = tz_{j^*}`.

### 3.4 Actualización de Buffer Circular
Se almacena $v_i$ en `ring[head]`. Se avanza `head = (head + 1) & 127`. Si `count < 128`, `count++`.

### 3.5 Tabla de Trazabilidad Algorítmica con Liakos et al. (VLDB 2022)

| Elemento Chimp128 en C | Sección / Algoritmo VLDB 2022 | Semántica y Justificación Técnica |
| :--- | :--- | :--- |
| `ring[128]` | Sección 3.2, Párrafo 1 | Tamaño de ventana de 128 valores previos para balancear ratio y memoria (7 bits de índice). |
| `head & 127` | Sección 3.2, Párrafo 2 | Inserción FIFO circular mediante máscara bitwise eficiente. |
| `lz_quant = lz / 4` | Sección 3.1, Algoritmo 1 | Cuantización en bloques de 4 bits para reducir el coste de emisión de leading zeros de 5 a 3 bits. |
| Caso `00` (9 bits) | Sección 3.2, Ecuación 4 | Identificación de repetición de valor histórico; supera al Gorilla original (que solo detecta repetición de $v_{i-1}$). |
| Criterio de recencia | Sección 3.2, Párrafo 4 | Desempate determinista prefiriendo la referencia más próxima temporalmente. |

---

## 4. Sección C.3 — Contrato Binario de Contenedor y Directorio de Series (NC-C03 y NC-C04)

### 4.1 Cabecera Binaria Canónica de Bloque (32 Bytes, Little-Endian)
Todo bloque comprimido por cualquiera de los motores M1 a M7 comienza con la cabecera canónica de 32 bytes:

```c
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;           // M1: 0x474F5231, M2: 0x43484D31, M3: 0x43313238, M7: 0x42504344
    uint16_t version;         // Versión de formato = 0x0001
    uint16_t num_channels;    // Cantidad de canales K (1..65535)
    uint32_t num_samples;     // Cantidad de tuplas temporales N
    uint32_t time_bytes;      // Tamaño exacto en bytes de la sección temporal (alineado a byte)
    uint32_t time_bits;       // Conteo exacto de bits válidos en la sección temporal
    uint32_t values_bytes;    // Tamaño exacto en bytes de la sección de valores
    uint32_t values_bits;     // Conteo exacto de bits válidos en la sección de valores
    uint32_t extra_metadata;  // Bytes adicionales ocupados por tablas de saltos o directorios
    uint32_t payload_crc32;   // CRC32 del payload completo (tiempo + valores + extra)
} bp011_block_header_t;       // Total: 32 bytes exactos
#pragma pack(pop)
```

### 4.2 Reglas de Padding, Offset y Validación Aritmética
1. **Padding Final:** Si `time_bits` o `values_bits` no son múltiplos de 8, el escritor seguro rellena los bits sobrantes del último byte con ceros (`0`). El decodificador valida formalmente que los bits de padding leídos sean idénticos a cero; de lo contrario, rechaza el stream con `BP011_ERR_STREAM_CORRUPT`.
2. **Prevención de Overflow en Offsets:**  
   Antes de acceder al buffer, el decodificador valida con aritmética segura:
   ```c
   if (header.time_bytes > total_size - sizeof(bp011_block_header_t)) return BP011_ERR_BUFFER_OVERFLOW;
   if (header.values_bytes > total_size - sizeof(bp011_block_header_t) - header.time_bytes) return BP011_ERR_BUFFER_OVERFLOW;
   if (header.extra_metadata > total_size - sizeof(bp011_block_header_t) - header.time_bytes - header.values_bytes) return BP011_ERR_BUFFER_OVERFLOW;
   ```

### 4.3 Tabla de Saltos de Canales para M7 (`BinPack_Channel_Dedicated`)
Para el motor M7, `extra_metadata` contiene la **Channel Jump Table** ($K \times 12\text{ bytes}$):
```c
#pragma pack(push, 1)
typedef struct {
    uint32_t channel_offset;     // Offset relativo en bytes desde el inicio de la sección de valores
    uint32_t channel_comp_bytes; // Tamaño físico en bytes del canal comprimido
    uint32_t channel_bit_len;    // Conteo exacto de bits válidos del canal
} bp011_channel_entry_t;         // 12 bytes por canal
#pragma pack(pop)
```
- **Contabilización Estricta:** Para M7, $B_{\text{container}} = 32 + (K \times 12)\text{ bytes}$. Dicha estructura se computa obligatoriamente en $B_{\text{total}}$ y en las métricas de bits por tupla ($\text{bptuple}$).

### 4.4 Directorio de Series para Telemetría Discontinua (C-MAPSS) (NC-C04)
En datasets como DS-02 (C-MAPSS), donde se concatenan las corridas de múltiples motores (`unit_id` 1 a 100):
1. **Estructura del Directorio de Series:**  
   Cuando la telemetría incluye múltiples motores, `extra_metadata` incluye un directorio de series:
   ```c
   #pragma pack(push, 1)
   typedef struct {
       uint32_t unit_id;          // Identificador de motor (ej. 1..100)
       uint32_t start_index;      // Índice de tupla de inicio (0..N-1)
       uint32_t sample_count;     // Cantidad de ciclos operativos del motor
       int64_t  start_cycle;      // Ciclo operativo inicial (típicamente 1)
   } bp011_series_entry_t;        // 20 bytes por serie
   #pragma pack(pop)
   ```
2. **Reinicio de Estado Temporal Simétrico:**  
   Al codificar la primera muestra de cada `unit_id`, el codificador emite forzosamente el **Escape Absoluto (`111111`)**, codifica los 64 bits del ciclo inicial, y reinicia `has_prev_delta = false`. Queda formalmente prohibido calcular deltas entre el último ciclo del motor $U_k$ y el primer ciclo del motor $U_{k+1}$.
3. **Consultas Directas por Motor:**  
   El directorio permite resolver consultas sobre un motor individual $U_k$ sin descomprimir ni escanear los ciclos de los motores anteriores, garantizando la preservación de la identidad de serie y la reproducibilidad de los registros.

---

## 5. Sección C.4 — Bitstream Seguro y Manejo Robusto de Errores (NC-C05)

Se sustituyen los lectores y escritores desprotegidos por estructuras seguras con control incondicional de desbordamiento:

```c
typedef enum {
    BP011_OK                     = 0,
    BP011_ERR_INVALID_ARGUMENT   = -1,
    BP011_ERR_BUFFER_OVERFLOW    = -2,
    BP011_ERR_STREAM_TRUNCATED   = -3,
    BP011_ERR_STREAM_CORRUPT     = -4,
    BP011_ERR_CRC_MISMATCH       = -5
} bp011_status_t;

typedef struct {
    uint8_t *buf;
    size_t   cap;       // Capacidad máxima en bytes
    uint64_t bit_pos;   // Posición actual en bits (64 bits previene overflow de contador)
    int      error;     // Código bp011_status_t
} bit_writer_safe_t;

typedef struct {
    const uint8_t *buf;
    size_t         byte_len;  // Tamaño físico en bytes
    uint64_t       bit_len;   // Longitud total en bits (byte_len * 8 seguro)
    uint64_t       bit_pos;   // Cursor actual de lectura en bits
    int            error;     // Código bp011_status_t
} bit_reader_safe_t;
```

### 5.1 Reglas de Validación Incondicional
1. **Inicialización:**  
   `bw_safe_init(bw, buf, cap)` valida `buf != NULL && cap > 0`. Comprueba que `cap <= (UINT64_MAX >> 3)` para impedir desbordamiento en la multiplicación de bits.
2. **Escritura Segura de Bits:**  
   `bw_safe_write_bit(bw, bit)` comprueba `(bw->bit_pos >> 3) < bw->cap`. Si se excede, marca `bw->error = BP011_ERR_BUFFER_OVERFLOW` y aborta sin escribir.
3. **Escritura Multi-Bit:**  
   `bw_safe_write_bits(bw, val, n_bits)` comprueba `bw->bit_pos + n_bits <= (uint64_t)bw->cap * 8`. Si falla, marca error y retorna sin escrituras parciales.
4. **Lectura Segura:**  
   `br_safe_read_bit(br)` comprueba `br->bit_pos < br->bit_len`. Si se alcanza el final, marca `br->error = BP011_ERR_STREAM_TRUNCATED` y retorna 0.
5. **Diferenciación de Errores:**  
   Las funciones retornan `bp011_status_t`. Se erradica el retorno ambiguo de `0 bytes`, discriminando formalmente entre éxito con datos nulos, buffer insuficiente y stream corrupto.

---

## 6. Sección C.5 — Contrato de Medición y Metodología Cold-Cache (NC-C05)

### 6.1 Procedimiento Reproducible de Vaciado de Caché (Cold-Cache)
Para garantizar que el régimen Cold-Cache refleje transferencias reales de almacenamiento y no lecturas desde el buffer de memoria del sistema operativo:
- **En Windows:**  
  1. El archivo del bloque comprimido se abre mediante `CreateFileA` utilizando las banderas combinadas:
     ```c
     FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH
     ```
     omitiendo completamente la caché de archivos de Windows (*Windows File System Cache Bypass*).
  2. Antes de la lectura medida, se asigna y recorre secuencialmente un buffer de purga de memoria de $128\text{ MB}$ (tamaño superior a cuatro veces la caché L3 del procesador) mediante escrituras forzadas volátiles para desalojar líneas residuales en la jerarquía de cachés de la CPU.
- **En Linux (si aplica):** Ejecución previa mediante llamada sincronizada de `posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED)`.

### 6.2 Definición Formal de Métricas de Amplificación
Para erradicar discrepancias en el cálculo de sobrecoste físico:
1. **Carga Útil Solicitada ($B_{\text{target}}$):**
   $$B_{\text{target}} = N_{\text{query}} \times \left( \text{sizeof}(t_i) + \sum_{k \in \text{canales\_solicitados}} \text{sizeof}(v_k) \right)$$
   *(Para $Q_1$, $1.000 \text{ pts} \times (8 + 4) = 12.000\text{ bytes}$; no incluye metadatos ni cabeceras).*
2. **Bytes Físicos Leídos ($B_{\text{read}}$):**  
   Cantidad total de bytes transferidos desde almacenamiento o RAM para completar la consulta. En motores entrelazados (M1..M6), requiere leer el bloque completo; en M7, lee únicamente la cabecera, la entrada de salto y el chunk del canal solicitado.
3. **Bytes Descomprimidos en Memoria ($B_{\text{decomp}}$):**  
   Cantidad de bytes sin comprimir producidos en buffers intermedios durante la descompresión.
4. **Fórmulas de Amplificación:**
   $$A_{\text{read}} = \frac{B_{\text{read}}}{B_{\text{target}}}, \qquad A_{\text{decomp}} = \frac{B_{\text{decomp}}}{B_{\text{target}}}$$
   *(La hipótesis de que $A_{\text{decomp}} \approx 1.0$ en M7 será contrastada empíricamente en el benchmark, no asumida a priori).*

---

## 7. Sección C.6 — Aislamiento y Organización Modular

Todo el código fuente experimental de la Fase C se estructurará de forma autocontenida bajo:

```
tests/experiments/bp011/
├── bp011_types.h                  <- Definiciones comunes, status_t, contenedores y cabeceras
├── timeseries_bitstream_safe.h    <- Bitstream seguro con control de límites y error handling
├── timeseries_timestamp_codec.h   <- Codec temporal R15.10 (prefijos prefix-free, escape y reset)
├── gorilla_native.h               <- Baseline M1 Nativo (VLDB 2015, 32-bit delta-of-delta)
├── gorilla_composed.h             <- Baseline M1 Normalizado (R15.10 + Gorilla Float XOR)
├── chimp_native.h                 <- Baseline M2 Nativo (VLDB 2022)
├── chimp_composed.h               <- Baseline M2 Normalizado (R15.10 + Chimp Float32)
├── chimp128_native.h              <- Baseline M3 Nativo (VLDB 2022, Ring-Buffer 128)
├── chimp128_composed.h            <- Baseline M3 Normalizado (R15.10 + Chimp128)
├── binpack_timeseries_adapter.h   <- Motores M4, M5, M6 (Raw, Transpose Escalar, AVX2)
├── binpack_channel_dedicated.h    <- Motor M7 (Columnar Aislado con Channel Jump Table)
└── bp011_benchmark_harness.c      <- Orquestador del benchmark (PCG32, Latin Squares, Cold/Hot)
```

---

## 8. Conclusión y Petición

El presente documento de especificación técnica R2 resuelve exhaustivamente las no conformidades NC-C01 a NC-C05. Se somete al Auditor Líder para su aprobación definitiva, solicitando la consecuente **autorización para la codificación de los módulos en `tests/experiments/bp011/`**, manteniendo vigente el estricto *code freeze* sobre `src/`, `include/` y `deps/`.
