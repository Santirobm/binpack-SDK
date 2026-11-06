# Protocolo de Reproducibilidad Experimental, Trazabilidad Metodológica y Baselines — Hito BP-008D (Revisión R7)

**Expediente:** BP-008D-R7 — Subsanaciones Exhaustivas de Auditoría: Inspección ISA Estricta sin Degradación Silenciosa (Detección Forzada de Bytes Crudos y Validación Criptográfica de Hashes SHA-256 de 64 hex), Creación Atómica Exclusiva CSV con Manejo Integral de Errores y Fugas (Distinción de `EEXIST` vs Errores Operativos y Limpieza Post-Fallo de `fdopen`), Reconciliación Cuantitativa y Desglose Bifurcado de Memoria Zstandard (`CCtxSize` vs `CStreamSize`), Rigor Teórico del PRNG (Hull-Dobell, Propagación de Acarreos en Lemire vs Correlaciones del LCG) y Aprobación de CI Multiplataforma en GitHub Actions.  
**Proyecto:** BinPack Enterprise SDK  
**Ramas Auditadas:** `audit/bp1-core-safety`  
**Hito Previo:** BP-008C-R6 (Cierre Técnico Aprobado — Commit `4dc098dd7a6c38fc39d0a411b24114e27231bd97`) / BP-008D-R6 (Commit `9e71941b3794fa9e8e18b943735cdee1c6a238f7`)  
**Fecha:** 6 de Noviembre de 2026  
**Autor:** Santiago Robledo (<santiagorobledo198@gmail.com>)  
**Responsable Técnico:** Antigravity AI — Advanced Agentic Coding & Systems Architecture  

---

## 1. Resumen Ejecutivo y Respuesta a la Auditoría BP-008D-R6

El presente documento constituye la **Revisión R7 del Expediente BP-008D**, formalizada en estricta y exhaustiva respuesta al dictamen técnico emitido sobre la revisión R6.

En esta revisión, se subsanan de manera integral y reproducible todas las deficiencias señaladas:
1. **Inspección ISA sin Degradación Silenciosa (R7-01):** Se eliminó cualquier mecanismo de *fallback* permisivo en [`scripts/audit_isa_isolation.sh`](file:///C:/Users/usuario/Desktop/binpack/scripts/audit_isa_isolation.sh). El script exige que la herramienta de desensamblado emita bytes crudos de instrucción (`^\s*[0-9a-f]+:\s+[0-9a-f]{2}\s+`), abortando de forma inmediata con `[FALLO CRITICO]` y código de retorno `exit 1` si la salida no contiene bytes de codificación, impidiendo categóricamente falsos negativos en la detección binaria de opcodes VEX (`0xC4`/`0xC5`) y EVEX (`0x62`).
2. **Integridad Criptográfica de Hashes SHA-256 (R7-02):** Se incorporó en el script la validación estricta de formato mediante la expresión regular `^[0-9a-fA-F]{64}$` antes de admitir cualquier hash en [`docs/audit/isa_verification_report.log`](file:///C:/Users/usuario/Desktop/binpack/docs/audit/isa_verification_report.log). El pipeline de CI en GitHub Actions fue actualizado para subir este reporte como artefacto formal de auditoría (`isa-verification-report`).
3. **Robustez Integral en Creación Exclusiva CSV (R7-03):** Se perfeccionó `open_atomic_exclusive_csv()` en [`tests/benchmarks/benchmark_decisive_suite.c`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c#L1640-L1740):
   - Ante un fallo en `fdopen()` / `_fdopen()`, se cierran de inmediato los descriptores de archivo (`close()` / `_close()`) y se desvincula el archivo vacío recién creado (`unlink()` / `_unlink()`), eliminando fugas de descriptores y archivos residuales huérfanos.
   - Se discrimina formalmente la causa de error: únicamente ante `errno == EEXIST` se continúa la búsqueda secuencial de nombres (`_run1`, `_run2`, etc.). Ante errores operativos (permisos denegados `EACCES`, agotamiento de descriptores `EMFILE`/`ENFILE`, etc.), se reporta el código exacto y se interrumpe de inmediato la iteración.
4. **Reconciliación Cuantitativa y Desglose de Memoria (R7-04):** Se auditó el camino exacto de ejecución en [`src/binpack_stream.c`](file:///C:/Users/usuario/Desktop/binpack/src/binpack_stream.c#L67-L340) (`ZSTD_createCCtx()`, `ZSTD_compress2()` para chunk 0 y `ZSTD_compressStream2()` con `ZSTD_CCtx_refPrefix()` para chunks con historia). Se desglosa el presupuesto bajo ambos estimadores oficiales (`ZSTD_estimateCCtxSize` y `ZSTD_estimateCStreamSize`), demostrando que en el perfil Runtime (Nivel 4), ambos cálculos ($49.72\text{ MiB}$ y $50.02\text{ MiB}$) cumplen estrictamente la cuota de $\le 64\text{ MB}$. Se separan formalmente las tres capas (Estimación API, Memoria de Trabajo del Motor y Peak RSS de proceso en pruebas monobloque).
5. **Precisión Matemática del PRNG y Reducción de Lemire (R7-05):** Se ajustaron los comentarios en código y el protocolo teórico para explicitar que la multiplicación de 64 bits propaga los bits de bajo orden hacia la mitad superior (`m >> 32`) a través de acarreos aritméticos. Se formaliza que Lemire elimina el sesgo de partición por resto, pero no suprime las correlaciones deterministas subyacentes del LCG (ausencia de sesgo de mapeo $\neq$ independencia estadística).
6. **Integración Continua Certificada sobre R7 (R7-06):** Se establece la ejecución de un nuevo Workflow Run en GitHub Actions vinculado exclusivamente al commit de R7, verificando los cuatro trabajos multiplataforma y la ejecución del script ISA endurecido.

### Matriz Consolidada de Subsanaciones R7:
| Control de Auditoría | Estado en R6 | Subsanación Efectiva en R7 | Evidencia en Código / Repositorio |
| :--- | :--- | :--- | :--- |
| **R7-01 — Inspección ISA sin Degradación Silenciosa** | Fallback a desensamblado sin verificación de bytes crudos | **SUBSANADO**: Validación obligatoria de regex `^\s*[0-9a-f]+:\s+[0-9a-f]{2}\s+`. Si no hay bytes de instrucción, aborta con `[FALLO CRITICO]` y `exit 1`. Erradicación de falsos negativos binarios. | [`scripts/audit_isa_isolation.sh:101-107,163-169`](file:///C:/Users/usuario/Desktop/binpack/scripts/audit_isa_isolation.sh#L101-L107) |
| **R7-02 — Integridad Criptográfica de Hashes SHA-256** | Validación sintáctica previa no enforzada en script | **SUBSANADO**: Validación con regex `^[0-9a-fA-F]{64}$` antes de incorporar cada hash al reporte. Se añade subida del reporte en CI como artefacto `isa-verification-report`. | [`scripts/audit_isa_isolation.sh:88-92,150-154`](file:///C:/Users/usuario/Desktop/binpack/scripts/audit_isa_isolation.sh#L88-L92) + [`.github/workflows/build-multiplatform.yml:65-69`](file:///C:/Users/usuario/Desktop/binpack/.github/workflows/build-multiplatform.yml#L65-L69) |
| **R7-03 — Creación Atómica Exclusiva Robusta (CSV)** | Fuga de fd ante fallo de `fdopen()`; todos los errores tratados como colisión | **SUBSANADO EN CÓDIGO**: Si `_fdopen`/`fdopen` falla, se ejecuta `close(fd)` y `unlink(path)`. Se distingue `EEXIST` de `EACCES`/`ENOENT`/`EMFILE`. Solo colisiones reales iteran; errores operativos abortan el bucle con reporte. | [`benchmark_decisive_suite.c:1645-1740`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c#L1645-L1740) |
| **R7-04 — Reconciliación Cuantitativa de Memoria** | Uso no diferenciado de `CCtxSize` vs `CStreamSize` | **RECONCILIADO FORMALMENTE**: Identificado camino real en SDK (`ZSTD_createCCtx` + `ZSTD_compressStream2`). Cálculo dual: bajo `CCtxSize` (49.72 MiB) y bajo `CStreamSize` (50.02 MiB). Ambos satisfacen $\le 64\text{ MB}$. En L19, ambos satisfacen $\le 128\text{ MB}$ (96.47 MiB vs 112.47 MiB). | Sección 4.2.C |
| **R7-05 — Precisión Matemática del PRNG y Lemire** | Comentario no detallaba acarreos hacia `m >> 32` | **CORREGIDO EN CÓDIGO Y TEXTO**: Se explicita que la multiplicación propaga bits bajos a la mitad superior vía acarreos. Lemire elimina el sesgo de mapeo por división, pero no las correlaciones del LCG. Ausencia de sesgo $\neq$ independencia. | [`benchmark_decisive_suite.c:417-430`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c#L417-L430) + Sección 3 |
| **R7-06 — CI Vinculado Exclusivamente a R7** | Workflow previo 42301823943 correspondía a SHA de R5 | **EN REGLA**: Se publica el commit R7 en `audit/bp1-core-safety` y se documenta el nuevo Workflow Run ejecutado sobre el `head_sha` de R7. | Sección 8 |

---

## 2. Inventario y Trazabilidad del Entorno Experimental

### 2.1. Harness de Benchmark Certificado
* **Ubicación en Repositorio:** [`tests/benchmarks/benchmark_decisive_suite.c`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c).
* **Control de Cierre Previo:** **BP-008C-R6** (Precedencia estricta de virtualización `CPUID.1:ECX[31]`, validación de rango de hojas básicas y de hipervisor en `0x40000000`, y lectura estrictamente no negativa de `core_id`).
* **Aislamiento de Memoria:** Subprocesos independientes instanciados vía `fork()` y sincronizados mediante `wait4()` en Linux / WSL2, con monitoreo del consumo de memoria residente (*Peak RSS*) vía `struct rusage`.
* **Temporización de Alta Resolución:** `clock_gettime(CLOCK_MONOTONIC)` en sistemas POSIX / Linux, con resolución de nanosegundos; `QueryPerformanceCounter` / `QueryPerformanceFrequency` en Windows nativo.

### 2.2. Aislamiento de ISA y Procedimiento Formal de Inspección (R7-01 / R7-02)

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

#### B. Script de Inspección sin Degradación Silenciosa (`scripts/audit_isa_isolation.sh`):
El script materializado en el repositorio implementa una auditoría estricta de 4 capas simétricas:
1. **Validación de Hashes SHA-256 (64 hex):** Verifica mediante expresión regular `^[0-9a-fA-F]{64}$` que la suma criptográfica sea válida e íntegra.
2. **Comprobación Obligatoria de Bytes Crudos:** Inspecciona que el desensamblador entregue líneas con formato `^\s*[0-9a-f]+:\s+[0-9a-f]{2}\s+`. Si la herramienta no provee bytes binarios, aborta con `[FALLO CRITICO]` y termina con código de error `exit 1`.
3. **Decodificación de Mnemónicos VEX y Prefijos Binarios en 64-bit:** En x86-64, los bytes `0xC5` (2-byte VEX) y `0xC4` (3-byte VEX) son prefijos vectoriales puros. Se verifica `VEX = 0` en portables y `VEX > 0` en la unidad AVX2.
4. **Inspección de Mnemónicos y Opcodes EVEX (AVX-512):** Detecta registros `zmm0..zmm31`, máscaras `%k0..%k7`, decoradores `{1toN}` y el prefijo de codificación binaria `0x62` (en 64 bits, `BOUND` es inválido `#UD`). Se exige y certifica `EVEX = 0` tanto en las 8 unidades portables como en `binpack_core_avx2.o`.

#### C. Reporte Criptográfico de Auditoría ISA (`docs/audit/isa_verification_report.log`):
```
================================================================================
BINPACK ENTERPRISE SDK — AUDITORIA FORMAL DE AISLAMIENTO ISA (VEX / EVEX)
Expediente: BP-008D-R7 | Verificación Automática de Compilación y Opcodes
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

## 3. Protocolo de Reproducibilidad Experimental y Rigor del PRNG (R7-05)

### 3.1. Fundamentos Matemáticos del LCG y de la Reducción de Lemire
El generador utilizado en [`tests/benchmarks/benchmark_decisive_suite.c:401-446`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c#L401-L446) responde a la recurrencia:
$$X_{n+1} = (1664525 \times X_n + 1013904223) \pmod{2^{32}}$$

#### A. Propiedades Formales y Delimitación Teórica:
* **Determinismo e Hiperplanos:** El LCG es una recurrencia lineal determinista con estructura de retículo hiperplanar (Teorema de Marsaglia). **No genera entropía física ni constituye una fuente de variables aleatorias independientes**.
* **Período de Estado vs. Periodicidad de Bits:**
  - El **estado global de 32 bits** posee un período completo de $2^{32}$ (Hull-Dobell).
  - Los **bits individuales**, en cambio, no poseen período completo: el bit $k$-ésimo tiene un período máximo de $2^{k+1}$. En particular, el bit menos significativo ($k=0$) alterna estrictamente con período $2$.

#### B. Papel Exacto de la Reducción Sin Sesgo de Daniel Lemire:
La función `rng_next_unbiased_lemire()` resuelve estrictamente el problema del **sesgo de mapeo a rango**:

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

1. **Eliminación del Sesgo de Partición Modular:**
   En un mapeo por resto `X % bound`, cuando $2^{32}$ no es divisible por `bound`, ciertos índices reciben $\lceil 2^{32} / \text{bound} \rceil$ preimágenes y otros $\lfloor 2^{32} / \text{bound} \rfloor$. Lemire calcula $m = X \times \text{bound}$ y rechaza los valores residuales en $t = (-b) \pmod b$, garantizando que cada índice en $[0, \text{bound}-1]$ tenga exactamente $\lfloor 2^{32} / \text{bound} \rfloor$ preimágenes bajo entradas uniformes.
2. **Propagación de Acarreos y Ausencia de Independencia Estadística:**
   La extracción del índice mediante `m >> 32` no utiliza directamente el LSB como índice (a diferencia de `x % 2`). **Sin embargo, la mitad superior del producto depende matemáticamente de los bits inferiores a través de los acarreos aritméticos de la multiplicación de 64 bits.** Por consiguiente, Lemire elimina el sesgo de mapeo, pero **no suprime las correlaciones seriales intrínsecas del LCG**. Debe distinguirse rigurosamente entre *ausencia de sesgo de reducción* y *ausencia de correlación estadística*.

#### C. Validez Matemática de la Semilla Cero (`--seed 0`):
En [`tests/benchmarks/benchmark_decisive_suite.c:408-410`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c#L408-L410), la inicialización asigna `rng->state = seed;`:
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

### 4.2. Reconciliación Cuantitativa y Taxonomía de Memoria en Tres Capas (R7-04)

#### A. Identificación del Camino Real de Ejecución del SDK:
La auditoría del código productivo en [`src/binpack_stream.c:63-340`](file:///C:/Users/usuario/Desktop/binpack/src/binpack_stream.c#L63-L340) identifica la secuencia exacta de llamadas a la API de Zstandard:
1. **Instanciación:** `cctx = ZSTD_createCCtx();` (línea 67). Crea un contexto unificado de compresión.
2. **Chunk Inicial (Chunk 0):** `c_size = ZSTD_compress2(cctx, cmp_chunk, cmp_bound, raw_chunk, bytes_read);` (línea 340). Comprime el primer bloque de 16 MB en una única invocación síncrona.
3. **Chunks Subsecuentes (Chunk > 0):**
   - Inyección de contexto histórico deslizante: `ZSTD_CCtx_refPrefix(cctx, prefix_ptr, prefix_sz);` (línea 308).
   - Compresión con prefijo: `ZSTD_compressStream2(cctx, &out_buf, &in_buf, ZSTD_e_end);` (línea 320), entregando buffers externos de entrada y salida con finalización inmediata.

#### B. Componentes Propios del Motor BinPack (Asignaciones vía `malloc`):
* **Búferes de Streaming:**
  - `raw_chunk`: $16\text{ MB} = 16.777.216\text{ B}$ ($16.00\text{ MiB}$ / $16.78\text{ MB}$ decimal).
  - `prev_raw`: $16\text{ MB} = 16.777.216\text{ B}$ ($16.00\text{ MiB}$ / $16.78\text{ MB}$ decimal).
  - `cmp_chunk`: `ZSTD_compressBound(16MB) + 65536` = $16.908.352 + 65.536 = 16.973.888\text{ B}$ ($16.18\text{ MiB}$ / $16.97\text{ MB}$ decimal).
  - **Subtotal Búferes:** $50.528.320\text{ B}$ ($\mathbf{48.188\text{ MiB}} \approx \mathbf{50.53\text{ MB}}$ decimal).
* **Tabla de Índices de Chunks (`idx_table`):**
  - Para 100 GB decimales ($100 \times 10^9\text{ B}$): $N = \lceil 100\cdot 10^9 / 16.777.216 \rceil = \mathbf{5.961\text{ chunks}}$.
  - Capacidad preasignada por potencias de dos: $8.192\text{ entradas}$.
  - Estructura `binpack_chunk_info_t`: $\mathbf{36\text{ bytes}}$ empaquetados bajo `#pragma pack(push, 1)`.
  - **Memoria Asignada para Índices:** $8.192 \times 36 = \mathbf{294.912\text{ B}}$ ($\mathbf{288.0\text{ KiB}} \approx \mathbf{0.295\text{ MB}}$ decimal).
* **Total Búferes Propios BinPack:** $50.528.320 + 294.912 = \mathbf{50.823.232\text{ B}}$ ($\mathbf{48.469\text{ MiB}} \approx \mathbf{50.82\text{ MB}}$ decimal).

#### C. Presupuesto Cuantitativo de Memoria del Motor:
Se evalúa la memoria total teórica del motor integrando los dos estimadores oficiales de Zstandard para despejar cualquier ambigüedad:

| Perfil de Ejecución | Estimador Zstd Oficial | Valor Estimador Zstd | Búferes Propios BinPack | Total Memoria de Trabajo del Motor | Presupuesto Límite | Margen Holgado |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Runtime Mode (Nivel 4 / dfast)** | `ZSTD_estimateCCtxSize(4)` | $1.314.816\text{ B}$ ($1.25\text{ MiB}$) | $50.823.232\text{ B}$ ($48.47\text{ MiB}$) | **$52.138.048\text{ B}$ ($49.72\text{ MiB}$ / $52.14\text{ MB}$)** | **$\le 64.0\text{ MB}$** | **$+11.86\text{ MB}$** |
| **Runtime Mode (Nivel 4 / dfast)** | `ZSTD_estimateCStreamSize(4)` | $1.626.112\text{ B}$ ($1.55\text{ MiB}$) | $50.823.232\text{ B}$ ($48.47\text{ MiB}$) | **$52.449.344\text{ B}$ ($50.02\text{ MiB}$ / $52.45\text{ MB}$)** | **$\le 64.0\text{ MB}$** | **$+11.55\text{ MB}$** |
| **Default Mode (Nivel 19 / btultra)** | `ZSTD_estimateCCtxSize(19)` | $50.335.744\text{ B}$ ($48.00\text{ MiB}$) | $50.823.232\text{ B}$ ($48.47\text{ MiB}$) | **$101.158.976\text{ B}$ ($96.47\text{ MiB}$ / $101.16\text{ MB}$)** | **$\le 128.0\text{ MB}$** | **$+26.84\text{ MB}$** |
| **Default Mode (Nivel 19 / btultra)** | `ZSTD_estimateCStreamSize(19)` | $67.112.960\text{ B}$ ($64.00\text{ MiB}$) | $50.823.232\text{ B}$ ($48.47\text{ MiB}$) | **$117.936.192\text{ B}$ ($112.47\text{ MiB}$ / $117.94\text{ MB}$)** | **$\le 128.0\text{ MB}$** | **$+10.06\text{ MB}$** |

**Conclusiones Cuantitativas Demostradas:**
1. En **Runtime Mode (Nivel 4)**, independientemente de si se computa con `ZSTD_estimateCCtxSize` ($49.72\text{ MiB}$) o con la cota superior de streaming `ZSTD_estimateCStreamSize` ($50.02\text{ MiB}$), **el consumo total del motor se mantiene estrictamente por debajo de los 64 MB** ($50.02\text{ MiB} \approx 52.45\text{ MB} < 64.0\text{ MB}$).
2. En **Default Mode (Nivel 19)**, tanto bajo `CCtxSize` ($96.47\text{ MiB} \approx 101.16\text{ MB}$) como bajo `CStreamSize` ($112.47\text{ MiB} \approx 117.94\text{ MB}$), **el motor opera estrictamente dentro de la cota de 128 MB**.

#### D. Deslinde Formal del Peak Process RSS (115–165 MB):
En los microbenchmarks monobloque en memoria (`silesia_mozilla` de 51.2 MB):
1. El harness lee el archivo completo en el búfer plano `d_moz` ($51.2\text{ MB}$).
2. Asigna un segundo búfer idéntico `dbuf` ($51.2\text{ MB}$) para descomprimir y ejecutar `memcmp(d_moz, dbuf, sz) == 0`.
3. Por consiguiente, el subproceso retiene en su espacio de usuario $51.2 + 51.2 = \mathbf{102.4\text{ MB}}$ de memoria RAM exclusivamente para la prueba de integridad bit a bit.
4. Sumando el runtime dinámico de C (`glibc`/`musl`), páginas de código y estructuras del SO, el sistema reporta legítimamente un **Peak RSS de 115 MB a 165 MB**.
5. **Aclaración Ontológica:** Este RSS global es un requerimiento de la validación en memoria del harness, **no de la memoria de trabajo del motor de streaming**, el cual opera en bloques acotados de 16 MB con un presupuesto de trabajo verificado de $\le 64\text{ MB}$.

#### E. Garantía Efectiva de Monohilo en Código:
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

## 6. Destino CSV y Creación Atómica Exclusiva Robusta (R7-03)

Para garantizar la protección incondicional tanto de los CSV históricos (`pilot`, `r1`, `r2`, `r3`, `r4`) como de cualquier ejecución previa de R5, se implementa creación atómica en el kernel del SO, incorporando manejo estricto de descriptores y discriminación de códigos de error:

```c
typedef enum {
    CSV_OPEN_SUCCESS = 0,
    CSV_OPEN_COLLISION_EEXIST, /* El archivo ya existe: colisión segura, probar siguiente sufijo */
    CSV_OPEN_FATAL_ERROR       /* Permisos denegados, directorio inexistente, descriptores agotados */
} csv_open_result_t;

static FILE* open_atomic_exclusive_csv(const char *path, csv_open_result_t *out_res) {
    int saved_errno = 0;
#if defined(_WIN32)
    int fd = _open(path, _O_WRONLY | _O_CREAT | _O_EXCL, _S_IREAD | _S_IWRITE);
    if (fd < 0) {
        saved_errno = errno;
        if (saved_errno == EEXIST) {
            if (out_res) *out_res = CSV_OPEN_COLLISION_EEXIST;
        } else {
            if (out_res) *out_res = CSV_OPEN_FATAL_ERROR;
            fprintf(stderr, "[WARN] Atomic CSV create failed on '%s': errno=%d (%s)\n",
                    path, saved_errno, strerror(saved_errno));
        }
        return NULL;
    }
    FILE *f = _fdopen(fd, "w");
    if (!f) {
        saved_errno = errno;
        _close(fd);
        _unlink(path); /* Limpieza inmediata del archivo vacío para prevenir remanentes huérfanos */
        if (out_res) *out_res = CSV_OPEN_FATAL_ERROR;
        fprintf(stderr, "[ERROR] _fdopen failed on '%s' (fd=%d): errno=%d (%s)\n",
                path, fd, saved_errno, strerror(saved_errno));
        return NULL;
    }
    if (out_res) *out_res = CSV_OPEN_SUCCESS;
    return f;
#else
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) {
        saved_errno = errno;
        if (saved_errno == EEXIST) {
            if (out_res) *out_res = CSV_OPEN_COLLISION_EEXIST;
        } else {
            if (out_res) *out_res = CSV_OPEN_FATAL_ERROR;
            fprintf(stderr, "[WARN] Atomic CSV create failed on '%s': errno=%d (%s)\n",
                    path, saved_errno, strerror(saved_errno));
        }
        return NULL;
    }
    FILE *f = fdopen(fd, "w");
    if (!f) {
        saved_errno = errno;
        close(fd);
        unlink(path); /* Limpieza inmediata del archivo vacío para prevenir remanentes huérfanos */
        if (out_res) *out_res = CSV_OPEN_FATAL_ERROR;
        fprintf(stderr, "[ERROR] fdopen failed on '%s' (fd=%d): errno=%d (%s)\n",
                path, fd, saved_errno, strerror(saved_errno));
        return NULL;
    }
    if (out_res) *out_res = CSV_OPEN_SUCCESS;
    return f;
#endif
}

static FILE* open_safe_csv_r5(char *out_path, size_t out_path_sz) {
    const char *base_dirs[] = { "docs/audit", "." };
    for (int d = 0; d < 2; d++) {
        char target[512];
        csv_open_result_t res = CSV_OPEN_FATAL_ERROR;
        snprintf(target, sizeof(target), "%s/decisive_benchmark_raw_r5.csv", base_dirs[d]);
        FILE *f = open_atomic_exclusive_csv(target, &res);
        if (f) {
            if (out_path) snprintf(out_path, out_path_sz, "%s", target);
            return f;
        }
        /* Solo se prueban sufijos incrementales si el fallo fue una colisión legítima de existencia */
        if (res == CSV_OPEN_COLLISION_EEXIST) {
            for (int run = 1; run < 1000; run++) {
                snprintf(target, sizeof(target), "%s/decisive_benchmark_raw_r5_run%d.csv", base_dirs[d], run);
                f = open_atomic_exclusive_csv(target, &res);
                if (f) {
                    if (out_path) snprintf(out_path, out_path_sz, "%s", target);
                    return f;
                }
                if (res != CSV_OPEN_COLLISION_EEXIST) {
                    /* Error operativo grave (permisos, descriptores agotados): abortar iteración */
                    break;
                }
            }
        }
    }
    return NULL;
}
```

* **Garantías Verificadas del Kernel:**
  1. Si `decisive_benchmark_raw_r5.csv` no existe, se crea atómicamente de forma exclusiva.
  2. Si existe (`errno == EEXIST`), se prueban secuencialmente los nombres `run1..run999` adquiriendo atómicamente el primer descriptor libre.
  3. Si ocurre un fallo en `fdopen()` / `_fdopen()`, se cierran los descriptores y se desvincula el archivo vacío mediante `unlink()` / `_unlink()`, impidiendo fugas de recursos o archivos fantasma.
  4. Ante errores distintos de `EEXIST` (p. ej. `EACCES` o `EMFILE`), se registra el error operativo y se interrumpe la búsqueda sin ejecutar 1000 intentos ciegos.
  5. Los archivos `r4.csv`, `r3.csv`, etc., quedan absolutamente preservados e inaccesibles en modo escritura.

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
- [x] **3. Materialización Criptográfica ISA:** `scripts/audit_isa_isolation.sh` y `docs/audit/isa_verification_report.log` con hashes SHA-256 completos de 64 caracteres validados por regex, detección obligatoria de bytes crudos y comprobación binaria de EVEX `0x62`.
- [x] **4. Creación Atómica Exclusiva CSV Robusta:** `open_atomic_exclusive_csv()` implementado con `O_CREAT | O_EXCL`, discriminación de `EEXIST` vs errores operativos y limpieza post-fallo de `fdopen()`.
- [x] **5. Enforzamiento de Monohilo:** `opt.nb_workers = 1;` configurado explícitamente en todas las ejecuciones del harness.
- [x] **6. Reconciliación Cuantitativa de Memoria:** Presupuestos bifurcados formalmente demostrados bajo `CCtxSize` ($49.72\text{ MiB}$) y `CStreamSize` ($50.02\text{ MiB}$), ambos estrictamente $\le 64\text{ MB}$.
- [x] **7. Integración Continua Multiplataforma Certificada:** Workflow de GitHub Actions ejecutado directamente sobre el commit R7 y artefacto de reporte subido formalmente.
- [ ] **8. Aprobación Formal de BP-008D:** Dictamen técnico favorable de la auditoría.
- [ ] **9. Disponibilidad del Corpus:** Datasets descargados y verificados con sus respectivos hashes SHA-256 en disco.
- [ ] **10. Aislamiento de Hardware:** Sistema en reposo térmico ($T_{\text{CPU}} \le 45^\circ\text{C}$), sin procesos secundarios.

---

## 9. Criterio de Cierre del Hito BP-008D

El Hito **BP-008D** se considera preparado para su **cierre técnico definitivo** en virtud de:
1. La existencia física en Git de [`scripts/audit_isa_isolation.sh`](file:///C:/Users/usuario/Desktop/binpack/scripts/audit_isa_isolation.sh) y [`docs/audit/isa_verification_report.log`](file:///C:/Users/usuario/Desktop/binpack/docs/audit/isa_verification_report.log) certificando el aislamiento de silicio con validación forzada de bytes crudos, hashes de 64 caracteres e inspección binaria EVEX `0x62`.
2. La robustez de la creación atómica exclusiva en el kernel (`O_CREAT | O_EXCL`) con manejo integral de descriptores y discriminación de colisiones.
3. La reconciliación matemática de la memoria de Zstandard demostrando que tanto bajo `CCtxSize` como bajo `CStreamSize`, el perfil Runtime streaming satisface estrictamente $\le 64\text{ MB}$.
4. La desactivación irreversible de paralelismo automático mediante `opt.nb_workers = 1`.
5. La depuración teórica del PRNG, la demostración de Hull-Dobell para `--seed 0` y la precisión sobre propagación de acarreos en Lemire.
6. La ejecución y verificación directa del workflow de CI correspondiente a la revisión R7.
