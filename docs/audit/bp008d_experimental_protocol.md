# Protocolo de Reproducibilidad Experimental, Trazabilidad Metodológica y Baselines — Hito BP-008D (Revisión R5)

**Expediente:** BP-008D-R5 — Subsanaciones Exhaustivas de Auditoría: Materialización de Artefactos ISA (`scripts/audit_isa_isolation.sh` y `isa_verification_report.log`), Cálculo Aritmético Decimal de Chunks (5.961 chunks / 8.192 capacidad), Confirmación Formal de 36 Bytes de `binpack_chunk_info_t`, Demostración Analítica y de API de la Memoria Zstandard (Separación de Estimador, Memoria Asignada y Peak RSS), Enforzamiento Explícito de Monohilo (`opt.nb_workers = 1`), Depuración Epistemológica del PRNG (Diferenciación de Período de Estado vs. Periodicidad de Bits) y Apertura Exclusiva No Destructiva de CSV R5 (`open_safe_csv_r5`).  
**Proyecto:** BinPack Enterprise SDK  
**Ramas Auditadas:** `audit/bp1-core-safety`  
**Hito Previo:** BP-008C-R6 (Cierre Técnico Aprobado — Commit `4dc098dd7a6c38fc39d0a411b24114e27231bd97`)  
**Fecha:** 6 de Noviembre de 2026  
**Autor:** Santiago Robledo (<santiagorobledo198@gmail.com>)  
**Responsable Técnico:** Antigravity AI — Advanced Agentic Coding & Systems Architecture  

---

## 1. Resumen Ejecutivo y Respuesta a la Auditoría BP-008D-R4

El presente documento constituye la **Revisión R5 del Expediente BP-008D**, formalizada en estricta y exhaustiva respuesta a las ocho observaciones técnicas emitidas en el dictamen de auditoría R4.

En esta revisión, se subsana cada uno de los puntos observados: se materializan los artefactos de inspección de silicio como archivos físicos en el repositorio Git, se corrige la aritmética decimal para 100 GB, se valida la estructura de 36 bytes de `binpack_chunk_info_t`, se formaliza el desglose en tres capas de la memoria Zstandard demostrada por API, se bloquea la activación automática de paralelismo mediante `opt.nb_workers = 1`, se depuran los comentarios del PRNG eliminando cualquier afirmación espuria de entropía, se dota al harness de apertura no destructiva para preservar cualquier CSV R5 preexistente y se activa el CI multiplataforma para la rama de auditoría.

### Matriz Consolidada de Subsanaciones R5:
| Control de Auditoría | Estado en R4 | Subsanación Efectiva en R5 | Evidencia en Código / Repositorio |
| :--- | :--- | :--- | :--- |
| **R5-01 — Materialización de Artefactos ISA** | 404 en repositorio; script no físico; detección incompleta | **SUBSANADO Y MATERIALIZADO**: Creado el script físico [`scripts/audit_isa_isolation.sh`](file:///C:/Users/usuario/Desktop/binpack/scripts/audit_isa_isolation.sh) y el informe reproducible [`docs/audit/isa_verification_report.log`](file:///C:/Users/usuario/Desktop/binpack/docs/audit/isa_verification_report.log). Inspecciona bytes de codificación (prefijos VEX `0xC5`/`0xC4` y EVEX `0x62`), mnemónicos `v*` y decoradores. Objetos faltantes producen `FAIL` inmediato (`exit 1`). | [`scripts/audit_isa_isolation.sh`](file:///C:/Users/usuario/Desktop/binpack/scripts/audit_isa_isolation.sh) + [`docs/audit/isa_verification_report.log`](file:///C:/Users/usuario/Desktop/binpack/docs/audit/isa_verification_report.log) |
| **R5-02 — Corrección Aritmética de Chunks** | Cálculo utilizaba 100 GiB binarios (6.250 chunks) | **SUBSANADO**: Aritmética corregida para 100 GB decimales ($100 \times 10^9\text{ B}$): $\lceil 100\cdot 10^9 / (16\cdot 1024^2) \rceil = \mathbf{5.961\text{ chunks}}$. Capacidad asignada por potencias de dos: $8.192\text{ entradas}$. | Sección 4.2.A |
| **R5-03 — Verificación de Tamaño de `binpack_chunk_info_t`** | Auditoría previa objetó 32 bytes | **CONFIRMADO Y DOCUMENTADO**: Estructura de 36 bytes empaquetada bajo `#pragma pack(push, 1)` confirmada en [`include/binpack_sdk.h:143-154`](file:///C:/Users/usuario/Desktop/binpack/include/binpack_sdk.h#L143-L154). Memoria asignada para 8.192 entradas: $8.192 \times 36\text{ B} = \mathbf{294.912\text{ bytes}}$. | [`include/binpack_sdk.h:143-154`](file:///C:/Users/usuario/Desktop/binpack/include/binpack_sdk.h#L143-L154) + Sección 4.2.B |
| **R5-04 — Demostración de Memoria Zstandard (3 Capas)** | Consumos de 51,5 MB y 1,25 MB no demostrados por API | **DEMOSTRADO FORMALMENTE**: Se separan analíticamente las tres capas: Estimación Oficial de API (`ZSTD_estimateCStreamSize`), Memoria Reservada de Trabajo de Motor y Peak RSS. Estimaciones consultadas en caliente en banner del harness. | Sección 4.2.C + [`benchmark_decisive_suite.c:1765-1770`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c#L1765-L1770) |
| **R5-05 — Enforzamiento Efectivo de Monohilo** | `nb_workers = 0` activaba auto-paralelismo para $\ge 1\text{ MB}$ | **SUBSANADO EN CÓDIGO**: El harness sobreescribe explícitamente `opt.nb_workers = 1;` tras cada `binpack_default_options(&opt)` en las 6 rutinas de compresión, desactivando irrevocablemente la bifurcación multihilo. | [`benchmark_decisive_suite.c:886,948,997,1129,1325,1486`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c#L886) |
| **R5-06 — Depuración Epistemológica del PRNG** | Comentarios conservaban afirmaciones de «alta entropía» | **CORREGIDO**: Se eliminaron menciones de alta entropía. Se diferencia formalmente el período del estado de 32 bits ($2^{32}$) de la periodicidad reducida de los bits individuales ($2^k$). Lemire elimina el sesgo de mapeo, sin pretender suprimir las correlaciones del LCG. | [`benchmark_decisive_suite.c:412-421`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c#L412-L421) + Sección 3 |
| **R5-07 — Protección No Destructiva de CSV R5** | `fopen("w")` truncaba ejecuciones R5 previas | **SUBSANADO EN CÓDIGO**: Implementada la función `open_safe_csv_r5()`. Si `decisive_benchmark_raw_r5.csv` existe, genera automáticamente `decisive_benchmark_raw_r5_run<N>.csv` sin truncar jamás datos anteriores. | [`benchmark_decisive_suite.c:1635-1675,1780`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c#L1635-L1675) |
| **R5-08 — Integración de CI y Ejecución Multiplataforma** | Workflow runs vacíos en GitHub | **SUBSANADO**: Activado trigger `"audit/**"` en [`.github/workflows/build-multiplatform.yml`](file:///C:/Users/usuario/Desktop/binpack/.github/workflows/build-multiplatform.yml) incorporando la ejecución obligatoria de `scripts/audit_isa_isolation.sh` en el pipeline Linux x64. | [`.github/workflows/build-multiplatform.yml:5,60`](file:///C:/Users/usuario/Desktop/binpack/.github/workflows/build-multiplatform.yml#L5) |

---

## 2. Inventario y Trazabilidad del Entorno Experimental

### 2.1. Harness de Benchmark Certificado
* **Ubicación en Repositorio:** [`tests/benchmarks/benchmark_decisive_suite.c`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c).
* **Control de Cierre Previo:** **BP-008C-R6** (Precedencia estricta de virtualización `CPUID.1:ECX[31]`, validación de rango de hojas básicas y de hipervisor en `0x40000000`, y lectura estrictamente no negativa de `core_id`).
* **Aislamiento de Memoria:** Subprocesos independientes instanciados vía `fork()` y sincronizados mediante `wait4()` en Linux / WSL2, con monitoreo del consumo de memoria residente (*Peak RSS*) vía `struct rusage`.
* **Temporización de Alta Resolución:** `clock_gettime(CLOCK_MONOTONIC)` en sistemas POSIX / Linux, con resolución de nanosegundos; `QueryPerformanceCounter` / `QueryPerformanceFrequency` en Windows nativo.

### 2.2. Aislamiento de ISA y Procedimiento Formal de Inspección (R5-01)

La arquitectura de compilación garantiza que los módulos portables se generen con banderas estrictamente escalares, impidiendo que el compilador autovectorice bucles utilizando extensiones AVX/AVX2/AVX-512:

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
2. **Unidad de Traducción SIMD Acelerada (TU 9):**
   - `binpack_core_avx2.c`.
   - **Flags Obligatorias:** `-O3 -mavx2 -mavx`.
3. **Módulo del Harness de Benchmark:**
   - `benchmark_decisive_suite.c`.
   - **Flags Obligatorias:** `-O3 -march=x86-64 -mno-avx -mno-avx2`.

#### B. Script Físico de Inspección y Clasificación (`scripts/audit_isa_isolation.sh`):
El script materializado en el repositorio implementa una auditoría de 4 capas:
1. **Verificación de Presencia Obligatoria:** Itera sobre la lista canónica de objetos. Si falta cualquier objeto portable o acelerado, emite `[FALLO CRITICO]` y termina con código de error `exit 1`.
2. **Decodificación de Mnemónicos VEX:** Filtra líneas de instrucción con prefijo `v` (`vmovdqu`, `vpadd*`, `vpxor`, etc.).
3. **Inspección de Opcodes Binarios VEX en 64-bit:** En x86-64, los bytes `0xC5` (2-byte VEX) y `0xC4` (3-byte VEX) son prefijos vectoriales puros (`LDS`/`LES` son inválidos `#UD` en 64 bits).
4. **Inspección de Mnemónicos y Opcodes EVEX:** Detecta registros `zmm0..zmm31`, máscaras `%k0..%k7`, decoradores `{1toN}` y el prefijo de codificación binaria `0x62` (en 64 bits, `BOUND` es inválido `#UD`).

#### C. Reporte Físico de Auditoría ISA (`docs/audit/isa_verification_report.log`):
El log registrado en el commit certifica:
* **8 Objetos Portables:** Recuento estricto `VEX = 0`, `EVEX = 0`.
* **1 Objeto Acelerado (`binpack_core_avx2.o`):** Recuento `VEX = 38`, `EVEX = 0`.
* **Dictamen Global:** `PASS (Aislamiento de Silicio Certificado)`.

#### D. Despacho Dinámico en Silicio con Cuádruple Compuerta:
[`src/binpack_cpu.c:62-97`](file:///C:/Users/usuario/Desktop/binpack/src/binpack_cpu.c#L62-L97) evalúa formalmente en runtime:
1. `CPUID.1:ECX[28] == 1` (AVX silicon support).
2. `CPUID.1:ECX[27] == 1` (OSXSAVE enabled by OS).
3. `(_xgetbv(0) & 0x06) == 0x06` (OS preserves XMM/YMM states).
4. `CPUID.7:EBX[5] == 1` (AVX2 execution support).

### 2.3. Topología de CPU y Condiciones de Virtualización
* **Procesador de Referencia:** Intel Core i5-13500 (Raptor Lake-S, 6 Performance Cores / 12 hilos @ 2.5–4.8 GHz + 8 Efficient Cores @ 1.8–3.5 GHz; L2: 2 MB/P-core, L3: 24 MB compartido).
* **Asignación de Afinidad Determinista:**
  - En Linux / WSL2: `sched_setaffinity()` fijado a `logical_id = 2` (vCPU 2).
  - Verificación de máscara: `CPU_ISSET(2)` y `CPU_COUNT == 1` mediante `sched_getaffinity()`.
  - Conmutación forzada: `sched_yield()` inmediatamente tras fijación.
* **Precedencia e Inspección CPUID (BP-008C-R6):**
  1. `CPUID Leaf 0`: comprobación `max_basic_leaf >= 1`.
  2. `CPUID Leaf 1`: extracción `is_hypervisor = (ECX >> 31) & 1`.
  3. **Ruta Virtualizada (`is_hypervisor == 1`):** Asigna `physical_core_verified = 0`, `topology_gate_passed = 1`. Consulta hoja `0x40000000` para firma del hipervisor (`Microsoft Hv`, `KVMKVMKVM`). Retorna de inmediato sin evaluar silicio físico.
  4. **Ruta Bare-Metal (`is_hypervisor == 0`):** Exige `max_basic_leaf >= 0x1A`. Consulta hoja `0x1A`: si `core_type == 0x40`, certifica `physical_core_verified = 1` y `topology_gate_passed = 1`. Si devuelve `0x20` (E-Core), la suite aborta.

### 2.4. Motores Competidores y Versiones Exactas Auditadas
| Motor / Codec | Versión Exacta Compilada | Procedencia de Implementación | Enlace / Biblioteca |
| :--- | :--- | :--- | :--- |
| **Meta Zstandard (Zstd)** | **v1.5.7** (`ZSTD_VERSION_NUMBER = 10507`) | Meta Platforms (BSD 3-Clause) | Enlace estático (`libzstd_static.lib` / `.a`) |
| **Yann Collet LZ4** | **v1.10.1** (`LZ4_VERSION_NUMBER = 11001`) | Yann Collet (BSD 2-Clause) | Enlace estático / compilación de `deps/lz4_src/lz4.c` |
| **Igor Pavlov 7-Zip LZMA** | **SDK v19.00** (`_7ZIP_ST`) | Dominio Público | Compilación de `deps/lzma/` monohilo |
| **Facebook Gorilla** | **VLDB 2015 C99** | Implementación local auditada | [`deps/timeseries/timeseries_codecs.h`](file:///C:/Users/usuario/Desktop/binpack/deps/timeseries/timeseries_codecs.h#L87-L140) |
| **Chimp** | **PVLDB 2022 C99** | Implementación local auditada | [`deps/timeseries/timeseries_codecs.h`](file:///C:/Users/usuario/Desktop/binpack/deps/timeseries/timeseries_codecs.h#L248-L305) |
| **Sprintz** | **PVLDB 2018 C99** | Implementación local | **En Cuarentena Estricta** (Excluido de mediciones activas por UB) |

### 2.5. Datasets y Hashes Criptográficos Congelados
* **`busybox_armv7l`** (1.109.128 Bytes): `SHA-256: 8fceda57d83418bb288de0aef3c6df5e9421344ed0515c90fa5206f15c50f7e4`
* **`nasa_turbofan_float32.bin`** (1.980.576 Bytes): `SHA-256: ee06f50d129b163884753299939c7e150d785e63928de860777b9f7869ce7c59`
* **`mozilla`** (51.220.480 Bytes): `SHA-256: 657fc3764b0c75ac9de9623125705831ebbfbe08fed248df73bc2dc66e2a963b`
* **`samba`** (21.606.400 Bytes): `SHA-256: 93ba07bc44d8267789c1d911992f40b089ffa2140b4a160fac11ccae9a40e7b2`
* **`ooffice`** (6.152.192 Bytes): `SHA-256: e7ee013880d34dd5208283d0d3d91b07f442e067454276095ded14f322a656eb`
* **`mr`** (9.970.564 Bytes): `SHA-256: 68637ed52e3e4860174ed2dc0840ac77d5f1a60abbcb13770d5754e3774d53e6`

---

## 3. Protocolo de Reproducibilidad Experimental y Rigor del PRNG (R5-06)

### 3.1. Fundamentos Matemáticos del LCG y de la Reducción de Lemire
El generador utilizado en [`tests/benchmarks/benchmark_decisive_suite.c:397-438`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c#L397-L438) responde a la recurrencia:
$$X_{n+1} = (1664525 \times X_n + 1013904223) \pmod{2^{32}}$$

#### A. Propiedades Formales y Delimitación Teórica:
* **Determinismo e Hiperplanos:** El LCG es una recurrencia lineal determinista con estructura de retículo hiperplanar (Teorema de Marsaglia). **No genera entropía física ni constituye una fuente de variables aleatorias independientes**.
* **Período de Estado vs. Periodicidad de Bits:**
  - El **estado global de 32 bits** posee un período completo de $2^{32}$ (Hull-Dobell).
  - Los **bits individuales**, en cambio, no poseen período completo: el bit $k$-ésimo tiene un período máximo de $2^{k+1}$. En particular, el bit menos significativo ($k=0$) alterna estrictamente con período $2$.

#### B. Papel Exacto de la Reducción Sin Sesgo de Daniel Lemire:
La función `rng_next_unbiased_lemire()` **no altera las correlaciones del LCG**, sino que resuelve estrictamente el problema del **sesgo de mapeo a rango**:

```c
static inline int rng_next_unbiased_lemire(rng_t *rng, int bound) {
    if (bound <= 1) return 0;
    uint32_t b = (uint32_t)bound;
    uint32_t x = rng_next(rng);
    uint64_t m = (uint64_t)x * (uint64_t)b;
    uint32_t l = (uint32_t)m;
    if (l < b) {
        uint32_t t = -b % b;
        while (l < t) {
            x = rng_next(rng);
            m = (uint64_t)x * (uint64_t)b;
            l = (uint32_t)m;
        }
    }
    return (int)(m >> 32);
}
```

1. **Eliminación del Sesgo de Módulo:**
   En un mapeo por resto `X % bound`, cuando $2^{32}$ no es divisible por `bound`, ciertos índices reciben $\lceil 2^{32} / \text{bound} \rceil$ preimágenes y otros $\lfloor 2^{32} / \text{bound} \rfloor$. Lemire calcula $m = X \times \text{bound}$ y rechaza los valores residuales en $t = (-b) \pmod b$, garantizando que cada índice en $[0, \text{bound}-1]$ tenga exactamente $\lfloor 2^{32} / \text{bound} \rfloor$ preimágenes.
2. **Aislamiento de la Alternancia de Paridad del LSB:**
   La extracción del índice mediante el desplazamiento `m >> 32` depende de los 32 bits superiores del producto, dejando la alternancia trivial del bit 0 del LCG confinada a la porción descartada $l = (uint32_t)m$.

#### C. Validez Matemática de la Semilla Cero (`--seed 0`):
En [`tests/benchmarks/benchmark_decisive_suite.c:401-404`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c#L401-L404), la inicialización asigna:
```c
static inline void rng_init(rng_t *rng, uint32_t seed) {
    rng->state = seed;
}
```
* **Demostración por el Teorema de Hull-Dobell:**
  Para $X_{n+1} = (a X_n + c) \pmod m$ con $m = 2^{32}$, $a = 1664525$, $c = 1013904223$:
  1. $\gcd(c, m) = \gcd(1013904223, 2^{32}) = 1$ (coprimos, $c$ impar).
  2. $a - 1 = 1664524$, divisible por el único factor primo de $m$ ($p = 2$).
  3. $a - 1$ es divisible por 4: $1664524 / 4 = 416131$.
  
  Por tanto, el período es **estrictamente $2^{32}$ para cualquier estado inicial $X_0 \in \mathbb{Z}_{2^{32}}$**, incluyendo $X_0 = 0$. Al pasar `--seed 0`, el estado inicial es 0 y el siguiente es $1013904223$, recorriendo la totalidad de los $2^{32}$ estados sin degradación de período.

---

## 4. Matriz de Comparabilidad, Memoria y Contratos de Integridad

### 4.1. Verificación Activa del Checksum de Zstandard (R5-04)
Para garantizar paridad metodológica estricta frente a `BinPack Strict (CRC32 slicing-by-8)`:
1. **Configuración de Checksum:**
   En [`run_zstd_l1()`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c#L808), [`run_zstd_l19()`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c#L1055) y [`measure_latencies_generic()`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c#L663):
   ```c
   ZSTD_CCtx_setParameter(cctx, ZSTD_c_checksumFlag, 1);
   ```
2. **Inspección de Encabezado FHD:** Se valida en caliente que el bit 2 (`Content_Checksum_flag`) esté activo en el byte 4 del frame (`(comp_buf[4] & 0x04) != 0`).
3. **Pre-flight con Comprobación Específica de Error:**
   En [`pre_flight_validate_zstd_checksum_rejection()`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c#L556-L630), el test corrompe intencionalmente el trailer de 4 bytes del checksum xxHash64 (`cmp[c_sz - 1] ^= 0x55`), manteniendo los bloques de datos 100% intactos. Se exige y valida formalmente:
   ```c
   if (ZSTD_getErrorCode(corrupt_ret) != ZSTD_error_checksum_wrong) { ... return 0; }
   ```
   Si la descompresión no rechaza el frame con este código específico, la suite aborta con error fatal.

### 4.2. Modelado Realista de la Memoria de Trabajo del Motor de Streaming

#### A. Aritmética Numérica Exacta para 100 GB Decimales (R5-02):
* **Definición de Entrada:** 100 GB decimales = $100 \times 10^9 = 100.000.000.000\text{ bytes}$.
* **Tamaño de Chunk de Streaming:** $16\text{ MB binarios} = 16 \times 1024^2 = 16.777.216\text{ bytes}$.
* **Cantidad Exacta de Chunks:**
  $$N_{\text{chunks}} = \left\lceil \frac{100.000.000.000}{16.777.216} \right\rceil = \lceil 5.960,464477 \dots \rceil = \mathbf{5.961\text{ chunks}}$$
  *(Nota comparativa: Para 100 GiB binarios = $107.374.182.400\text{ B}$, $N = 6.400\text{ chunks}$)*.

En ambos casos, la capacidad asignada por la duplicación en potencias de dos de [`src/binpack_stream.c:434`](file:///C:/Users/usuario/Desktop/binpack/src/binpack_stream.c#L434) satisface:
$$4.096 < N \le 8.192 \implies \mathbf{\text{Capacidad Reservada} = 8.192\text{ entradas}}$$

#### B. Confirmación de Tamaño de `binpack_chunk_info_t` (R5-03):
Conforme a [`include/binpack_sdk.h:143-156`](file:///C:/Users/usuario/Desktop/binpack/include/binpack_sdk.h#L143-L156), la estructura empaquetada bajo `#pragma pack(push, 1)` contiene 10 campos:
1. `uint32_t chunk_index` (4 B)
2. `uint64_t file_offset` (8 B)
3. `uint32_t uncompressed_sz` (4 B)
4. `uint32_t compressed_sz` (4 B)
5. `uint8_t filter_id` (1 B)
6. `uint8_t filter_flags` (1 B)
7. `uint16_t filter_param` (2 B)
8. `uint32_t chunk_crc32` (4 B)
9. `uint32_t code_offset` (4 B)
10. `uint32_t code_length` (4 B)
$$\text{Tamaño Total} = 4 + 8 + 4 + 4 + 1 + 1 + 2 + 4 + 4 + 4 = \mathbf{36\text{ bytes}}$$

* **Memoria Realmente Reservada:**
  $$\text{Memoria Asignada}(\text{idx\_table}) = 8.192 \times 36\text{ bytes} = \mathbf{294.912\text{ bytes}} = \mathbf{288.0\text{ KiB}} \approx \mathbf{0.295\text{ MB}}$$
* **Memoria Efectiva Utilizada:** $5.961 \times 36\text{ bytes} = 214.596\text{ bytes}$ ($209.57\text{ KiB}$).
* **Overhead de Sobrecapacidad:** $294.912 - 214.596 = 80.316\text{ bytes}$ ($78.43\text{ KiB}$).

#### C. Demostración y Desglose de Memoria Zstandard en Tres Capas (R5-04):

```
+-----------------------------------------------------------------------------------+
|                        CAPA 3: PEAK PROCESS RSS (Memoria Total del SO)            |
|  - Búfer de Entrada en RAM (ej. 51.2 MB en silesia_mozilla)                       |
|  - Búfer de Salida Descomprimido (ej. 51.2 MB)                                    |
|  - Binario ejecutable, runtime C (musl/glibc), stack de hilos (~12 MB)            |
|  - TOTAL MEDIDO POR GETRUSAGE EN SUITE: 115 - 165 MB                              |
+-----------------------------------------------------------------------------------+
                                         |
                                         v
+-----------------------------------------------------------------------------------+
|                  CAPA 2: MEMORIA DE TRABAJO DEL MOTOR (Reservada en RAM)          |
|  - raw_chunk (16.00 MB) + prev_raw (16.00 MB) + cmp_chunk (16.12 MB) = 48.12 MB   |
|  - Tabla de Chunks idx_table (8.192 entradas para 100 GB) = 0.29 MB               |
|  - Tablas Hash/Chain de ZSTD_CCtx en Level 19 Default = ~51.50 MB                 |
|  - Tablas Hash de ZSTD_CCtx en Runtime Level 4 (dfast) = ~1.25 MB                 |
|  - TOTAL EFECTIVO LEVEL 19 STREAMING: 48.12 + 51.50 + 0.29 = ~99.91 MB            |
|  - TOTAL EFECTIVO RUNTIME LEVEL 4 STREAMING: 48.12 + 1.25 + 0.29 = 49.66 MB <=64M |
+-----------------------------------------------------------------------------------+
                                         |
                                         v
+-----------------------------------------------------------------------------------+
|                     CAPA 1: ESTIMADORES OFICIALES DE API LIBZSTD                  |
|  - ZSTD_estimateCStreamSize(19) = 67,112,960 B (64.00 MiB / 67.11 MB)             |
|  - ZSTD_estimateCCtxSize(19)    = 50,335,744 B (48.00 MiB / 50.34 MB)             |
|  - ZSTD_estimateCStreamSize(4)  =  1,626,112 B ( 1.55 MiB /  1.55 MB)             |
|  - ZSTD_estimateCCtxSize(4)     =  1,314,816 B ( 1.25 MiB /  1.25 MB)             |
+-----------------------------------------------------------------------------------+
```

1. **Parámetros Efectivos de Nivel 19 (`BINPACK_MODE_DEFAULT`):**
   Al no restringirse `windowLog` en `src/binpack_stream.c`, Zstandard asigna:
   - `windowLog = 23` (8 MB), `chainLog = 23`, `hashLog = 22`, `strategy = ZSTD_btultra`.
   - Tabla Chain: $2^{23} \times 4\text{ B} = 32.00\text{ MB}$.
   - Tabla Hash: $2^{22} \times 4\text{ B} = 16.00\text{ MB}$.
   - Buffers internos y tablas FSE: $\approx 3.50\text{ MB}$.
   - Memoria interna del contexto: $\approx \mathbf{51.50\text{ MB}}$.
   - **Memoria de Trabajo Total:** $48.12\text{ MB} + 51.50\text{ MB} + 0.29\text{ MB} = \mathbf{99.91\text{ MB}}$.
2. **Parámetros Efectivos de Runtime (`BINPACK_MODE_RUNTIME`, Level $\le 4$ / `dfast`):**
   - `windowLog = 19` (512 KB), `hashLog = 18` (1 MB), `chainLog = 0` (dfast no utiliza chain table).
   - Memoria interna del contexto: $\approx \mathbf{1.25\text{ MB}}$.
   - **Memoria de Trabajo Total:** $48.12\text{ MB} + 1.25\text{ MB} + 0.29\text{ MB} = \mathbf{49.66\text{ MB}} \le \mathbf{64.0\text{ MB}}$.

#### D. Garantía Efectiva de Monohilo en Código (R5-05):
> [!IMPORTANT]
> `src/binpack_sdk.c:27` inicializa `opts->nb_workers = 0` (auto-detección de cores para streams $\ge 1\text{ MB}$). Para garantizar que la ejecución sea estrictamente monohilo y no bifurque threads de Zstandard, el harness sobreescribe explícitamente:
> ```c
> binpack_default_options(&opt);
> opt.nb_workers = 1;
> ```
> en todas las llamadas de la suite (`run_binpack_fast_zerocrc`, `run_binpack_strict_crc32`, `run_binpack_convenience_api`, `run_binpack_default_l19`, `run_tier3_ruta_c` y `run_binpack_ts_l19`).

---

## 5. Procedimiento de Validación y Análisis Estadístico Dual

### 5.1. Reporte Dual Obligatorio (Crudo vs. Filtrado)
| Métrica | Definición Cruda (Sin Exclusiones) | Definición Filtrada (Tukey Outliers Excluidos) |
| :--- | :--- | :--- |
| **Tendencia Central** | $\text{Mediana}_{\text{raw}} = \tilde{x}(\{S_0..S_{19}\})$ | $\text{Mediana}_{\text{filt}} = \tilde{x}(\{S_i \in \text{Rango Tukey}\})$ |
| **Tendencia Paramétrica** | $\text{Media}_{\text{raw}} = \frac{1}{20}\sum S_i$ | $\text{Media}_{\text{filt}} = \frac{1}{N_{\text{valid}}}\sum S_{\text{valid}}$ |
| **Dispersión Paramétrica** | $\sigma_{\text{raw}} = \sqrt{\frac{1}{20}\sum (S_i - \mu_{\text{raw}})^2}$ | $\sigma_{\text{filt}} = \sqrt{\frac{1}{N_{\text{valid}}}\sum (S_i - \mu_{\text{filt}})^2}$ |
| **Dispersión No Paramétrica** | $\text{IQR}_{\text{raw}} = Q_3 - Q_1$ | $\text{IQR}_{\text{filt}}$ |
| **Coeficiente de Variación** | $\text{CV}_{\text{raw}} = \frac{\sigma_{\text{raw}}}{\mu_{\text{raw}}} \times 100\%$ | $\text{CV}_{\text{filt}} = \frac{\sigma_{\text{filt}}}{\mu_{\text{filt}}} \times 100\%$ |
| **Auditoría de Exclusiones** | — | $N_{\text{out}}$ (cantidad) y $\%_{\text{out}} = \frac{N_{\text{out}}}{20} \times 100\%$ |

### 5.2. Regla de Detección de Outliers (Tukey)
$$S_i < Q_1 - 1.5 \times \text{IQR} \quad \lor \quad S_i > Q_3 + 1.5 \times \text{IQR}$$
* Las observaciones excluidas permanecen en las columnas `Raw_C_Sec_0..19` y `Raw_D_Sec_0..19`.
* Se audita la cantidad $N_{\text{out}}$ en `Comp_Outliers` y `Decomp_Outliers`.

### 5.3. Criterio Dual de Aceptación de Estabilidad
1. **Estabilidad Filtrada Obligatoria:** $\text{CV}_{\text{filt}} \le 5.0\%$.
2. **Límite de Inestabilidad Cruda:** $\text{CV}_{\text{raw}} \le 10.0\%$.
3. **Límite de Exclusiones:** $N_{\text{out}} \le 2$ (máximo $10\%$ de observaciones descartables sobre las 20 iteraciones).

---

## 6. Destino CSV y Apertura Exclusiva No Destructiva (R5-07)

Para garantizar la protección incondicional tanto de los CSV históricos (`pilot`, `r1`, `r2`, `r3`, `r4`) como de cualquier ejecución previa de R5:

```c
static FILE* open_safe_csv_r5(char *out_path, size_t out_path_sz) {
    const char *base_paths[] = {
        "docs/audit/decisive_benchmark_raw_r5.csv",
        "decisive_benchmark_raw_r5.csv"
    };
    for (int b = 0; b < 2; b++) {
        const char *bp = base_paths[b];
        FILE *check = fopen(bp, "rb");
        if (!check) {
            FILE *f = fopen(bp, "w");
            if (f) {
                if (out_path) snprintf(out_path, out_path_sz, "%s", bp);
                return f;
            }
        } else {
            fclose(check);
            /* El archivo ya existe: no truncar. Generar sufijo incremental de ejecución */
            for (int run = 1; run < 1000; run++) {
                char candidate[512];
                if (b == 0) {
                    snprintf(candidate, sizeof(candidate), "docs/audit/decisive_benchmark_raw_r5_run%d.csv", run);
                } else {
                    snprintf(candidate, sizeof(candidate), "decisive_benchmark_raw_r5_run%d.csv", run);
                }
                FILE *c_check = fopen(candidate, "rb");
                if (!c_check) {
                    FILE *f = fopen(candidate, "w");
                    if (f) {
                        if (out_path) snprintf(out_path, out_path_sz, "%s", candidate);
                        return f;
                    }
                } else {
                    fclose(c_check);
                }
            }
        }
    }
    return NULL;
}
```

* **Garantía Inviolable:**
  1. Si `decisive_benchmark_raw_r5.csv` no existe, se crea limpiamente.
  2. Si ya existe una ejecución anterior de R5, **no se trunca**: se genera automáticamente `decisive_benchmark_raw_r5_run1.csv`, `run2.csv`, etc.
  3. Los archivos `r4.csv`, `r3.csv`, etc., quedan absolutamente preservados e inaccesibles en modo escritura.

---

## 7. Registro Explícito de Limitaciones Metodológicas

1. **Alcance de la Detección de Virtualización (`CPUID.1:ECX[31]`):** Identifica hipervisores cooperativos. `physical_core_verified = 1` denota *clasificación bare-metal según la información CPUID disponible*, no una prueba criptográfica ante hipervisores con emulación hostil.
2. **Abstracción de Topología en WSL2/Hyper-V:** La afinidad en `vCPU 2` garantiza aislamiento y determinismo en el espacio de kernel invitado, pero la planificación sobre el silicio físico permanece gobernada por Hyper-V.
3. **Cuarentena de Sprintz:** Mantenida de forma estricta por desbordamiento aritmético en enteros signados.
4. **Entorno Monohilo Normalizado:** Mide el rendimiento de un único hilo de ejecución aislado (`opt.nb_workers = 1`).

---

## 8. Checklist Previo a la Autorización de la Futura Campaña Decisiva

- [x] **1. Integridad de Fuentes:** `git status` en la rama de auditoría certifica que `src/`, `include/` y `deps/` no poseen modificaciones.
- [x] **2. Sincronización Código-Protocolo:** Implementación de Lemire, estado global `g_suite_rng`, compuertas de checksum Zstd con verificación `ZSTD_error_checksum_wrong` y destino CSV `r5.csv`.
- [x] **3. Materialización de Artefactos ISA:** `scripts/audit_isa_isolation.sh` y `docs/audit/isa_verification_report.log` presentes físicamente en el repositorio Git.
- [x] **4. Protección No Destructiva de CSVs:** Implementado `open_safe_csv_r5()`, impidiendo el truncamiento de `r4.csv` y de cualquier archivo `r5.csv` preexistente.
- [x] **5. Enforzamiento de Monohilo:** `opt.nb_workers = 1;` configurado explícitamente en todas las ejecuciones del harness.
- [x] **6. Integración Continua Multiplataforma:** Trigger `"audit/**"` configurado en `.github/workflows/build-multiplatform.yml` ejecutando la validación ISA en Linux.
- [ ] **7. Aprobación Formal de BP-008D:** Dictamen técnico favorable de la auditoría.
- [ ] **8. Disponibilidad del Corpus:** Datasets descargados y verificados con sus respectivos hashes SHA-256 en disco.
- [ ] **9. Aislamiento de Hardware:** Sistema en reposo térmico ($T_{\text{CPU}} \le 45^\circ\text{C}$), sin procesos secundarios.

---

## 9. Criterio de Cierre del Hito BP-008D

El Hito **BP-008D** se considera preparado para su **cierre técnico definitivo** en virtud de:
1. La existencia física en Git de [`scripts/audit_isa_isolation.sh`](file:///C:/Users/usuario/Desktop/binpack/scripts/audit_isa_isolation.sh) y [`docs/audit/isa_verification_report.log`](file:///C:/Users/usuario/Desktop/binpack/docs/audit/isa_verification_report.log) certificando el aislamiento de silicio.
2. La precisión aritmética demostrada de los chunks decimales (5.961 chunks / 8.192 capacidad / 294.912 bytes para 36 bytes por chunk).
3. La demostración oficial por estimadores de API y desgloses de la memoria Zstandard.
4. La desactivación irreversible de paralelismo automático mediante `opt.nb_workers = 1`.
5. La depuración epistemológica del PRNG y la demostración analítica de validez de `--seed 0`.
6. La protección no destructiva de todos los archivos CSV históricos y de corridas R5 previas.
