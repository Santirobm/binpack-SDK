# Especificación y Diseño Técnico R6 — Hito BP-011 / Fase C
## Protocolo Decisivo de Validación de Viabilidad Comercial en Telemetría Industrial

**Proyecto:** BinPack Enterprise SDK  
**Hito:** BP-011  
**Fase:** Fase C — Auditoría de Instrumentación y Diseño de Arneses (Revisión Definitiva R6)  
**Documento Rector:** `docs/audit/bp011_phase_c_technical_design.md`  
**Rama:** `audit/bp1-core-safety`  
**Fecha:** 12 de noviembre de 2026  
**Estado:** ESPECIFICACIÓN TÉCNICA DEFINITIVA DE DISEÑO — ÚLTIMA REVISIÓN DOCUMENTAL (REVISIÓN R6)  
**Gobernanza:** *Code Freeze* preservado al 100% (cero modificaciones en `src/`, `include/` y `deps/`; sin compilación ni ejecución en esta fase).

---

## Control de Cambios de la Revisión R6 (Pivote Estratégico y Cierre Documental)

La presente **Revisión R6** constituye la **última revisión documental de diseño de la Fase C**. Transforma y enfoca formalmente el Hito BP-011 de una exploración académica exhaustiva a una **Validación Decisiva de Viabilidad Comercial**, respondiendo a la pregunta de negocio central:  
*¿Ofrece BinPack una ventaja comercial suficientemente importante en telemetría industrial frente a las soluciones establecidas para justificar su continuidad, comercialización e inversión?*

### Principales Resoluciones de R6:
1. **Focalización en Tres Motores Clave (+ Zstd):**  
   Se acota la matriz de motores competitivos a:
   - **Gorilla (F64):** Referencia fundacional y estándar industrial de facto en series temporales (Pelkonen et al. 2015).
   - **Chimp128 (F64):** Referencia moderna de compresión temporal de alta densidad (Liakos et al. VLDB 2022).
   - **BinPack Transpose:** Nuestra propuesta central (transposición por planos de bytes + códec BinPack con acceso selectivo).
   - *(Zstd como referencia general secundaria de la industria).*
2. **Cinco Métricas Comerciales Decisivas:**  
   Ratio de compresión, velocidad de compresión (ingestión), velocidad de descompresión (recuperación), latencia y amplificación en consulta selectiva de un canal, y huella de memoria en hardware edge modesto.
3. **Corpus Mínimo Aprobado:**  
   Evaluación exclusiva sobre los dos escenarios industriales ya certificados en Fase B:
   - *NASA Turbofan C-MAPSS:* 24 canales, 20.631 registros (telemetría multicanal compleja).
   - *NASA Bearing IMS:* 48 características estadísticas, 2.156 registros (monitoreo de condición).
4. **Criterios Comerciales Objetivos de Decisión:**  
   Definición previa de umbrales inmutables para determinar si BinPack **continúa su desarrollo**, **se reposiciona a un nicho especializado** (ej. consultas selectivas multicanal) o **se congela/descarta honestamente**.
5. **Cierre Técnico Definitivo:**  
   Consolidación integral de la máquina de estados de Gorilla, Chimp128 canónico según Figura 8, cabecera de 64 bytes Little-Endian, validación de segmentos C-MAPSS y protocolo Windows Unbuffered I/O.

---

## 1. Misión, Mandato Comercial y Principios Rectores

### 1.1 Pregunta Comercial de Negocio
El objetivo de BP-011 no es competir académicamente en todos los tipos posibles de compresión, sino verificar si BinPack posee una propuesta de valor cuantificable y defendible en el mercado de IoT y telemetría industrial frente a software libre consolidado (Gorilla y Chimp128).

BinPack **no necesita ganar en todas las métricas ni en todos los datasets**:
- Si comprime igual que Gorilla pero permite **consultar un canal específico 5x o 10x más rápido** sin descomprimir la serie completa, posee un valor comercial indiscutible para historiadores industriales y edge gateways.
- Si logra un ratio de compresión significativamente superior con velocidad aceptable, es comercializable para reducción de costes de ancho de banda satelital y almacenamiento en la nube.
- Si no ofrece ventaja medible en compresión ni en consultas selectivas, se detendrá el proyecto de forma responsable y fundada.

### 1.2 Principios Rectores Innegociables
1. **Reconstrucción Exacta e Incondicional (Lossless):** Tolerancia cero a pérdida de datos; verificación bit a bit en todas las muestras flotantes.
2. **Paridad Estricta de Carga Útil:** Todos los motores comprimen exactamente las mismas tuplas $(t_i, v_{i, 0}, \dots, v_{i, K-1})$ empaquetadas en bloques autodescriptivos.
3. **Aislamiento Experimental Absoluto:** Todo el código se desarrollará exclusivamente en [`tests/experiments/bp011/`](file:///C:/Users/sgi.lm/Desktop/binpack-SDK/tests/experiments/bp011/), manteniendo `src/`, `include/` y `deps/` 100% congelados.
4. **Cierre de Ciclo Documental:** R6 es la última revisión documental. Tras su aprobación, se procede a codificar, realizar la auditoría estática de Fase D y ejecutar la campaña decisiva de Fase E.

---

## 2. Matriz de Competidores y Corpus Experimental Mínimo

### 2.1 Motores Evaluados
```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                        MATRIZ DECISIVA DE COMPETIDORES (BP-011 R6)                     │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ 1. Gorilla (Canonical F64): Estándar histórico de series temporales (Pelkonen 2015).   │
│ 2. Chimp128 (Canonical F64): Estado del arte en compresión flotante (Liakos 2022).    │
│ 3. BinPack Transpose: Propuesta central (Transposición plana + Compresión BinPack).    │
│ 4. Zstd (Baseline Secundario): Compresor general de referencia industrial.             │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

### 2.2 Corpus Mínimo Certificado (Fase B)
La evaluación se ejecutará estrictamente sobre los dos corpus físicos verificados:
1. **NASA C-MAPSS (FD001):** 24 canales continuos, 20.631 ciclos temporales. Evalúa telemetría multicanal densa y recuperación selectiva de sensores de turbofanes.
2. **NASA Bearing IMS:** 48 canales de características estadísticas, 2.156 ventanas temporales. Evalúa monitoreo continuo de vibración y diagnóstico de fallos.

---

## 3. Las Cinco Métricas Comerciales Decisivas

| Métrica | Definición Operativa | Pregunta Comercial que Responde |
| :--- | :--- | :--- |
| **1. Ratio de Compresión ($CR$)** | $\frac{\text{Bytes No Comprimidos}}{\text{Bytes Bloque Final}}$ | ¿Cuánto ancho de banda y almacenamiento en la nube ahorra al cliente? |
| **2. Velocidad de Compresión** | Megabytes no comprimidos procesados por segundo (MB/s) | ¿Puede ingerir flujos de telemetría de alta frecuencia sin saturar la CPU del Edge? |
| **3. Velocidad de Descompresión** | Megabytes recuperados por segundo (MB/s) | ¿Permite análisis en tiempo real y visualización fluida de históricos? |
| **4. Eficiencia de Consulta Selectiva** | Latencia (ms) y Amplificación ($A_{\text{decomp}}$ y $A_{\text{read}}$) al consultar 1 canal de $K$ | ¿Permite consultar un sensor individual sin el sobrecoste de descomprimir los $K-1$ restantes? |
| **5. Huella de Memoria (RAM)** | Memoria pico asignada durante compresión y descompresión (KB) | ¿Puede integrarse en PLCs, gateways IoT o microcontroladores industriales? |

---

## 4. Criterios Objetivos para Decidir la Continuidad de BinPack

La decisión comercial posterior a la campaña experimental de la Fase E se basará en las siguientes reglas inmutables:

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                         CRITERIOS DE DECISIÓN COMERCIAL BP-011                         │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ [SEGUIR DESARROLLANDO] Ventaja Comercial Demostrada:                                   │
│   • BinPack logra >= 15% de mejora de compresión frente a Gorilla y Chimp128,          │
│     O BIEN duplica (>= 2x) el rendimiento/throughput en consultas selectivas de canal, │
│     sin una penalización inaceptable en CPU (< 50% de la velocidad de Gorilla) ni RAM. │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ [REPOSICIONAR] Ventaja de Nicho Especializado:                                         │
│   • BinPack no gana en ratio global, pero demuestra una ventaja operativa clara y      │
│     defendible en un escenario concreto: consultas por canal en telemetría multicanal  │
│     alta (K >= 24), o ingestión ultra-eficiente en Edge gateways con memoria limitada. │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ [DESCARTAR / CONGELAR] Sin Ventaja Demostrada:                                         │
│   • BinPack no supera a Gorilla ni Chimp128 en compresión, y no ofrece ventaja         │
│     relevante en consultas selectivas ni velocidad. Se congela honestamente el motor.  │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 5. Especificación Algorítmica Cerrada de los Motores

### 5.1 Gorilla F64 Canónico (Pelkonen et al. 2015)
- **Máscara Segura sin UB:**
  ```c
  static inline uint64_t bp011_make_mask64(uint8_t len) {
      if (len == 0) return 0ULL;
      if (len >= 64) return ~0ULL;
      return (1ULL << len) - 1ULL;
  }
  ```
- **Flujo de Estados:**
  1. $v_0$ literal de 64 bits.
  2. Para $v_i$ ($i \ge 1$), $XOR = v_i \oplus v_{i-1}$.
     - Si $XOR == 0$: bit `0`.
     - Si $XOR \ne 0$: bit `1`.  
       $lz = \text{clz64}(XOR)$, $tz = \text{ctz64}(XOR)$, $lz_{\text{eff}} = \min(lz, 31)$, $\text{len} = 64 - lz_{\text{eff}} - tz$.
       - Si $lz_{\text{eff}} \ge \text{prev\_lz\_eff} \land tz \ge \text{prev\_tz}$: bit de control `0` + bits en ventana previa $\text{prev\_len}$.
       - En otro caso: bit de control `1` + $lz_{\text{eff}}$ (5b) + $(\text{len} - 1)$ (6b) + bits significativos de longitud $\text{len}$. Actualiza estado previo.

### 5.2 Chimp128 Canónico (Liakos et al., VLDB 2022, Figura 8)
- **Estructura:** Buffer circular de 128 elementos (`uint64_t ring_buf[128]`) y tabla hash de $2^{14} = 16,384$ entradas (`int32_t hash_table[16384]`).
- **Clave Hash y Umbral Estricto:** $\text{key} = v_i \& 0x3FFF$. Coincidencia en clave implica $tz_c \ge 14 > 13$.
- **Lógica de la Figura 8:**
  - Si el candidato $c$ es válido ($1 \le i - c \le 128$):
    - Emite bit `1` + 7 bits de índice ($c \bmod 128$). Referencia $v_{\text{ref}} = v_c$.
  - Si no es válido:
    - Emite bit `0` (sin índice). Referencia $v_{\text{ref}} = v_{i-1}$.
  - Codificación del $XOR = v_i \oplus v_{\text{ref}}$:
    - Si $XOR == 0$: emite bit `0`.
    - Si $XOR \ne 0$: emite bit `1`.
      - Si $lz_{\text{quant}} == \text{prev\_lz\_quant} \land tz == \text{prev\_tz}$: bit `0` + bits centrales.
      - En otro caso: bit `1` + $lz_{\text{quant}}$ (3b) + $(\text{len} - 1)$ (6b) + $\text{len}$ bits significativos. Actualiza estado previo.

### 5.3 BinPack Transpose (Nuestra Propuesta)
- **Filosofía Comercial:** Desentrelazar la matriz de muestras $N \times K$ en planos de bytes contiguos mediante transposición escalar/SIMD, permitiendo que la compresión aproveche la redundancia de los bits de mayor peso y posibilitando el salto selectivo directo a un canal $k$ mediante una tabla de offsets compacta.

---

## 6. Contrato Binario Canónico del Contenedor (64 Bytes)

```c
#define BP011_BLOCK_MAGIC             0x4B504E42 // "BNPK" Little-Endian
#define BP011_FORMAT_VERSION          0x0001
#define BP011_HEADER_SIZE_BYTES       64
#define BP011_FLAG_HAS_CHANNEL_TABLE  (1U << 0)
#define BP011_FLAG_HAS_SERIES_DIR     (1U << 1)
#define BP011_KNOWN_FLAGS             (BP011_FLAG_HAS_CHANNEL_TABLE | BP011_FLAG_HAS_SERIES_DIR)

typedef struct {
    uint32_t magic;                 // 0x00: 0x4B504E42
    uint16_t version;               // 0x04: 0x0001
    uint16_t flags;                 // 0x06: Flags
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

**Reglas de Validación Incondicional:**
1. Streams obligatorios: `time_stream_bytes > 0`, `time_stream_bits > 0`, `values_stream_bytes > 0`, `values_stream_bits > 0`.
2. Coherencia: $\text{bits} \le 8 \times \text{bytes}$. Padding en último byte estrictamente cero.
3. Flags: `(flags & ~BP011_KNOWN_FLAGS) == 0`.
4. No solapamiento: verificación par a par entre todas las secciones activas.

---

## 7. Directorio de Series C-MAPSS (32 Bytes / Segmento)

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
```
- Se prohíben series vacías (`sample_count == 0`).
- Validación secuencial de no solapamiento en muestras, tiempo y valores.

---

## 8. Protocolo de Medición Industrial y Windows I/O

1. **Throughput In-Memory (Primario):** Mide la velocidad computacional del algoritmo en RAM. Calentamiento previo de 5 ejecuciones. Reporte de media y desviación sobre $R = 20$ repeticiones.
2. **Consulta Selectiva End-to-End (Secundario):** Medición de latencia al recuperar 1 canal individual mediante lectura no amortiguada (`FILE_FLAG_NO_BUFFERING`).
   - Consulta de sector y buffer alignment mediante `GetVolumePathNameA(filepath)` y `STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR`.
   - Comprobación de alineación de puntero devuelto por `VirtualAlloc`.
   - Redondeo sectorial y acotación segura de la lectura al límite $F_{\text{aligned}}$ sin error `ERROR_HANDLE_EOF`.

---

## 9. Hoja de Ruta Ejecutiva tras Aprobación de R6

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                        HOJA DE RUTA EJECUTIVA PARA BP-011                              │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ Paso 1: Aprobación Formal de R6 (Última Revisión Documental de Diseño).                │
│ Paso 2: Fase C (Codificación) en tests/experiments/bp011/ de los 3 motores y arnés.  │
│ Paso 3: Fase D (Auditoría Estática de Arneses y Compilación Segura).                  │
│ Paso 4: Fase E (Campaña Experimental Única sobre C-MAPSS e IMS).                       │
│ Paso 5: Dictamen Comercial: Decisión definitiva (Continuar / Reposicionar / Congelar). │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 10. Conclusión y Petición Formal

El diseño técnico presentado en esta **Revisión R6** concentra todo el rigor metodológico desarrollado en las revisiones previas hacia un objetivo empresarial nítido: **demostrar con hechos y mediciones si BinPack posee viabilidad comercial**.

Se solicita al Auditor Líder:
1. **Aprobación definitiva e incondicional de la Especificación y Diseño Técnico R6.**
2. **Autorización formal e inmediata para iniciar la codificación experimental** exclusivamente en [`tests/experiments/bp011/`](file:///C:/Users/sgi.lm/Desktop/binpack-SDK/tests/experiments/bp011/), manteniendo el *code freeze* inalterado en `src/`, `include/` y `deps/`.
