# Especificación y Diseño Técnico — Hito BP-011 / Fase C
## Arquitectura de Instrumentación, Baselines Composicionales y Arnés de Medición

**Proyecto:** BinPack Enterprise SDK  
**Hito:** BP-011  
**Fase:** Fase C — Auditoría de Instrumentación y Diseño de Arneses  
**Documento Rector:** `docs/audit/bp011_phase_c_technical_design.md`  
**Rama:** `audit/bp1-core-safety`  
**Fecha:** 12 de noviembre de 2026  
**Estado:** ESPECIFICACIÓN TÉCNICA FORMAL DE DISEÑO DE FASE C  
**Gobernanza:** *Code Freeze* preservado al 100% (cero modificaciones en `src/`, `include/` y `deps/`; sin compilación ni ejecución en esta fase).

---

## 1. Introducción, Mandato y Principios Rectores

En cumplimiento estricto del dictamen de apertura de la **Fase C** emitido por el Auditor Líder, el presente documento formula la especificación de ingeniería completa para la instrumentación experimental del benchmark de series temporales industriales.

### 1.1 Principios Rectores Innegociables
1. **Simetría y Paridad Estricta de Carga Útil:**  
   Queda terminantemente prohibido comparar un motor que solo comprime valores flotantes frente a otro que almacena timestamps, cabeceras e índices. Todos los motores evaluados deberán comprimir, empaquetar y reconstruir **exactamente la misma información**: tuplas completas $(t_i, v_{i, 0}, \dots, v_{i, K-1})$ con sus metadatos y delimitadores de bloque.
2. **Aislamiento Experimental Absoluto:**  
   Toda la implementación de la Fase C residirá exclusivamente en el directorio experimental aislado [`tests/experiments/bp011/`](file:///C:/Users/sgi.lm/Desktop/binpack-SDK/tests/experiments/bp011/). El código productivo en `src/`, `include/` y las librerías en `deps/` permanecerán inalterados y bajo estricto congelamiento.
3. **Seguridad y Contención de Memoria:**  
   En respuesta a la observación de auditoría de Fase B sobre `timeseries_codecs.h`, todo el manejo de flujos de bits se reescribirá con envoltorios seguros que impidan desbordamientos de buffer en lectura y escritura.
4. **Cero Compilación y Cero Ejecución Prematura:**  
   Durante la Fase C solo se diseñará, especificará y codificará el arnés. La compilación, verificación estática y ejecución experimental requerirán sus respectivas aprobaciones en las Fases D y E.

---

## 2. Sección C.1 — Arquitectura de Baselines Composicionales

Se diseñan 7 motores experimentales comparables de forma simétrica:

```
┌────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                              ARQUITECTURA DE MOTORES EXPERIMENTALES                                    │
├─────────┬──────────────────────────────┬────────────────────────────┬──────────────────────────────────┤
│ ID      │ Denominación                 │ Codec Temporal             │ Codec de Valores                 │
├─────────┼──────────────────────────────┼────────────────────────────┼──────────────────────────────────┤
│ **M1**  │ `Gorilla_Composed`           │ Delta-of-Delta R15.10      │ Gorilla Float XOR (VLDB 2015)    │
│ **M2**  │ `Chimp_Composed`             │ Delta-of-Delta R15.10      │ Chimp Float32 Original (VLDB 22) │
│ **M3**  │ `Chimp128_Composed`          │ Delta-of-Delta R15.10      │ Chimp128 con Ring-Buffer 128     │
│ **M4**  │ `BinPack_Raw_Scalar`         │ Delta-of-Delta R15.10      │ BinPack L19 Byte-Stream Escalar  │
│ **M5**  │ `BinPack_Transposed_Scalar`  │ Bloque Temporal R15.10     │ Transpose Stride-K + Delta + L19 │
│ **M6**  │ `BinPack_Transposed_SIMD`    │ Bloque Temporal R15.10     │ Transpose AVX2 + Delta + L19     │
│ **M7**  │ `BinPack_Channel_Dedicated`  │ Bloque Temporal R15.10     │ Canales Columnarmente Aislados   │
└─────────┴──────────────────────────────┴────────────────────────────┴──────────────────────────────────┘
```

### 2.1 M1 — `Gorilla_Composed`
- **Componente Temporal ($B_{\text{time}}$):** Codificador temporal canónico R15.10 (Sección C.2).
- **Componente de Valores ($B_{\text{values}}$):** Codificador flotante XOR adaptativo (Pelkonen et al., 2015). Para $K$ canales entrelazados, cada canal mantiene su propio estado de predicción previa $v_{i-1, k}$ y máscara previa de ceros iniciales y finales (`prev_lz[k]`, `prev_tz[k]`).
- **Contenedor ($B_{\text{container}}$):** Cabecera canónica de 16 bytes:
  - `magic` (4B): `0x474F5231` ("GOR1").
  - `num_samples` (4B, uint32_t): Cardinalidad $N$.
  - `num_channels` (2B, uint16_t): Cardinalidad $K$.
  - `time_bytes` (4B, uint32_t): Tamaño exacto de la sección temporal en bytes.
  - `flags` (2B, uint16_t): Flags de precisión (0=F32, 1=F64).

### 2.2 M2 — `Chimp_Composed`
- **Componente Temporal ($B_{\text{time}}$):** Codificador temporal canónico R15.10.
- **Componente de Valores ($B_{\text{values}}$):** Chimp original (Liakos et al., 2022) con cuantización de ceros iniciales a múltiplos de 4 (factores 0 a 7 codificados en 3 bits) y códigos de control de 2 bits (`00`=igual, `01`=mismo rango, `10`=nuevo rango). Mantiene estados independientes por canal.
- **Contenedor ($B_{\text{container}}$):** Cabecera canónica de 16 bytes con `magic` `0x43484D31` ("CHM1").

### 2.3 M3 — `Chimp128_Composed` (Baseline Especializado Canónico)
- **Componente Temporal ($B_{\text{time}}$):** Codificador temporal canónico R15.10.
- **Componente de Valores ($B_{\text{values}}$ con Ring-Buffer):**  
  Implementación formal del algoritmo Chimp128 conforme al paper de VLDB 2022:
  - Estructura de estado por canal:
    ```c
    typedef struct {
        uint32_t ring_buf[128]; // Buffer circular de 128 valores flotantes previos
        uint8_t ring_head;       // Cursor de inserción circular (0 a 127)
        uint8_t ring_count;      // Elementos disponibles (0 a 128)
        uint8_t prev_lz;         // Ceros iniciales previos
        uint8_t prev_tz;         // Ceros finales previos
    } chimp128_channel_state_t;
    ```
  - **Algoritmo de Selección de Referencia:**  
    Para cada valor entrante $v_{i, k}$, se evalúa la operación XOR contra los candidatos disponibles en el buffer circular. Se selecciona el índice $j^* \in [0, \text{ring\_count}-1]$ que maximice el conteo de ceros iniciales (`clz32(v ^ ring_buf[j])`).
  - **Codificación:**  
    Se emite el código de caso: si $v = \text{ring\_buf}[j^*]$, emite código de coincidencia exacta y el índice de 7 bits; de lo contrario, emite el índice de 7 bits seguido de la diferencia XOR codificada con cuantización de ceros iniciales.
  - Se inserta $v$ en `ring_buf[ring_head]` y se avanza `ring_head = (ring_head + 1) & 127`.
- **Contenedor ($B_{\text{container}}$):** Cabecera canónica de 16 bytes con `magic` `0x43313238` ("C128").

### 2.4 M4, M5 y M6 — BinPack Transpose (Raw, Escalar y SIMD AVX2)
- **Componente Temporal ($B_{\text{time}}$):** Codificador temporal canónico R15.10 encapsulado como stream independiente.
- **Componente de Valores ($B_{\text{values}}$):**  
  - *M4 (Raw):* Serialización plana de flotantes alimentada directamente a BinPack L19.
  - *M5 (Transposed Escalar):* Matriz desentrelazada por planos de bytes con stride $S = 4 \times K$, delta por plano, y compresión L19.
  - *M6 (Transposed AVX2):* Idem M5 acelerado mediante vectorización SIMD AVX2 de 256 bits (`bp_transpose_32x8_bits_avx2`).
- **Contenedor ($B_{\text{container}}$):** Cabecera estándar BinPack de 24 bytes por chunk (`binpack_chunk_header_t`).

### 2.5 M7 — `BinPack_Channel_Dedicated` (Arquitectura Columnar Optimizada)
- **Diseño Orientado a Consultas Selectivas:**  
  Para resolver consultas sobre canales individuales ($Q_1$ y $Q_2$) sin incurrir en descompresión espuria de canales ajenos:
  - El bloque se particiona físicamente en $K$ sub-bloques independientes contiguos (uno por canal).
  - La cabecera incluye una **Tabla de Saltos de Canales** (*Channel Jump Table*):
    ```c
    typedef struct {
        uint32_t channel_offset;     // Offset en bytes respecto al inicio del payload
        uint32_t channel_comp_size;  // Tamaño comprimido del canal
    } channel_jump_entry_t;
    ```
  - **Acceso en Consulta:** La consulta $Q_1$ o $Q_2$ sobre el canal $k$ salta directamente a `channel_offset`, leyendo y descomprimiendo exclusivamente los bytes del canal solicitado ($A_{\text{decomp}} \approx 1.0$).

---

## 3. Sección C.2 — Diseño del Codec Temporal Canónico R15.10

### 3.1 Especificación de la Máquina de Estados y Aritmética Segura
El codificador y decodificador temporal implementan estrictamente la máquina de estados formalizada en el Protocolo R15.10:

```
[Inicio de Bloque o Nueva Serie]
               │
               ▼
   Emite Prefijo '111111' + 64 bits de t_0
       has_prev_delta = false
               │
               ▼
         [Muestra i+1]
               │
       ¿Delta desborda int64_t?
        /                     \
    (Sí)                      (No)
      │                         │
Emite '111111' + t_{i+1}   Emite '111110' + 64 bits de Delta
has_prev_delta = false     has_prev_delta = true
                                │
                                ▼
                       [Muestra i+2 en adelante]
                                │
                   Comprobación Segura en 3 Pasos
                 ¿D = Delta_i - Delta_{i-1} seguro?
                     /                      \
                 (No)                       (Sí)
                   │                          │
           Emite '111111' + t_i        Evalúa Rango de D
           has_prev_delta = false      Emite Prefijo Variable (0, 10, 110, 1110, 11110)
```

### 3.2 Tabla de Prefijos Inequívoca (Strictly Prefix-Free)
1. $D = 0 \implies$ Prefijo `0` (1 bit).
2. $-63 \le D \le 64 \implies$ Prefijo `10` (2 bits) + 7 bits de valor sesgado $D + 63$.
3. $-255 \le D \le 256 \implies$ Prefijo `110` (3 bits) + 9 bits de valor sesgado $D + 255$.
4. $-2047 \le D \le 2048 \implies$ Prefijo `1110` (4 bits) + 12 bits de valor sesgado $D + 2047$.
5. $-2^{31} \le D < 2^{31} \implies$ Prefijo `11110` (5 bits) + 32 bits en complemento a 2.
6. Delta Inicial 64 bits $\implies$ Prefijo `111110` (6 bits) + 64 bits.
7. Escape Absoluto 64 bits $\implies$ Prefijo `111111` (6 bits) + 64 bits.

### 3.3 Tratamiento de Series Discontinuas (C-MAPSS)
En datasets como DS-02 donde la telemetría agrupa múltiples motores (`unit_id` 1 a 100), cada motor es una serie temporal independiente. El codificador detectará el cambio de `unit_id`, emitiendo inmediatamente un **Escape Absoluto (`111111`)** y reseteando `has_prev_delta = false`. Queda prohibido calcular diferencias temporales entre el final de un motor y el inicio del siguiente.

---

## 4. Sección C.3 — Seguridad, Contención y Robustez del Bitstream

Para subsanar formalmente la vulnerabilidad identificada en `timeseries_codecs.h` sin tocar dicho archivo congelado, se diseñan las estructuras seguras:

```c
typedef struct {
    uint8_t *buf;
    size_t cap;       // Capacidad física máxima en bytes
    size_t bit_pos;   // Posición actual del puntero en bits
    int error;        // Flag de desbordamiento (0=OK, 1=Buffer Overflow)
} bit_writer_safe_t;

typedef struct {
    const uint8_t *buf;
    size_t byte_len;  // Tamaño físico del buffer en bytes
    size_t bit_len;   // Longitud total en bits (byte_len * 8)
    size_t bit_pos;   // Posición actual de lectura en bits
    int error;        // Flag de desbordamiento (0=OK, 1=Stream Truncado)
} bit_reader_safe_t;
```

### Reglas de Contención Estricta:
1. **Escritura Segura (`bw_safe_write_bit`):**  
   Antes de escribir, comprueba si `(bw->bit_pos >> 3) < bw->cap`. Si se supera la capacidad, establece `bw->error = 1` y no realiza la escritura, abortando la compresión de forma limpia.
2. **Lectura Segura (`br_safe_read_bit`):**  
   Antes de leer, comprueba si `br->bit_pos < br->bit_len`. Si se alcanza el final del stream, establece `br->error = 1` y retorna 0, evitando lecturas fuera de límites (*out-of-bounds read*).
3. **Validación de Entradas:**  
   Entradas con $N=0$ o punteros nulos se rechazan inmediatamente retornando 0 bytes sin invocar operaciones de memoria.

---

## 5. Sección C.4 — Ubicación y Organización Modular del Código

Todo el código fuente experimental se ubicará de forma aislada en:

```
tests/experiments/bp011/
├── bp011_types.h                  <- Definiciones comunes, tuplas, enums y códigos de error
├── timeseries_bitstream_safe.h    <- Lector y escritor de bits seguro con verificación de límites
├── timeseries_timestamp_codec.h   <- Implementación del codec temporal canónico R15.10
├── gorilla_composed.h             <- Baseline M1 (Gorilla Composed)
├── chimp_composed.h               <- Baseline M2 (Chimp Composed)
├── chimp128_composed.h            <- Baseline M3 (Chimp128 con Ring-Buffer de 128 valores)
├── binpack_timeseries_adapter.h   <- Adaptadores M4, M5, M6, M7 (Transpose y Columnar)
└── bp011_benchmark_harness.c      <- Orquestador del benchmark (PCG32, Latin Squares, Consultas)
```

---

## 6. Sección C.5 — Contrato de Medición y Procedimiento de Consultas

### 6.1 Catálogo de Métricas Obligatorias (10 Categorías)
1. **Tamaño Físico y Ratios:** $B_{\text{time}}$, $B_{\text{values}}$, $B_{\text{container}}$, $B_{\text{total}}$, y Ratio de Compresión.
2. **Densidades:** Bits por valor ($\text{bpv}$), bits por timestamp ($\text{bpt}$), bits por tupla ($\text{bptuple}$) y bits por celda ($\text{bpc}$).
3. **Velocidad de Ingesta:** Throughput en $\text{MB/s}$ y $\text{Mpts/s}$.
4. **Velocidad de Descompresión Masiva:** Throughput en $\text{MB/s}$ y $\text{Mpts/s}$.
5. **Latencia de Consultas:** Tiempo wall-clock de resolución para $Q_1$, $Q_2$ y $Q_3$.
6. **Amplificación de Lectura ($A_{\text{read}}$):** $B_{\text{read}} / B_{\text{target}}$.
7. **Amplificación de Descompresión ($A_{\text{decomp}}$):** $B_{\text{decomp}} / B_{\text{target}}$.
8. **Huella de Memoria:** $M_{\text{peak\_alloc}}$, $M_{\text{alloc\_count}}$, y memoria estática auxiliar.
9. **Integridad Bit-Exacta:** Reconstrucción bit a bit idéntica (`memcmp` estricto) de marcas de tiempo y matrices de sensores.
10. **Estabilidad Estadística:** Verificación de $\text{CV}_{\text{raw}} \le 10\%$, $\text{CV}_{\text{filt}} \le 5\%$ y $\text{Outliers} \le 2$.

### 6.2 Regímenes de Caché y Ejecución de Consultas
- **Régimen Hot-Cache:** Bloque comprimido precargado en memoria RAM; mide el coste algorítmico puro.
- **Régimen Cold-Cache:** Bloque recuperado desde disco con vaciado previo de caché de E/S.
- **Regla Estricta de Aplicabilidad:**  
  - $Q_1$ (1.000 puntos) se ejecuta sobre DS-01 y DS-02.
  - $Q_2$ (10.000 puntos) se ejecuta sobre DS-02; se marca **`NOT_APPLICABLE`** sobre DS-01 ($N=2.156$).
  - $Q_3$ (4 canales) se ejecuta sobre DS-01 ($K=48$) y DS-02 ($K=24$).

---

## 7. Dictamen de Solicitud de Validación Metodológica de Fase C

El presente diseño técnico formaliza punto por punto los requisitos de los apartados C.1 a C.5 autorizados por el Auditor Líder. Se somete a revisión para obtener la validación metodológica del diseño antes de proceder a la escritura de código en `tests/experiments/bp011/`.
