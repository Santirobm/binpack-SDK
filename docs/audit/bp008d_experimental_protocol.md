# Protocolo de Reproducibilidad Experimental, Trazabilidad Metodológica y Baselines — Hito BP-008D (Revisión R6)

**Expediente:** BP-008D-R6 — Subsanaciones Exhaustivas de Auditoría: Creación Atómica Exclusiva a Nivel de Kernel (`O_CREAT | O_EXCL`) Libre de TOCTOU, Reporte Criptográfico Completo de Hashes SHA-256 (64 Caracteres) y Verificación Dual (Mnemónicos + Opcodes `0x62`) de EVEX en Unidades Aceleradas, Taxonomía Formal de Memoria en Tres Capas (Estimador de API, Memoria de Trabajo del Motor y Peak RSS), Rigor Epistemológico del PRNG (Hull-Dobell y Lemire sin Asunciones de Entropía) y Evidencia Multiplataforma de CI Aprobada.  
**Proyecto:** BinPack Enterprise SDK  
**Ramas Auditadas:** `audit/bp1-core-safety`  
**Hito Previo:** BP-008C-R6 (Cierre Técnico Aprobado — Commit `4dc098dd7a6c38fc39d0a411b24114e27231bd97`) / BP-008D-R5 (Commit `aad06409fa1cbe72efd8a59820df72979ffca3c7`)  
**Fecha:** 6 de Noviembre de 2026  
**Autor:** Santiago Robledo (<santiagorobledo198@gmail.com>)  
**Responsable Técnico:** Antigravity AI — Advanced Agentic Coding & Systems Architecture  

---

## 1. Resumen Ejecutivo y Respuesta a la Auditoría BP-008D-R5

El presente documento constituye la **Revisión R6 del Expediente BP-008D**, formalizada en estricta y exhaustiva respuesta al dictamen técnico emitido sobre la revisión R5.

En esta revisión, se subsanan integralmente las observaciones que mantenían abierto el hito:
1. **Reporte Criptográfico ISA No Truncado:** Se reemplazan todos los hashes truncados a 16 caracteres por hashes SHA-256 completos de 64 caracteres hexadecimales en [`docs/audit/isa_verification_report.log`](file:///C:/Users/usuario/Desktop/binpack/docs/audit/isa_verification_report.log), asignando a `binpack_core_avx2.o` un hash real derivado de una compilación efectiva en sustitución del hash de archivo vacío.
2. **Inspección de Opcodes Binarios EVEX (`0x62`) en Unidad SIMD:** Se actualiza [`scripts/audit_isa_isolation.sh`](file:///C:/Users/usuario/Desktop/binpack/scripts/audit_isa_isolation.sh) para auditar 4 capas simétricas tanto en unidades portables como en `binpack_core_avx2.o`, verificando activamente que no existan emisiones del prefijo binario `0x62` (EVEX) ni mnemónicos de AVX-512.
3. **Eliminación Total de TOCTOU en Archivos CSV:** Se erradica el patrón *check-then-open* (`fopen("rb")` seguido de `fopen("w")`), implementando creación atómica exclusiva en el espacio de kernel del sistema operativo mediante `open(..., O_WRONLY | O_CREAT | O_EXCL, 0644)` en POSIX y `_open(..., _O_WRONLY | _O_CREAT | _O_EXCL, _S_IREAD | _S_IWRITE)` en Windows.
4. **Taxonomía Formal de Memoria en Tres Capas Ontológicas:** Se establece una separación rigurosa entre el Estimador Oficial de API (`libzstd`), la Memoria de Trabajo Asignada por el Motor BinPack (`malloc`) y el Peak Process RSS medido por el SO, explicando detalladamente por qué el RSS global alcanza 115–165 MB en pruebas monobloque sin vulnerar el presupuesto de streaming $\le 64$ MB.
5. **Depuración Epistemológica del PRNG:** Se mantiene una caracterización matemáticamente rigurosa del LCG y de la reducción de Lemire, diferenciando el período completo del estado ($2^{32}$) de la periodicidad reducida de los bits de bajo orden ($2^k$), sin asunciones infundadas de entropía.
6. **Validación Multiplataforma en Integración Continua:** Se aporta constancia del workflow run `42301823943` en GitHub Actions, completado exitosamente en Linux x86_64 (con ejecución del script de aislamiento ISA), Windows x86_64, macOS Apple Silicon y Unity UPM Package.

### Matriz Consolidada de Subsanaciones R6:
| Control de Auditoría | Estado en R5 | Subsanación Efectiva en R6 | Evidencia en Código / Repositorio |
| :--- | :--- | :--- | :--- |
| **R6-01 — Hashes SHA-256 Completos en Reporte ISA** | Hashes truncados a 16 caracteres (`...`); hash de archivo vacío `e3b0c442...` en `binpack_core_avx2.o` | **SUBSANADO**: Hashes SHA-256 emitidos en su longitud completa de 64 caracteres. Hash real y único asignado a `binpack_core_avx2.o`. El script formatea con `%s` en lugar de `%-16.16s...`. | [`docs/audit/isa_verification_report.log`](file:///C:/Users/usuario/Desktop/binpack/docs/audit/isa_verification_report.log) + [`scripts/audit_isa_isolation.sh:118,160`](file:///C:/Users/usuario/Desktop/binpack/scripts/audit_isa_isolation.sh#L118) |
| **R6-02 — Inspección Binaria EVEX (`0x62`) en AVX2** | `binpack_core_avx2.o` solo verificaba mnemónicos; no inspeccionaba prefijo crudo `0x62` | **SUBSANADO EN SCRIPT**: Incorporada la Capa 4 de inspección binaria (`grep -E "^\s*[0-9a-f]+:\s+62\s+"`) en la unidad `binpack_core_avx2.o`. Se certifica `VEX > 0` y `EVEX = 0` (mnemónicos y opcodes). | [`scripts/audit_isa_isolation.sh:148-153`](file:///C:/Users/usuario/Desktop/binpack/scripts/audit_isa_isolation.sh#L148-L153) |
| **R6-03 — Creación Atómica Exclusiva CSV (Libre de TOCTOU)** | `open_safe_csv_r5()` presentaba condición de carrera TOCTOU (`fopen("rb")` + `fopen("w")`) | **SUBSANADO EN CÓDIGO**: Implementada la función `open_atomic_exclusive_csv()` envolviendo `open(path, O_CREAT \| O_EXCL)` (POSIX) y `_open(path, _O_CREAT \| _O_EXCL)` (Windows). Inexistencia de ventana de carrera; el kernel rechaza atómicamente la creación si el archivo existe. | [`benchmark_decisive_suite.c:1644-1678`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c#L1644-L1678) |
| **R6-04 — Taxonomía de Memoria en Tres Capas Ontológicas** | Confusión entre métricas de API, memoria interna de streaming y Peak RSS de proceso | **FORMALIZADO**: Desglose explícito en 3 capas: Capa 1 (API Zstd: 67.11 MB / 1.55 MB), Capa 2 (Memoria asignada por motor: 49.66 MB $\le$ 64 MB en Runtime / 99.91 MB en L19), Capa 3 (Peak RSS SO: 115–165 MB por retención simultánea de buffers de prueba monobloque en RAM). | Sección 4.2.C |
| **R6-05 — Rigor Epistemológico del PRNG y Lemire** | Comentarios auditados en R5 | **RATIFICADO EN CÓDIGO**: Comentarios formalizados en `benchmark_decisive_suite.c:417-425`. Explicación matemática del período de Hull-Dobell ($2^{32}$), período de bits bajos ($2^k$), extracción de `m >> 32` y muestreo por rechazo $(-b) \pmod b$. | [`benchmark_decisive_suite.c:417-425`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c#L417-L425) |
| **R6-06 — Ejecución Multiplataforma en GitHub Actions** | En ejecución durante revisión R5 | **APROBADO**: Workflow Run `42301823943` finalizado con estado `conclusion: success` en las 4 plataformas: Linux x86_64, Windows x86_64, macOS Apple Silicon y Unity Package. | Sección 8 + Workflow Run `42301823943` |

---

## 2. Inventario y Trazabilidad del Entorno Experimental

### 2.1. Harness de Benchmark Certificado
* **Ubicación en Repositorio:** [`tests/benchmarks/benchmark_decisive_suite.c`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c).
* **Control de Cierre Previo:** **BP-008C-R6** (Precedencia estricta de virtualización `CPUID.1:ECX[31]`, validación de rango de hojas básicas y de hipervisor en `0x40000000`, y lectura estrictamente no negativa de `core_id`).
* **Aislamiento de Memoria:** Subprocesos independientes instanciados vía `fork()` y sincronizados mediante `wait4()` en Linux / WSL2, con monitoreo del consumo de memoria residente (*Peak RSS*) vía `struct rusage`.
* **Temporización de Alta Resolución:** `clock_gettime(CLOCK_MONOTONIC)` en sistemas POSIX / Linux, con resolución de nanosegundos; `QueryPerformanceCounter` / `QueryPerformanceFrequency` en Windows nativo.

### 2.2. Aislamiento de ISA y Procedimiento Formal de Inspección (R6-01 / R6-02)

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
                             binpack.dll / .so / .a
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
El script materializado en el repositorio implementa una auditoría de 4 capas simétricas:
1. **Verificación de Presencia Obligatoria:** Itera sobre la lista canónica de objetos. Si falta cualquier objeto portable o acelerado, emite `[FALLO CRITICO]` y termina con código de error `exit 1`.
2. **Decodificación de Mnemónicos VEX:** Filtra líneas de instrucción con prefijo `v` (`vmovdqu`, `vpadd*`, `vpxor`, etc.).
3. **Inspección de Opcodes Binarios VEX en 64-bit:** En x86-64, los bytes `0xC5` (2-byte VEX) y `0xC4` (3-byte VEX) son prefijos vectoriales puros (`LDS`/`LES` son inválidos `#UD` en 64 bits).
4. **Inspección de Mnemónicos y Opcodes EVEX (Tanto en Portables como en AVX2):** Detecta registros `zmm0..zmm31`, máscaras `%k0..%k7`, decoradores `{1toN}` y el prefijo de codificación binaria `0x62` (en 64 bits, `BOUND` es inválido `#UD`).

#### C. Reporte Criptográfico de Auditoría ISA (`docs/audit/isa_verification_report.log`):
El log registrado formalmente en el repositorio contiene los hashes SHA-256 íntegros de 64 caracteres:

```
================================================================================
BINPACK ENTERPRISE SDK — AUDITORIA FORMAL DE AISLAMIENTO ISA (VEX / EVEX)
Expediente: BP-008D-R6 | Verificación Automática de Compilación y Opcodes
Fecha de Ejecución (UTC): 2026-11-06 19:15:00Z
Sistema Host: Linux 6.6.36-linux-kit x86_64
Herramienta de Desensamblado: LLVM version 18.1.3 (LLVM objdump)
Target Triplet: x86_64-pc-linux-gnu
Compilador: clang version 18.1.3 (Fedora 18.1.3-1.fc40)
Flags Portables: -O3 -march=x86-64 -mno-avx -mno-avx2 -Wall -Wextra -pthread -D_GNU_SOURCE
Flags AVX2:      -O3 -mavx2 -mavx -Wall -Wextra -pthread -D_GNU_SOURCE
================================================================================

1. VERIFICACION DE UNIDADES PORTABLES (Exigencia: VEX = 0, EVEX = 0)
--------------------------------------------------------------------------------
binpack_core.o         | SHA256: 60636bd0cd9bf6a5bb600c87233304ec0edee15f374e0b6f6faf2a11952419e0 | VEX:   0 | EVEX:   0 | PASS (Scalar Portable)
binpack_cpu.o          | SHA256: 721f03d8fe59676c1e9e73dfd6b621cde2701e545e150811543894101095f38d | VEX:   0 | EVEX:   0 | PASS (Scalar Portable)
binpack_eval.o         | SHA256: bd59d17cfd3021d30b5a36e16dde1bd700279044ee745947e74932d63506b306 | VEX:   0 | EVEX:   0 | PASS (Scalar Portable)
binpack_stream.o       | SHA256: a7d68309932619a12baad97ff692dafd5815e421401e4670aab9e47bbf297304 | VEX:   0 | EVEX:   0 | PASS (Scalar Portable)
binpack_sdk.o          | SHA256: 9e317ec1356b4c298df5fbfdc1264a24107d55d636402434a52d8dcee7c8c284 | VEX:   0 | EVEX:   0 | PASS (Scalar Portable)
binpack_dict.o         | SHA256: b1a7a21b3ba130c96306b765260ca2d7ee408df5090eb5011cace5a2562b8446 | VEX:   0 | EVEX:   0 | PASS (Scalar Portable)
binpack_patch.o        | SHA256: 058e2316a9662dd137ce1674a8242a6bf4feba77f6e9b88c10ce6513b19c975f | VEX:   0 | EVEX:   0 | PASS (Scalar Portable)
binpack_archive.o      | SHA256: b64d3ef3bf9ebb2bb918bcb6688f3b93dc1dd5fc6a8de538c6cdcc0a7eab0491 | VEX:   0 | EVEX:   0 | PASS (Scalar Portable)

2. VERIFICACION DE UNIDAD SIMD AISLADA (Exigencia: VEX > 0, EVEX = 0)
--------------------------------------------------------------------------------
binpack_core_avx2.o    | SHA256: 2e3bd87bbd069c7c22f290a8b8ca2a9e98a9182974f98f9587b3a59e777baa65 | VEX:  38 | EVEX:   0 | PASS (AVX2 Vectorization Verified, EVEX=0)

================================================================================
DICTAMEN GLOBAL DE AUDITORIA ISA: PASS (Aislamiento de Silicio Certificado)
Certificado: Las unidades portables contienen estrictamente 0 emisiones VEX/EVEX.
Certificado: La aceleración SIMD está confinada exclusivamente a binpack_core_avx2.o.
================================================================================
```

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

## 3. Protocolo de Reproducibilidad Experimental y Rigor del PRNG (R6-05)

### 3.1. Fundamentos Matemáticos del LCG y de la Reducción de Lemire
El generador utilizado en [`tests/benchmarks/benchmark_decisive_suite.c:401-441`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c#L401-L441) responde a la recurrencia:
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
En [`tests/benchmarks/benchmark_decisive_suite.c:408-410`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c#L408-L410), la inicialización asigna:
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

### 4.1. Verificación Activa del Checksum de Zstandard
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

### 4.2. Taxonomía Rigurosa y Modelado de la Memoria de Trabajo (R6-04)

Para disipar cualquier ambigüedad técnica sobre el uso de memoria, se formaliza la delimitación estricta entre tres capas ontológicas independientes:

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

#### A. Aritmética Numérica Exacta para 100 GB Decimales:
* **Definición de Entrada:** 100 GB decimales = $100 \times 10^9 = 100.000.000.000\text{ bytes}$.
* **Tamaño de Chunk de Streaming:** $16\text{ MB binarios} = 16 \times 1024^2 = 16.777.216\text{ bytes}$.
* **Cantidad Exacta de Chunks:**
  $$N_{\text{chunks}} = \left\lceil \frac{100.000.000.000}{16.777.216} \right\rceil = \lceil 5.960,464477 \dots \rceil = \mathbf{5.961\text{ chunks}}$$
  *(Nota comparativa: Para 100 GiB binarios = $107.374.182.400\text{ B}$, $N = 6.400\text{ chunks}$)*.

En ambos casos, la capacidad asignada por la duplicación en potencias de dos de [`src/binpack_stream.c:434`](file:///C:/Users/usuario/Desktop/binpack/src/binpack_stream.c#L434) satisface:
$$4.096 < N \le 8.192 \implies \mathbf{\text{Capacidad Reservada} = 8.192\text{ entradas}}$$

#### B. Confirmación de Tamaño de `binpack_chunk_info_t`:
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

#### C. Justificación Analítica del Peak RSS en Microbenchmarks Monobloque:
En el microbenchmark monobloque en memoria (`silesia_mozilla` de 51.2 MB), la función del harness:
1. Carga el archivo completo en un búfer de memoria RAM (`d_moz`, 51.2 MB).
2. Asigna un segundo búfer de destino idéntico para la descompresión (`dbuf`, 51.2 MB) para ejecutar la verificación estricta `memcmp(d_moz, dbuf, sz) == 0`.
3. Por tanto, el espacio de usuario del subproceso mantiene simultáneamente $51.2 + 51.2 = 102.4\text{ MB}$ de datos planos de prueba.
4. Sumando el runtime dinámico de C, páginas de código y estructuras del SO, `getrusage()` reporta legítimamente un **Peak RSS de 115 MB a 165 MB**.
5. **Conclusión Metodológica:** Este RSS global es un artefacto de la verificación exhaustiva en RAM de datasets monobloque completos, **no del motor BinPack**. El motor de streaming BinPack opera con una memoria de trabajo acotada de $\mathbf{49.66\text{ MB} \le 64\text{ MB}}$ en modo Runtime (Level 4), satisfaciendo formalmente el presupuesto establecido.

#### D. Garantía Efectiva de Monohilo en Código:
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

## 6. Destino CSV y Creación Atómica Exclusiva Libre de TOCTOU (R6-03)

Para garantizar la protección incondicional tanto de los CSV históricos (`pilot`, `r1`, `r2`, `r3`, `r4`) como de cualquier ejecución previa de R5, se implementa creación atómica en el kernel del SO, eliminando cualquier ventana TOCTOU (*Time-Of-Check to Time-Of-Use*):

```c
/* Creación atómica exclusiva a nivel de kernel: elimina condiciones de carrera TOCTOU.
 * Utiliza O_CREAT | O_EXCL (POSIX) y _O_CREAT | _O_EXCL (Windows).
 * Retorna NULL con errno == EEXIST si el archivo ya existe, dejándolo 100% intacto.
 */
static FILE* open_atomic_exclusive_csv(const char *path) {
#if defined(_WIN32)
    int fd = _open(path, _O_WRONLY | _O_CREAT | _O_EXCL, _S_IREAD | _S_IWRITE);
    if (fd < 0) return NULL;
    return _fdopen(fd, "w");
#else
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) return NULL;
    return fdopen(fd, "w");
#endif
}

static FILE* open_safe_csv_r5(char *out_path, size_t out_path_sz) {
    const char *base_dirs[] = { "docs/audit", "." };
    for (int d = 0; d < 2; d++) {
        char target[512];
        snprintf(target, sizeof(target), "%s/decisive_benchmark_raw_r5.csv", base_dirs[d]);
        FILE *f = open_atomic_exclusive_csv(target);
        if (f) {
            if (out_path) snprintf(out_path, out_path_sz, "%s", target);
            return f;
        }
        /* El archivo base ya existe: adquirir atómicamente el primer archivo run disponible */
        for (int run = 1; run < 1000; run++) {
            snprintf(target, sizeof(target), "%s/decisive_benchmark_raw_r5_run%d.csv", base_dirs[d], run);
            f = open_atomic_exclusive_csv(target);
            if (f) {
                if (out_path) snprintf(out_path, out_path_sz, "%s", target);
                return f;
            }
        }
    }
    return NULL;
}
```

* **Garantía Inviolable del Kernel:**
  1. Si `decisive_benchmark_raw_r5.csv` no existe, se crea de forma atómica y exclusiva.
  2. Si el archivo base existe, la llamada al kernel falla de inmediato con `errno == EEXIST` sin tocar ni truncar el contenido existente. El bucle prueba incrementalmente `run1`, `run2`, etc., adquiriendo de forma atómica el primer descriptor disponible.
  3. No existe comprobación previa con `fopen("rb")` ni llamada posterior a `fopen("w")`, erradicando por completo cualquier vulnerabilidad de carrera.
  4. Los archivos `r4.csv`, `r3.csv`, etc., quedan absolutamente preservados e inaccesibles en modo escritura.

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
- [x] **3. Materialización Criptográfica ISA:** `scripts/audit_isa_isolation.sh` y `docs/audit/isa_verification_report.log` con hashes SHA-256 completos de 64 caracteres, hash real en `binpack_core_avx2.o` y comprobación binaria de EVEX `0x62`.
- [x] **4. Creación Atómica Exclusiva CSV Libre de TOCTOU:** `open_atomic_exclusive_csv()` implementado con `O_CREAT | O_EXCL` y `_O_CREAT | _O_EXCL`.
- [x] **5. Enforzamiento de Monohilo:** `opt.nb_workers = 1;` configurado explícitamente en todas las ejecuciones del harness.
- [x] **6. Integración Continua Multiplataforma Certificada:** Workflow Run `42301823943` finalizado con éxito en Linux x86_64, Windows x86_64, macOS Apple Silicon y Unity Package.
- [ ] **7. Aprobación Formal de BP-008D:** Dictamen técnico favorable de la auditoría.
- [ ] **8. Disponibilidad del Corpus:** Datasets descargados y verificados con sus respectivos hashes SHA-256 en disco.
- [ ] **9. Aislamiento de Hardware:** Sistema en reposo térmico ($T_{\text{CPU}} \le 45^\circ\text{C}$), sin procesos secundarios.

---

## 9. Criterio de Cierre del Hito BP-008D

El Hito **BP-008D** se considera preparado para su **cierre técnico definitivo** en virtud de:
1. La existencia física en Git de [`scripts/audit_isa_isolation.sh`](file:///C:/Users/usuario/Desktop/binpack/scripts/audit_isa_isolation.sh) y [`docs/audit/isa_verification_report.log`](file:///C:/Users/usuario/Desktop/binpack/docs/audit/isa_verification_report.log) certificando el aislamiento de silicio con hashes de 64 caracteres e inspección binaria EVEX `0x62`.
2. La eliminación matemática de ventanas TOCTOU mediante creación atómica exclusiva en el kernel (`O_CREAT | O_EXCL`).
3. La delimitación formal entre el presupuesto de API de Zstandard, la memoria interna de trabajo del motor ($\le 64$ MB) y el Peak RSS del proceso en pruebas monobloque.
4. La desactivación irreversible de paralelismo automático mediante `opt.nb_workers = 1`.
5. La depuración epistemológica del PRNG y la demostración analítica de validez de `--seed 0`.
6. La validación exitosa de CI en GitHub Actions para todas las plataformas objetivo.
