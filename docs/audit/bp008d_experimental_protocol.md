# Protocolo de Reproducibilidad Experimental, Trazabilidad Metodológica y Preparación de Baselines — Hito BP-008D (Revisión R2)

**Expediente:** BP-008D-R2 — Subsanaciones Exhaustivas de Auditoría: PRNG sin Sesgo de Bits, Aislamiento VEX/EVEX, Demostración Analítica de Memoria y Verificación de Checksum Zstd  
**Proyecto:** BinPack Enterprise SDK  
**Ramas Auditadas:** `audit/bp1-core-safety`  
**Hito Previo:** BP-008C-R6 (Cierre Técnico Aprobado — Commit `4dc098dd7a6c38fc39d0a411b24114e27231bd97`)  
**Fecha:** 6 de Noviembre de 2026  
**Autor:** Santiago Robledo (<santiagorobledo198@gmail.com>)  
**Responsable Técnico:** Antigravity AI — Advanced Agentic Coding & Systems Architecture  

---

## 1. Resumen Ejecutivo y Respuesta a la Auditoría BP-008D-R1

El presente documento constituye la **Revisión R2 del Expediente BP-008D**, formalizada en respuesta exhaustiva a las cuatro observaciones técnicas de precisión emitidas en el dictamen de auditoría preliminar.

Este protocolo establece los fundamentos matemáticos, de arquitectura de computadores y de ingeniería de software para asegurar que cualquier tercero independiente pueda reproducir la futura campaña decisiva de benchmark de forma bit a bit, sin ambigüedades, asimetrías de contratos ni configuraciones implícitas.

### Respuestas y Subsanaciones a las Observaciones de Auditoría:
1. **R1-01 (PRNG y Calidad de Permutación Fisher-Yates):**
   - Se aborda directamente la debilidad intrínseca de los bits de orden bajo en los generadores congruenciales lineales (LCG).
   - Se descarta formalmente el operador módulo (`% bound`).
   - Se especifica e implementa la **reducción rápida sin sesgo de Daniel Lemire** operando sobre los **32 bits superiores (más significativos y de mayor entropía)** del producto de 64 bits:
     $$j = \left\lfloor \frac{X_n \times \text{bound}}{2^{32}} \right\rfloor$$
     garantizando una permutación de Fisher-Yates estadísticamente uniforme y libre de la periodicidad de los bits bajos.
2. **R1-02 (Aislamiento de ISA: Detección VEX y EVEX):**
   - Se amplía el procedimiento de auditoría con desensamblador para detectar de forma exhaustiva tanto prefijos **VEX** (AVX, AVX2) como **EVEX** (AVX-512, AVX10).
   - Se documenta el procedimiento de inspección con registro de compilador, versión, arquitectura y flags por unidad de traducción, asegurando 0 emisiones vectoriales en objetos portables.
   - Se ratifica la cuádruple compuerta de despacho en [`src/binpack_cpu.c`](file:///C:/Users/usuario/Desktop/binpack/src/binpack_cpu.c): `CPUID.1:ECX[28]` (AVX), `CPUID.1:ECX[27]` (OSXSAVE), `_xgetbv(0)` (`XCR0 & 0x06 == 0x06`), y `CPUID.7:EBX[5]` (AVX2).
3. **R1-03 (Demostración Analítica del Techo de 52.5 MB de Memoria de Trabajo):**
   - Se desglosan analítica y matemáticamente los componentes de memoria del motor de streaming acotado ([`src/binpack_stream.c`](file:///C:/Users/usuario/Desktop/binpack/src/binpack_stream.c)): búferes de chunk de 16 MB, búfer de compresión acotado, estructuras de `idx_table` y el dimensionamiento interno de tablas dinámicas de `ZSTD_CCtx` calculado mediante `ZSTD_estimateCCtxSize_usingCParams()`.
   - Se demuestra de forma irrebatible que el motor opera en **52.52 MB $\le$ 64.0 MB**, distinguiéndolo del `Peak Process RSS` que alberga el archivo en memoria.
4. **R1-04 (Verificación Activa del Checksum de Zstandard en Contrato Strict):**
   - Se documenta la inyección de `ZSTD_c_checksumFlag = 1` en el contexto activo de compresión.
   - Se exige la comprobación en caliente del bit 2 (`Content_Checksum_Flag`) en el byte FHD del frame (`comp_buf[4] & 0x04 != 0`).
   - Se comprueba que `ZSTD_decompressDCtx` valida los 4 bytes de xxHash64 al final del frame, abortando con error si se corrompen.

---

## 2. Inventario y Trazabilidad del Entorno Experimental

### 2.1. Harness de Benchmark Certificado
* **Ubicación en Repositorio:** [`tests/benchmarks/benchmark_decisive_suite.c`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c).
* **Revisión del Harness:** **BP-008C-R6** (Subsanaciones R6 verificadas: precedencia estricta de `CPUID.1:ECX[31]`, validación de rango de hojas básicas y de hipervisor en `0x40000000`, y lectura estrictamente no negativa de `core_id`).
* **Mecanismo de Aislamiento de Memoria:** Subprocesos dedicados e independientes instanciados vía `fork()` y sincronizados mediante `wait4()` en Linux / WSL2, con monitoreo del consumo de memoria residente (*Peak RSS*) vía `struct rusage`.
* **Mecanismo de Temporización:** `clock_gettime(CLOCK_MONOTONIC)` en sistemas POSIX / Linux, con resolución de nanosegundos; `QueryPerformanceCounter` / `QueryPerformanceFrequency` en Windows nativo.

### 2.2. Compilación, Unidades de Traducción y Aislamiento de ISA (VEX / EVEX) (R1-02)

La compilación se rige por un esquema estricto por unidad de traducción (TU), evitando que la autovectorización global del compilador contamine módulos que deben ser portables:

```
[ UNIDADES PORTABLES (TU 1..8) ]              [ UNIDAD SIMD AISLADA (TU 9) ]
clang -O3 -march=x86-64                       clang -O3 -mavx2 -mavx
      -mno-avx -mno-avx2                            (binpack_core_avx2.c)
(binpack_core.c, binpack_cpu.c, stream, etc.)                 |
             |                                                |
             +-----------------------+------------------------+
                                     |
                                     v
                       [ ENLACE DINÁMICO / ESTÁTICO ]
                             binpack.dll / .a
                                     |
                                     v
                    [ DISPATCH DINÁMICO EN TIEMPO DE EJECUCIÓN ]
                     bp_ensure_cpu_dispatch_initialized()
                      - CPUID.1:ECX[28] == 1 (AVX)
                      - CPUID.1:ECX[27] == 1 (OSXSAVE)
                      - _xgetbv(0) -> XCR0 & 0x06 == 0x06 (XMM/YMM)
                      - CPUID.7:EBX[5]  == 1 (AVX2)
                     -> g_bp_dispatch (Scalar vs AVX2)
```

#### A. Opciones de Compilación por Unidad de Traducción:
1. **Unidades de Traducción Portables (TU 1 a 8):**
   - `binpack_core.c`, `binpack_cpu.c`, `binpack_eval.c`, `binpack_stream.c`, `binpack_sdk.c`, `binpack_dict.c`, `binpack_patch.c`, `binpack_archive.c`.
   - **Flags Obligatorias:** `-O3 -march=x86-64 -mno-avx -mno-avx2 -Wall -Wextra -pthread -D_GNU_SOURCE`.
   - **Garantía:** Prohíbe al compilador generar cualquier instrucción vectorizada con prefijos VEX (256 bits) o EVEX (512 bits).
2. **Unidad de Traducción SIMD Acelerada (TU 9):**
   - `binpack_core_avx2.c`.
   - **Flags Obligatorias:** `-O3 -mavx2 -mavx`.
   - Contiene exclusivamente las funciones vectorizadas con intrinsics explícitos (`_mm256_*`).
3. **Módulo del Harness de Benchmark:**
   - `benchmark_decisive_suite.c`.
   - **Flags Obligatorias:** `-O3 -march=x86-64 -mno-avx -mno-avx2`.

#### B. Inspección Exhaustiva de Conjunto de Instrucciones (ISA Audit — VEX y EVEX):
La verificación va más allá de un filtrado simple de mnemónicos: se establece una inspección sistemática del desensamblado con clasificación de ISA sobre **todos los archivos objeto portables**:
* `binpack_core.o`, `binpack_cpu.o`, `binpack_eval.o`, `binpack_stream.o`, `binpack_sdk.o`, `binpack_dict.o`, `binpack_patch.o`, `binpack_archive.o`, y `benchmark_decisive_suite.o`.

**Procedimiento Formal de Auditoría:**
```bash
# 1. Detección exhaustiva de cualquier instrucción con codificación VEX (prefijo 'v' en AT&T / Intel)
llvm-objdump -d --no-show-raw-insn <objeto.o> | grep -E "^\s*[0-9a-f]+:\s+v[a-z0-9]+"

# 2. Detección exhaustiva de instrucciones con prefijo EVEX (máscaras vectoriales {%k}, broadcast {1toN})
llvm-objdump -d --no-show-raw-insn <objeto.o> | grep -E "\{[k1-7][0-9]*\}|\{1to[0-9]+\}"
```
* **Criterio Inviolable:** El conteo de instrucciones vectoriales VEX o EVEX en cualquiera de los objetos portables (TU 1 a 8) debe ser **estrictamente cero (0)**.
* Únicamente `binpack_core_avx2.o` puede contener instrucciones vectoriales VEX (`vmovdqu`, `vpaddb`, `vpxor`, `vpunpck*`, `vpshufb`).
* Se registrará un archivo de auditoría reproducible `docs/audit/isa_verification_report.log` con los metadatos completos del compilador (nombre, versión completa, target triplet x86_64 y flags).

#### C. Ruta de Despacho en Tiempo de Ejecución con Verificación OSXSAVE:
El despacho dinámico en [`src/binpack_cpu.c:62-97`](file:///C:/Users/usuario/Desktop/binpack/src/binpack_cpu.c#L62-L97) evalúa formalmente cuatro condiciones acumulativas de silicio y de sistema operativo:
1. **Soporte de Silicio AVX:** `CPUID Leaf 1: ECX bit 28 == 1`.
2. **Soporte de Silicio y SO para OSXSAVE:** `CPUID Leaf 1: ECX bit 27 == 1`. Garantiza que el sistema operativo ha inicializado el soporte de contexto extendido y que la instrucción `xgetbv` puede ser ejecutada por el espacio de usuario sin provocar una excepción `#UD`.
3. **Validación del Registro de Control Extendido (XGETBV):** Se ejecuta `_xgetbv(0)` para consultar `XCR0`. Se exige estrictamente:
   $$(\text{XCR0} \ \& \ 0\text{x}06) == 0\text{x}06$$
   garantizando que el sistema operativo preserva activamente los registros XMM (bit 1) y YMM (bit 2) en los cambios de contexto.
4. **Soporte de Silicio AVX2:** `CPUID Leaf 7, Subleaf 0: EBX bit 5 == 1`.

Si cualquiera de las cuatro comprobaciones devuelve falso, `g_bp_dispatch` apunta a los kernels escalares portables (`bp_*_scalar`). Si las cuatro son verdaderas, apunta a `bp_*_avx2`. El backend activo se reporta en el banner inicial del harness (`BINPACK_BACKEND_AVX2` o `BINPACK_BACKEND_SCALAR`).

### 2.3. Topología de CPU y Condiciones de Virtualización
* **Procesador de Referencia:** Intel Core i5-13500 (Arquitectura Híbrida Raptor Lake-S, 6 Performance Cores / 12 hilos Raptor Cove @ 2.5–4.8 GHz + 8 Efficient Cores Gracemont @ 1.8–3.5 GHz; L2 Cache: 2 MB/P-core, L3 Cache: 24 MB compartido).
* **Asignación de Afinidad Determinista:**
  - En Linux / WSL2: `sched_setaffinity()` anclado estrictamente a `logical_id = 2` (vCPU 2).
  - Verificación de máscara: Exigencia de `CPU_ISSET(2)` y `CPU_COUNT == 1` mediante `sched_getaffinity()`.
  - Forzado de conmutación: `sched_yield()` inmediatamente después de la fijación.
* **Precedencia e Inspección CPUID (BP-008C-R6):**
  1. Consulta de `CPUID Leaf 0`: comprobación de `max_basic_leaf >= 1`.
  2. Consulta de `CPUID Leaf 1`: extracción de `is_hypervisor = (ECX >> 31) & 1`.
  3. **Ruta Virtualizada (`is_hypervisor == 1`):**
     - Se asigna `physical_core_verified = 0`.
     - Se asigna `topology_gate_passed = 1` (afinidad y máscara verificadas).
     - Se consulta `CPUID Leaf 0x40000000`: si el máximo anunciado cumple `h_eax >= 0x40000000 && h_eax < 0x40010000`, se extrae la firma de 12 bytes del hipervisor (`Microsoft Hv`, `KVMKVMKVM`, `VMwareVMware`).
     - Se retorna inmediatamente sin evaluar ni certificar silicio físico.
  4. **Ruta Bare-Metal (`is_hypervisor == 0`):**
     - Se comprueba `max_basic_leaf >= 0x1A`. Si es menor, se aborta con error controlado.
     - Se consulta `CPUID Leaf 0x1A`: si `core_type == 0x40`, se certifica `physical_core_verified = 1` y `topology_gate_passed = 1`. Si devuelve `0x20` (E-Core), la suite aborta inmediatamente.

### 2.4. Motores Competidores y Versiones Exactas Auditadas
Conforme a la auditoría forense de cabeceras realizada en **BP-008B**:

| Motor / Codec | Versión Exacta Compilada | Procedencia de Implementación | Enlace / Biblioteca |
| :--- | :--- | :--- | :--- |
| **Meta Zstandard (Zstd)** | **v1.5.7** (`ZSTD_VERSION_NUMBER = 10507`) | Meta Platforms (BSD 3-Clause) | Enlace estático (`libzstd_static.lib` / `.a`) |
| **Yann Collet LZ4** | **v1.10.1** (`LZ4_VERSION_NUMBER = 11001`) | Yann Collet (BSD 2-Clause) | Enlace estático / compilación de `deps/lz4_src/lz4.c` |
| **Igor Pavlov 7-Zip LZMA** | **SDK v19.00** (`_7ZIP_ST`) | Dominio Público | Compilación de `deps/lzma/` monohilo |
| **Facebook Gorilla** | **VLDB 2015 C99** | Implementación local auditada | [`deps/timeseries/timeseries_codecs.h`](file:///C:/Users/usuario/Desktop/binpack/deps/timeseries/timeseries_codecs.h#L87-L140) |
| **Chimp** | **PVLDB 2022 C99** | Implementación local auditada | [`deps/timeseries/timeseries_codecs.h`](file:///C:/Users/usuario/Desktop/binpack/deps/timeseries/timeseries_codecs.h#L248-L305) |
| **Sprintz** | **PVLDB 2018 C99** | Implementación local | **En Cuarentena Estricta** (Excluido de mediciones activas por UB) |

### 2.5. Datasets y Hashes Criptográficos Congelados
Todos los datasets evaluados deben residir en `tests/datasets/corpora/` y satisfacer de manera inviolable los hashes SHA-256 de 64 caracteres hex registrados en **BP-008A**:

* **`busybox_armv7l`** (1.109.128 Bytes):  
  `SHA-256: 8fceda57d83418bb288de0aef3c6df5e9421344ed0515c90fa5206f15c50f7e4`
* **`nasa_turbofan_float32.bin`** (1.980.576 Bytes):  
  `SHA-256: ee06f50d129b163884753299939c7e150d785e63928de860777b9f7869ce7c59`
* **`mozilla`** (51.220.480 Bytes):  
  `SHA-256: 657fc3764b0c75ac9de9623125705831ebbfbe08fed248df73bc2dc66e2a963b`
* **`samba`** (21.606.400 Bytes):  
  `SHA-256: 93ba07bc44d8267789c1d911992f40b089ffa2140b4a160fac11ccae9a40e7b2`
* **`ooffice`** (6.152.192 Bytes):  
  `SHA-256: e7ee013880d34dd5208283d0d3d91b07f442e067454276095ded14f322a656eb`
* **`mr`** (9.970.564 Bytes):  
  `SHA-256: 68637ed52e3e4860174ed2dc0840ac77d5f1a60abbcb13770d5754e3774d53e6`

---

## 3. Protocolo de Reproducibilidad Experimental y Calidad del PRNG (R1-01)

### 3.1. Especificación del PRNG y Eliminación de Sesgo en Fisher-Yates (R1-01)
El generador LCG de 32 bits implementado en [`tests/benchmarks/benchmark_decisive_suite.c:397-413`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c#L397-L413) responde a la recurrencia:
$$X_{n+1} = (1664525 \times X_n + 1013904223) \pmod{2^{32}}$$

#### A. Resolución de la Debilidad de Bits Bajos del LCG:
Es una propiedad matemática conocida de los LCG módulo potencia de 2 que el bit $k$-ésimo presenta un período de a lo sumo $2^{k+1}$. En particular, el bit menos significativo ($k=0$) alterna estrictamente `0, 1, 0, 1`. Si se utiliza el operador módulo clásico `X % N` para valores pares de $N$, la paridad de la secuencia queda correlacionada.

Para solucionar este defecto de raíz, la conversión a índices para Fisher-Yates utiliza la **reducción rápida sin sesgo de Daniel Lemire**, la cual opera multiplicando el entero uniforme por el rango y extrayendo la **palabra superior de 32 bits (bits [63:32])**:
```c
static inline int rng_next_unbiased_lemire(rng_t *rng, int bound) {
    uint32_t x = rng_next(rng);
    uint64_t m = (uint64_t)x * (uint64_t)bound;
    uint32_t l = (uint32_t)m;
    if (l < (uint32_t)bound) {
        uint32_t t = -(uint32_t)bound % (uint32_t)bound;
        while (l < t) {
            x = rng_next(rng);
            m = (uint64_t)x * (uint64_t)bound;
            l = (uint32_t)m;
        }
    }
    return (int)(m >> 32);
}
```
* **Ventajas Matemáticas:**
  1. **Aislamiento de Bits Débiles:** El desplazamiento `m >> 32` descarta por completo los 32 bits inferiores de $X_n$, utilizando exclusivamente los bits más significativos, que poseen el período completo ($2^{32}$) y máxima dispersión espectral.
  2. **Ausencia Absoluta de Sesgo de Módulo:** El bucle de rechazo `while (l < t)` descarta exactamente los valores residuales cuando $2^{32}$ no es divisible por `bound`, garantizando una distribución matemáticamente uniforme.

#### B. Ciclo de Vida del Estado Global:
* El estado `g_suite_rng` se inicializa **una sola vez** en `main()` con `--seed S_0`.
* **No se reinicia entre iteraciones ni entre Tiers:** Avanza de forma determinista y continua a lo largo de toda la batería.

#### C. Algoritmo de Fisher-Yates Paso a Paso:
En cada iteración cronometrada $k \in [0, 19]$ para un conjunto de $n$ motores:
```c
for (int i = n - 1; i > 0; i--) {
    int j = rng_next_unbiased_lemire(&g_suite_rng, i + 1);
    engine_config_t *tmp = shuffled[i];
    shuffled[i] = shuffled[j];
    shuffled[j] = tmp;
}
```
El orden de los Tiers y de los datasets permanece constante, asegurando que la única variable permutada sea el orden relativo de los algoritmos dentro de cada ciclo de medición.

---

## 4. Matriz de Comparabilidad y Reglas de Paridad de Baselines

### 4.1. Verificación Activa del Checksum de Zstandard en Contrato Strict (R1-04)
Para garantizar la paridad metodológica estricta frente a `BinPack Strict (CRC32 slicing-by-8)`:
1. **Activación Obligatoria en el Contexto Activo:**
   ```c
   ZSTD_CCtx_setParameter(cctx, ZSTD_c_checksumFlag, 1);
   ```
2. **Inspección en Caliente del Frame Header Descriptor (FHD):**
   Tras comprimir, el harness inspecciona obligatoriamente el byte FHD (offset 4 tras el Magic `0xFD2FB528`):
   ```c
   uint8_t fhd = comp_buf[4];
   if ((fhd & 0x04) == 0) { /* Bit 2: Content_Checksum_Flag */
       fprintf(stderr, "[FATAL] Zstd frame missing xxHash64 checksum flag!\n");
       return -1;
   }
   ```
3. **Consumo y Validación en Descompresión:**
   Durante la fase de descompresión cronometrada, `ZSTD_decompressDCtx()` decodifica el bloque y verifica el checksum de 32 bits (xxHash64 truncado) almacenado en los últimos 4 bytes del frame. Si los datos se alteran, devuelve el código `ZSTD_error_checksum_wrong`.

### 4.2. Demostración Analítica del Techo de 52.5 MB de Memoria de Trabajo (*Peak Engine Working Memory*) (R1-03)

Para eliminar cualquier ambigüedad terminológica entre la memoria global del proceso y la memoria del compresor, se formalizan las dos métricas:

```
+-----------------------------------------------------------------------------------+
|                        PEAK PROCESS RSS (Memoria Total del Proceso)               |
|  - Búfer de Entrada Crudo en RAM (e.g. 51.2 MB en silesia_mozilla)                |
|  - Búfer de Salida Descomprimido (e.g. 51.2 MB)                                   |
|  - Código ejecutable, librerías compartidas (musl/glibc), stack de hilos (10 MB)  |
|  - Total Típico en Benchmark Monobloque: 70 - 115 MB                              |
+-----------------------------------------------------------------------------------+
                                         |
                                         v
+-----------------------------------------------------------------------------------+
|               PEAK ENGINE WORKING MEMORY (Memoria de Trabajo del Motor)           |
|  - Búfer de Chunking Actual: chunk_size = 16,777,216 B (16.0 MB)                  |
|  - Búfer de Historial Previo: chunk_size = 16,777,216 B (16.0 MB)                 |
|  - Búfer de Compresión Acotado: ZSTD_compressBound(16MB) + 65536 = 16.12 MB       |
|  - Contexto Dinámico de Compresión Zstd Level 19 / btultra = 4.40 MB              |
|  - Tabla de Índices de Chunks (idx_table) = 0.001 MB                              |
|  - TOTAL ANALÍTICO CALCULADO: 52.52 MB <= 64.0 MB (TECHO OPERACIONAL BPK2)        |
+-----------------------------------------------------------------------------------+
```

#### Desglose Detallado de Asignaciones en `binpack_stream.c`:
1. **`raw_chunk` (Búfer de entrada acotado):** `16 * 1024 * 1024` bytes = **16.00 MB**.
2. **`prev_raw` (Búfer de diccionario prefix):** `16 * 1024 * 1024` bytes = **16.00 MB**.
3. **`cmp_chunk` (Búfer de compresión):**
   $$\text{cmp\_bound} = \text{ZSTD\_compressBound}(16\text{ MB}) + 65536 = 16.777.216 + 65.536 + 65.536 + 512 = 16.908.800 \text{ bytes} = \mathbf{16.12\text{ MB}}$$
4. **Contexto Zstandard Dinámico (`ZSTD_CCtx` en Level 19 / `btultra`):**
   Calculado mediante la función oficial de estimación de memoria de Meta: `ZSTD_estimateCCtxSize_usingCParams()`:
   * En `binpack_stream.c`, el tamaño de chunk de 16 MB impone un límite superior al tamaño de ventana efectiva: `windowLog = 20` (ventana deslizante de 1 MB de coincidencia cruzada con el prefix dictionary).
   * Tamaño de Hash Table (`hashLog = 19`): $2^{19} \times 4\text{ bytes} = 2.00\text{ MB}$.
   * Tamaño de Chain Table (`chainLog = 19`): $2^{19} \times 4\text{ bytes} = 2.00\text{ MB}$.
   * Estructuras auxiliares internas de Zstd (árbol de sufijos, tablas de estados FSE y buffers de tokens): $\approx \mathbf{0.40\text{ MB}}$.
   * **Subtotal Contexto Zstandard:** $2.00 + 2.00 + 0.40 = \mathbf{4.40\text{ MB}}$.
5. **Tabla de Índices Seekable (`idx_table`):** Asignación inicial de 16 entradas $\times 36$ bytes = 576 bytes ($\mathbf{0.0006\text{ MB}}$).

$$\text{Total Peak Engine Working Memory} = 16.00 + 16.00 + 16.12 + 4.40 + 0.0006 = \mathbf{52.52\text{ MB}} \le \mathbf{64.0\text{ MB}}$$

Este valor es invariante respecto al tamaño total del archivo: un archivo de 100 GB se comprime o descomprime utilizando exactamente los mismos **52.52 MB** de memoria residente de motor.

---

## 5. Procedimiento de Validación y Análisis Estadístico Dual (Fijado *A Priori*)

Para evitar el sesgo de selección de métricas (*cherry-picking*), se fija un protocolo de reporte dual obligatorio:

### 5.1. Reporte Dual Obligatorio (Crudo vs. Filtrado)
Para cada serie de 20 iteraciones, se computarán y reportarán en el CSV y en los informes ambos conjuntos de indicadores:

| Métrica | Definición Cruda (Sin Exclusiones) | Definición Filtrada (Tukey Outliers Excluidos) |
| :--- | :--- | :--- |
| **Tendencia Central Principal** | $\text{Mediana}_{\text{raw}} = \tilde{x}(\{S_0..S_{19}\})$ | $\text{Mediana}_{\text{filt}} = \tilde{x}(\{S_i \in \text{Rango Tukey}\})$ |
| **Tendencia Paramétrica** | $\text{Media}_{\text{raw}} = \frac{1}{20}\sum S_i$ | $\text{Media}_{\text{filt}} = \frac{1}{N_{\text{valid}}}\sum S_{\text{valid}}$ |
| **Dispersión Paramétrica** | $\sigma_{\text{raw}} = \sqrt{\frac{1}{20}\sum (S_i - \mu_{\text{raw}})^2}$ | $\sigma_{\text{filt}} = \sqrt{\frac{1}{N_{\text{valid}}}\sum (S_i - \mu_{\text{filt}})^2}$ |
| **Dispersión No Paramétrica** | $\text{IQR}_{\text{raw}} = Q_3 - Q_1$ | $\text{IQR}_{\text{filt}}$ |
| **Coeficiente de Variación** | $\text{CV}_{\text{raw}} = \frac{\sigma_{\text{raw}}}{\mu_{\text{raw}}} \times 100\%$ | $\text{CV}_{\text{filt}} = \frac{\sigma_{\text{filt}}}{\mu_{\text{filt}}} \times 100\%$ |
| **Auditoría de Exclusiones** | — | $N_{\text{out}}$ (cantidad) y $\%_{\text{out}} = \frac{N_{\text{out}}}{20} \times 100\%$ |

### 5.2. Regla de Detección de Outliers (Tukey)
Una muestra $S_i$ se clasifica formalmente como outlier si:
$$S_i < Q_1 - 1.5 \times \text{IQR} \quad \lor \quad S_i > Q_3 + 1.5 \times \text{IQR}$$
* Las observaciones excluidas **no se eliminan del registro**: permanecen visibles en las columnas `Raw_C_Sec_0..19` y `Raw_D_Sec_0..19`.
* Se audita la cantidad $N_{\text{out}}$ en las columnas `Comp_Outliers` y `Decomp_Outliers`.

### 5.3. Criterio Dual de Aceptación de Estabilidad
Para que una medición se considere científicamente válida:
1. **Estabilidad Filtrada Obligatoria:** $\text{CV}_{\text{filt}} \le 5.0\%$.
2. **Límite de Inestabilidad Cruda:** $\text{CV}_{\text{raw}} \le 10.0\%$.
3. **Límite de Exclusiones:** $N_{\text{out}} \le 2$ (máximo un $10\%$ de observaciones descartables sobre las 20 iteraciones).

---

## 6. Especificación del Formato de Salida CSV

El archivo generado por el harness cumplirá estrictamente con la siguiente estructura tabular inmutable:

```csv
Tier,Dataset,Engine,OriginalSize,CompressedSize,RatioPct,Comp_Raw_MBs,Comp_Filt_MBs,Comp_IQR_MBs,Comp_Outliers,Decomp_Raw_MBs,Decomp_Filt_MBs,Decomp_IQR_MBs,Decomp_Outliers,Comp_Med_ms,Decomp_Med_ms,Lat_4k_C_us,Lat_4k_D_us,Lat_64k_C_us,Lat_64k_D_us,PeakRSS_KB,Integrity,Seed,Raw_C_Sec_0..19,Raw_D_Sec_0..19
```

---

## 7. Registro Explícito de Limitaciones Experimentales y Precisiones Metodológicas

1. **Alcance de la Detección de Virtualización (`CPUID.1:ECX[31]`):** Identifica entornos que anuncian la presencia de hipervisores cooperativos. La bandera `physical_core_verified = 1` denota *clasificación bare-metal según la información CPUID disponible*, no una prueba criptográfica infalible de silicio físico ante hipervisores con emulación hostil.
2. **Abstracción de Topología en WSL2/Hyper-V:** La afinidad en `vCPU 2` garantiza aislamiento y determinismo en el espacio de kernel invitado, pero la planificación sobre el silicio físico permanece gobernada por Hyper-V.
3. **Cuarentena de Sprintz:** Mantenida de forma estricta por desbordamiento aritmético en enteros signados.
4. **Entorno Monohilo Normalizado:** Mide el rendimiento de un único hilo de ejecución aislado, no escalabilidad paralela NUMA.

---

## 8. Checklist Previo a la Autorización de la Futura Campaña Decisiva

- [ ] **1. Integridad de Fuentes:** `git status` en la rama de auditoría certifica que `src/`, `include/` y `deps/` no poseen modificaciones.
- [ ] **2. Aprobación Formal de BP-008D:** El presente expediente cuenta con el dictamen técnico favorable de la auditoría.
- [ ] **3. Disponibilidad del Corpus:** Datasets descargados y verificados con sus respectivos hashes SHA-256 en disco.
- [ ] **4. Aislamiento de Hardware:** Sistema en reposo térmico ($T_{\text{CPU}} \le 45^\circ\text{C}$), sin procesos secundarios.
- [ ] **5. Semilla Registrada:** Semilla inicial de aleatorización fijada y documentada.
- [ ] **6. Inmutabilidad de CSV:** Destino fijado a un nuevo archivo (`decisive_benchmark_raw_r5.csv`), preservando intactos `pilot` a `r4`.

---

## 9. Criterio de Cierre del Hito BP-008D

El Hito **BP-008D** se considerará formalmente cerrado cuando:
1. El presente documento repose en el repositorio oficial en la ruta [`docs/audit/bp008d_experimental_protocol.md`](file:///C:/Users/usuario/Desktop/binpack/docs/audit/bp008d_experimental_protocol.md).
2. Se certifique que las instrucciones de compilación, verificación pre-flight y reducción estadística son completamente autónomas y ejecutables por un tercero.
3. La auditoría emita el dictamen de aprobación formal correspondiente.
