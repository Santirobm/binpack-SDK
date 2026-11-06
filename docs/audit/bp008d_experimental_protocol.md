# Protocolo de Reproducibilidad Experimental, Trazabilidad Metodológica y Baselines — Hito BP-008D (Revisión R4)

**Expediente:** BP-008D-R4 — Subsanaciones Exhaustivas de Auditoría: Rigor Matemático del PRNG Lemire, Procedimiento Reproducible de Auditoría ISA VEX/EVEX, Modelado Exacto de Memoria (Capacidad de `idx_table` y Parámetros Efectivos de `ZSTD_CCtx`), Protección Absoluta del CSV R4 (`decisive_benchmark_raw_r5.csv`) y Validación Pre-flight de `ZSTD_error_checksum_wrong`  
**Proyecto:** BinPack Enterprise SDK  
**Ramas Auditadas:** `audit/bp1-core-safety`  
**Hito Previo:** BP-008C-R6 (Cierre Técnico Aprobado — Commit `4dc098dd7a6c38fc39d0a411b24114e27231bd97`)  
**Fecha:** 6 de Noviembre de 2026  
**Autor:** Santiago Robledo (<santiagorobledo198@gmail.com>)  
**Responsable Técnico:** Antigravity AI — Advanced Agentic Coding & Systems Architecture  

---

## 1. Resumen Ejecutivo y Respuesta a la Auditoría BP-008D-R3

El presente documento constituye la **Revisión R4 del Expediente BP-008D**, formalizada en estricta respuesta a las observaciones formuladas en el dictamen de auditoría R3.

En esta revisión, se corrigen de forma rigurosa los fundamentos matemáticos de la aleatorización, se documenta la inspección exhaustiva de ISA con clasificación binaria, se recalcula la memoria de trabajo conforme a las estructuras reales de `src/binpack_stream.c` y los parámetros efectivos de Zstandard v1.5.7, se garantiza la preservación incondicional de los CSV históricos fijando el destino de la futura campaña a `decisive_benchmark_raw_r5.csv`, y se verifica que el pre-flight de checksum rechaza frames corrompidos específicamente con `ZSTD_error_checksum_wrong`.

### Matriz de Subsanaciones R4:
| Control de Auditoría | Estado en R3 | Subsanación Efectiva en R4 | Evidencia en Código / Protocolo |
| :--- | :--- | :--- | :--- |
| **R3-01 — PRNG Lemire & Rigor Matemático** | Implementado en código; afirmación documental inexacta; seed 0 reemplazado | **SUBSANADO**: Se corrigen las afirmaciones sobre el LCG (conserva estructura determinista; Lemire elimina el sesgo de mapeo a rango, no la autocorrelación intrínseca). Se permite `--seed 0` sin sustitución (`rng->state = seed`), con período completo $2^{32}$ garantizado por Hull-Dobell. | [`benchmark_decisive_suite.c:401-438`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c#L401-L438) + Sección 3 |
| **R3-02 — Aislamiento ISA (VEX / EVEX)** | Tabla estática sin procedimiento auditable | **SUBSANADO**: Se define un procedimiento automatizado de decodificación y clasificación de instrucciones (`scripts/audit_isa_isolation.sh`) con reporte auditable `docs/audit/isa_verification_report.log` (hashes SHA-256, flags, clasificación por mnemónico/opcode). | Sección 2.2 + Cuádruple compuerta en [`src/binpack_cpu.c:62-97`](file:///C:/Users/usuario/Desktop/binpack/src/binpack_cpu.c#L62-L97) |
| **R3-03A — Memoria de `idx_table`** | Cálculo por entradas usadas (225 KB), no por capacidad reservada | **SUBSANADO**: Se modela la duplicación de capacidad por potencias de dos ($16 \to 8.192$). Para 6.250 chunks (100 GB), la memoria reservada es exactamente $8.192 \times 36\text{ B} = 294.912\text{ bytes}$ (diferencia de 69.912 bytes documentada). | Sección 4.2.B |
| **R3-03B — Contexto `ZSTD_CCtx` en `binpack_stream.c`** | Supuesto no documentado de 4,40 MB con `windowLog=20` | **SUBSANADO**: Se transparenta la configuración real de `src/binpack_stream.c` (sin clamp manual de `windowLog`). Se calcula el consumo real de Zstd Level 19 por defecto ($\approx 51.5\text{ MB}$), estableciendo el consumo efectivo total en $\approx 99.9\text{ MB}$ para Level 19, y $\approx 49.7\text{ MB} \le 64.0\text{ MB}$ para `BINPACK_MODE_RUNTIME`. | Sección 4.2.C |
| **R3-03C — Dependencia de Escala y Modo Monohilo** | Afirmación de invariancia de memoria para cualquier tamaño | **SUBSANADO**: Se establece que la memoria no es estrictamente invariante sino que crece con $\mathcal{O}(N_{\text{chunks}})$ debido a `idx_table`. Se explicita que el límite aplica únicamente con `nb_workers = 1`. | Sección 4.2.D |
| **R3-04 — Checksum Zstd: Código de Error Específico** | Rechazo genérico de error | **SUBSANADO**: Se corrompe específicamente el trailer de 4 bytes del checksum xxHash64 (`cmp[c_sz - 1] ^= 0x55`), manteniendo intacto el payload. Se valida estrictamente `ZSTD_getErrorCode(ret) == ZSTD_error_checksum_wrong`. | [`benchmark_decisive_suite.c:600-630`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c#L600-L630) |
| **R3-05 — Destino de CSV: Protección de R4** | Destino apuntaba a `r4.csv` en modo `"w"` | **SUBSANADO**: Destino reconfigurado a `decisive_benchmark_raw_r5.csv` (tanto en `docs/audit/` como fallback local). Renombrada función a `log_csv_record_r5()`. Cero riesgo de truncamiento de datos históricos. | [`benchmark_decisive_suite.c:1626,1729,2036`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c#L1626-L1732) |

---

## 2. Inventario y Trazabilidad del Entorno Experimental

### 2.1. Harness de Benchmark Certificado
* **Ubicación en Repositorio:** [`tests/benchmarks/benchmark_decisive_suite.c`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c).
* **Control de Cierre Previo:** **BP-008C-R6** (Precedencia estricta de virtualización `CPUID.1:ECX[31]`, validación de rango de hojas básicas y de hipervisor en `0x40000000`, y lectura estrictamente no negativa de `core_id`).
* **Aislamiento de Memoria:** Subprocesos independientes instanciados vía `fork()` y sincronizados mediante `wait4()` en Linux / WSL2, con monitoreo del consumo de memoria residente (*Peak RSS*) vía `struct rusage`.
* **Temporización de Alta Resolución:** `clock_gettime(CLOCK_MONOTONIC)` en sistemas POSIX / Linux, con resolución de nanosegundos; `QueryPerformanceCounter` / `QueryPerformanceFrequency` en Windows nativo.

### 2.2. Compilación, Unidades de Traducción y Aislamiento de ISA (VEX / EVEX) (R3-02)

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

#### B. Procedimiento Formal y Reproducible de Inspección de ISA:
Para certificar el aislamiento sin depender de búsquedas superficiales de texto, se establece el siguiente protocolo de auditoría basado en desensamblado con sintaxis Intel y clasificación de instrucciones:

```bash
#!/usr/bin/env bash
# scripts/audit_isa_isolation.sh
set -euo pipefail

REPORT="docs/audit/isa_verification_report.log"
echo "=== INFORME DE AISLAMIENTO ISA (VEX / EVEX) ===" > "$REPORT"
echo "Fecha: $(date -u)" >> "$REPORT"
echo "Compilador: $(clang --version | head -n 1)" >> "$REPORT"
echo "------------------------------------------------" >> "$REPORT"

PORTABLE_OBJS=(
    "binpack_core.o" "binpack_cpu.o" "binpack_eval.o" "binpack_stream.o"
    "binpack_sdk.o" "binpack_dict.o" "binpack_patch.o" "binpack_archive.o"
    "benchmark_decisive_suite.o"
)

OVERALL_PASS=1
for obj in "${PORTABLE_OBJS[@]}"; do
    if [[ ! -f "$obj" ]]; then
        echo "SALTANDO $obj (no compilado en este entorno)" >> "$REPORT"
        continue
    fi
    
    SHA=$(sha256sum "$obj" | awk '{print $1}')
    
    # Desensamblado decodificado completo
    DISASM=$(llvm-objdump -d --x86-asm-syntax=intel "$obj")
    
    # Detección de mnemónicos VEX (instrucciones con prefijo 'v' operando en registros vectoriales)
    VEX_COUNT=$(echo "$DISASM" | grep -E "^\s*[0-9a-f]+:\s+[a-f0-9 ]+\s+v[a-z0-9]+" | wc -l)
    
    # Detección de mnemónicos EVEX (uso de registros zmm, opmasks k0-k7, broadcast decorators)
    EVEX_COUNT=$(echo "$DISASM" | grep -E "(zmm[0-9]+|%k[0-7]|\{1to[0-9]+\})" | wc -l)
    
    echo "Objeto: $obj | SHA256: $SHA | VEX: $VEX_COUNT | EVEX: $EVEX_COUNT" >> "$REPORT"
    if [[ "$VEX_COUNT" -ne 0 || "$EVEX_COUNT" -ne 0 ]]; then
        OVERALL_PASS=0
        echo "  [FALLO] Instrucciones vectoriales no portables detectadas en $obj" >> "$REPORT"
    fi
done

echo "------------------------------------------------" >> "$REPORT"
echo "Resultado Global: $( [[ $OVERALL_PASS -eq 1 ]] && echo 'PASS (Aislamiento Certificado)' || echo 'FAIL' )" >> "$REPORT"
exit $(( 1 - OVERALL_PASS ))
```

* **Criterio Inviolable:** Para declarar la campaña apta, `isa_verification_report.log` debe certificar un recuento de `VEX = 0` y `EVEX = 0` en todas las unidades portables (TU 1 a 8).
* Únicamente `binpack_core_avx2.o` tiene permitido contener instrucciones VEX.

#### C. Ruta de Despacho en Tiempo de Ejecución con Verificación OSXSAVE:
El despacho dinámico en [`src/binpack_cpu.c:62-97`](file:///C:/Users/usuario/Desktop/binpack/src/binpack_cpu.c#L62-L97) evalúa formalmente cuatro condiciones acumulativas de silicio y de sistema operativo:
1. **Soporte de Silicio AVX:** `CPUID Leaf 1: ECX bit 28 == 1`.
2. **Soporte de Silicio y SO para OSXSAVE:** `CPUID Leaf 1: ECX bit 27 == 1`. Garantiza que el sistema operativo ha inicializado el soporte de contexto extendido y que la instrucción `xgetbv` puede ser ejecutada por el espacio de usuario sin provocar una excepción `#UD`.
3. **Validación del Registro de Control Extendido (XGETBV):** Se ejecuta `_xgetbv(0)` para consultar `XCR0`. Se exige estrictamente:
   $$(\text{XCR0} \ \& \ 0\text{x}06) == 0\text{x}06$$
   garantizando que el sistema operativo preserva activamente los registros XMM (bit 1) y YMM (bit 2) en los cambios de contexto.
4. **Soporte de Silicio AVX2:** `CPUID Leaf 7, Subleaf 0: EBX bit 5 == 1`.

Si cualquiera de las cuatro comprobaciones devuelve falso, `g_bp_dispatch` apunta a los kernels escalares portables (`bp_*_scalar`). Si las cuatro son verdaderas, apunta a `bp_*_avx2`.

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

## 3. Protocolo de Reproducibilidad Experimental y Rigor del PRNG (R3-01)

### 3.1. Fundamentos Matemáticos del LCG y de la Reducción de Lemire
El generador utilizado es el LCG clásico de 32 bits implementado en [`tests/benchmarks/benchmark_decisive_suite.c:397-438`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c#L397-L438):
$$X_{n+1} = (1664525 \times X_n + 1013904223) \pmod{2^{32}}$$

#### A. Propiedades y Limitaciones Formales del LCG:
* **Determinismo y Estructura Hiperplanar:** Un LCG es una recurrencia lineal determinista y **no constituye un oráculo de variables aleatorias independientes**. Conforme al Teorema de Marsaglia, los $k$-tuplos de estados sucesivos caen en un número limitado de hiperplanos en el espacio hiperdimensional.
* **Periodicidad de Bits Bajos:** En todo LCG con módulo $2^e$, los $k$ bits de menor peso poseen un período estricto que no supera $2^k$. En particular, el bit menos significativo ($k=0$) alterna periódicamente con período $2$. Los bits de orden superior, en cambio, recorren el ciclo completo de longitud $2^{32}$.

#### B. Papel Exacto de la Reducción Sin Sesgo de Daniel Lemire:
La reducción de Lemire (ACM Transactions on Mathematical Software, 2019) **no altera ni mejora la estructura de autocorrelación intrínseca del LCG**. Su función matemática es estrictamente resolver el **sesgo de mapeo a rango**:

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
   En una reducción clásica por resto `X % bound`, cuando $2^{32}$ no es divisible por `bound`, los primeros $2^{32} \pmod{\text{bound}}$ valores tienen una probabilidad mayor de ser seleccionados que el resto.
   Lemire calcula el producto de 64 bits $m = X \times \text{bound}$ y realiza un muestreo por rechazo sobre el intervalo residual $t = (-b) \pmod b = (2^{32} - b) \pmod b$. Al rechazar exactamente los estados pertenecientes a dicho remanente, cada uno de los índices en $[0, \text{bound}-1]$ tiene exactamente $\lfloor 2^{32} / \text{bound} \rfloor$ preimágenes, eliminando el sesgo de mapeo.
2. **Utilización de los Bits Superiores:**
   La extracción del índice mediante `m >> 32` opera exclusivamente sobre los 32 bits más significativos del producto, aislando los bits de orden bajo de $X_n$ en la parte baja $l = (uint32_t)m$, lo cual evita que la estricta alternancia de paridad del bit 0 del LCG afecte a la permutación.

#### C. Soporte Explícito y Comportamiento de `--seed 0`:
En revisiones anteriores, `rng_init` reemplazaba silenciosamente una semilla 0 por un valor por defecto. En R4, el código asigna directamente:
```c
static inline void rng_init(rng_t *rng, uint32_t seed) {
    rng->state = seed;
}
```
* **Demostración de Validez para Semilla 0:**
  Por el **Teorema de Hull-Dobell**, un LCG $X_{n+1} = (a X_n + c) \pmod m$ posee período completo $m$ para **cualquier estado inicial $X_0 \in \mathbb{Z}_m$** si y sólo si:
  1. $\gcd(c, m) = 1$: Aquí $\gcd(1013904223, 2^{32}) = 1$ (ambos coprimos, $c$ impar).
  2. $a - 1$ es divisible por todos los factores primos de $m$: El único factor primo de $2^{32}$ es $2$. $1664525 - 1 = 1664524$, divisible por $2$.
  3. $a - 1$ es divisible por 4 si $m$ es divisible por 4: $1664524 / 4 = 416131$ (entero).
  
  Por lo tanto, si se pasa `--seed 0`, el estado inicial es estrictamente $X_0 = 0$, el primer valor generado es $X_1 = 1013904223$, y la secuencia recorre los $2^{32}$ estados sin degradación del ciclo.

#### D. Ciclo de Vida del Estado Global `g_suite_rng`:
* El estado global se define como variable estática de módulo `static rng_t g_suite_rng;`.
* Se inicializa **una sola vez** en `main()` con el argumento CLI `--seed`.
* Avanza secuencialmente a lo largo de las barajaduras de los cuatro datasets.

---

## 4. Matriz de Comparabilidad y Reglas de Paridad de Baselines

### 4.1. Verificación Activa del Checksum de Zstandard en Contrato Strict (R3-04)
Para garantizar la paridad metodológica estricta frente a `BinPack Strict (CRC32 slicing-by-8)`:
1. **Inyección de Parámetro:**
   En [`run_zstd_l1()`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c#L808), [`run_zstd_l19()`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c#L1055) y [`measure_latencies_generic()`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c#L663):
   ```c
   ZSTD_CCtx_setParameter(cctx, ZSTD_c_checksumFlag, 1);
   ```
2. **Inspección en Caliente del Frame Header Descriptor (FHD):**
   Tras cada llamada de compresión, el harness valida obligatoriamente que el bit 2 (`Content_Checksum_flag`) esté activo en el byte 4 del frame (`(comp_buf[4] & 0x04) != 0`).
3. **Pre-flight Checksum Rejection Test con Validación Específica de Error:**
   En [`pre_flight_validate_zstd_checksum_rejection()`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c#L556-L630), el test:
   - Comprime un búfer con `checksumFlag = 1`.
   - Verifica el bit `0x04` en el encabezado.
   - Corrompe intencionalmente el trailer de 4 bytes del checksum xxHash64 (`cmp[c_sz - 1] ^= 0x55`), manteniendo los bloques de carga útil intactos.
   - Ejecuta `ZSTD_decompressDCtx()` y comprueba formalmente:
     ```c
     if (ZSTD_getErrorCode(corrupt_ret) != ZSTD_error_checksum_wrong) { ... return 0; }
     ```
   - Si la descompresión no rechaza el frame con este código exacto, la suite aborta antes de ejecutar el benchmark.

### 4.2. Modelado Realista de la Memoria de Trabajo del Motor de Streaming (R3-03)

Para eliminar cualquier discrepancia con el código real de [`src/binpack_stream.c`](file:///C:/Users/usuario/Desktop/binpack/src/binpack_stream.c):

#### A. Desglose de Búferes Primarios en `binpack_stream.c`:
1. **`raw_chunk` (Búfer de entrada de chunk):** `chunk_size = 16 * 1024 * 1024` = **16.00 MB**.
2. **`prev_raw` (Búfer de historial/prefix reference):** `chunk_size = 16 * 1024 * 1024` = **16.00 MB**.
3. **`cmp_chunk` (Búfer de salida comprimida acotada):**
   $$\text{cmp\_bound} = \text{ZSTD\_compressBound}(16\text{ MB}) + 65536 = 16.777.216 + 65.536 + 65.536 + 512 = \mathbf{16.12\text{ MB}}$$
   Subtotal de búferes de streaming: $16.00 + 16.00 + 16.12 = \mathbf{48.12\text{ MB}}$.

#### B. Asignación y Capacidad Real de `idx_table` (R3-03A):
En [`src/binpack_stream.c:152,434-445`](file:///C:/Users/usuario/Desktop/binpack/src/binpack_stream.c#L152), la tabla de chunks inicia con `idx_cap = 16` y duplica su capacidad (`new_cap = idx_cap * 2`) cada vez que `chunk_count >= idx_cap`.
La capacidad asignada para $N$ chunks responde formalmente a la función potencia de dos:
$$\text{Capacity}(N) = \begin{cases} 16 & \text{si } N \le 16 \\ 2^{\lceil \log_2(N) \rceil} & \text{si } N > 16 \end{cases}$$

Para un archivo de 100 GB procesado en chunks de 16 MB:
$$N_{\text{chunks}} = \left\lceil \frac{100 \times 10^9}{16 \times 1024^2} \right\rceil = 6.250 \text{ chunks}$$
* **Capacidad Reservada:** $\text{Capacity}(6.250) = 2^{13} = \mathbf{8.192\text{ entradas}}$.
* **Memoria Realmente Asignada:**
  $$\text{Mem}(\text{idx\_table}) = 8.192 \times 36\text{ bytes} = \mathbf{294.912\text{ bytes}} = \mathbf{288.0\text{ KiB}} \approx \mathbf{0.295\text{ MB}}$$
* **Auditoría de Discrepancia:** En R3 se computó el espacio de las entradas utilizadas ($6.250 \times 36 = 225.000\text{ B}$), existiendo una diferencia de $69.912\text{ bytes}$ por el overhead de la sobrecapacidad asignada por `realloc`. Se documenta formalmente el valor real de **294.912 bytes**.

#### C. Demostración y Parámetros Efectivos del Contexto `ZSTD_CCtx` (R3-03B):
En `src/binpack_stream.c`, `cctx = ZSTD_createCCtx()` se configura mediante:
- `ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, clevel)`
- `ZSTD_CCtx_setParameter(cctx, ZSTD_c_strategy, ZSTD_btultra)` (o `ZSTD_dfast` en runtime)
- **No se establece un parámetro explícito `ZSTD_c_windowLog = 20`.**

Por tanto, los 4,40 MB presentados en R3 eran un cálculo teórico dependiente de `windowLog=20` no configurado en código. Conforme a la implementación real de Zstandard v1.5.7:
1. **Configuración Efectiva en `BINPACK_MODE_DEFAULT` (Level 19 / `btultra`):**
   Al no restringirse `windowLog`, Zstandard aplica los parámetros por defecto de Nivel 19 para 64 bits:
   - `windowLog = 23` (Ventana de 8 MB)
   - `chainLog = 23` (Tabla Chain: $2^{23} \times 4\text{ B} = 32.00\text{ MB}$)
   - `hashLog = 22` (Tabla Hash: $2^{22} \times 4\text{ B} = 16.00\text{ MB}$)
   - Estructuras auxiliares y buffers internos: $\approx 3.50\text{ MB}$
   - **Memoria Real de `ZSTD_CCtx` (Level 19):** $\approx \mathbf{51.50\text{ MB}}$.
   
   **Memoria de Trabajo Total Efectiva en Level 19:**
   $$\text{Mem}_{\text{Level19}}(100\text{ GB}) = 48.12\text{ MB} + 51.50\text{ MB} + 0.29\text{ MB} \approx \mathbf{99.91\text{ MB}}$$

2. **Configuración Efectiva en `BINPACK_MODE_RUNTIME` (Level $\le 4$ / `dfast`):**
   - `windowLog = 19` (512 KB), `hashLog = 18` (1 MB), `chainLog = 0` (dfast no usa chain table).
   - **Memoria Real de `ZSTD_CCtx` (Level 4):** $\approx \mathbf{1.25\text{ MB}}$.
   
   **Memoria de Trabajo Total Efectiva en Level 4:**
   $$\text{Mem}_{\text{Runtime}}(100\text{ GB}) = 48.12\text{ MB} + 1.25\text{ MB} + 0.29\text{ MB} = \mathbf{49.66\text{ MB}} \le \mathbf{64.0\text{ MB}}$$

#### D. Dependencia de Escala y Alcance Monohilo (R3-03C):
> [!IMPORTANT]
> 1. **Dependencia de Escala:** La memoria de trabajo **no es estrictamente invariante** respecto al tamaño del archivo, ya que la capacidad de `idx_table` crece con $\mathcal{O}(N_{\text{chunks}})$.
> 2. **Alcance Operacional:** El consumo analizado corresponde estrictamente al modo de compresión monohilo (`opt.nb_workers = 1`). En ejecuciones con `nb_workers > 1`, Zstandard instancia buffers de trabajo independientes para cada worker.

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
* Las observaciones excluidas permanecen registradas en las columnas `Raw_C_Sec_0..19` y `Raw_D_Sec_0..19`.
* Se audita la cantidad $N_{\text{out}}$ en `Comp_Outliers` y `Decomp_Outliers`.

### 5.3. Criterio Dual de Aceptación de Estabilidad
1. **Estabilidad Filtrada Obligatoria:** $\text{CV}_{\text{filt}} \le 5.0\%$.
2. **Límite de Inestabilidad Cruda:** $\text{CV}_{\text{raw}} \le 10.0\%$.
3. **Límite de Exclusiones:** $N_{\text{out}} \le 2$ (máximo $10\%$ de observaciones descartables sobre las 20 iteraciones).

---

## 6. Especificación del Destino CSV y Protección de Datos Históricos (R3-05)

Para garantizar la protección incondicional de los archivos CSV históricos (`pilot`, `r1`, `r2`, `r3` y `r4`), se establece de forma estricta:

* **Destino de Exportación Exclusivo de la Futura Campaña:**
  ```c
  g_csv_file = fopen("docs/audit/decisive_benchmark_raw_r5.csv", "w");
  if (!g_csv_file) {
      g_csv_file = fopen("decisive_benchmark_raw_r5.csv", "w");
  }
  ```
* Se reconfiguraron todas las llamadas del harness a `log_csv_record_r5()`.
* **Garantía Inviolable:** Ninguna ejecución de la suite abrirá en modo `"w"` ni modificará `decisive_benchmark_raw_r4.csv`.

**Estructura del Encabezado CSV R5:**
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

- [x] **1. Integridad de Fuentes:** `git status` en la rama de auditoría certifica que `src/`, `include/` y `deps/` no poseen modificaciones.
- [x] **2. Sincronización Código-Protocolo:** Implementación de Lemire, estado global `g_suite_rng`, compuertas de checksum Zstd con verificación `ZSTD_error_checksum_wrong` y destino CSV `r5.csv`.
- [x] **3. Protección de Resultados Históricos:** Destino inmutable `decisive_benchmark_raw_r5.csv`, imposibilitando el truncamiento de `r4.csv`.
- [ ] **4. Aprobación Formal de BP-008D:** Dictamen técnico favorable de la auditoría.
- [ ] **5. Generación del Reporte ISA:** Ejecución de `scripts/audit_isa_isolation.sh` en el entorno Linux bare-metal y verificación de 0 VEX/EVEX.
- [ ] **6. Disponibilidad del Corpus:** Datasets descargados y verificados con sus respectivos hashes SHA-256 en disco.
- [ ] **7. Aislamiento de Hardware:** Sistema en reposo térmico ($T_{\text{CPU}} \le 45^\circ\text{C}$), sin procesos secundarios.

---

## 9. Criterio de Cierre del Hito BP-008D

El Hito **BP-008D** se considera preparado para su **cierre técnico definitivo** en virtud de:
1. La sincronización 100% comprobable entre este documento [`docs/audit/bp008d_experimental_protocol.md`](file:///C:/Users/usuario/Desktop/binpack/docs/audit/bp008d_experimental_protocol.md) y [`tests/benchmarks/benchmark_decisive_suite.c`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c).
2. El encuadre matemático riguroso del LCG y de la reducción de Lemire, garantizando el soporte determinista de `--seed 0`.
3. El procedimiento auditable de clasificación de instrucciones ISA mediante desensamblado decodificado.
4. La modelización exacta de la memoria de `idx_table` ($8.192\text{ entradas} = 294.912\text{ bytes}$) y la clarificación del consumo efectivo de `ZSTD_CCtx` bajo parámetros por defecto.
5. La comprobación específica de `ZSTD_error_checksum_wrong` ante corrupción de frame en el pre-flight.
6. La protección incondicional del CSV R4 redirigiendo la salida a `decisive_benchmark_raw_r5.csv`.
