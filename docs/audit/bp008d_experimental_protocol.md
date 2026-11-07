# Protocolo de Reproducibilidad Experimental, Trazabilidad Metodológica y Baselines — Hito BP-008D (Revisión R9)

**Expediente:** BP-008D-R9 — Corrección Final de Trazabilidad ISA y Validación Adversarial CSV: Sincronización Estricta 1:1 de Banderas de Compilación (`PORTABLE_CFLAGS` y `AVX2_CFLAGS`), Eliminación de `continue` Inválido en Bash, Validación de Identidad por Handle Win32 (`BY_HANDLE_FILE_INFORMATION`) y `fstat`/`lstat` POSIX, Política Defensiva de Limpieza Post-Fallo sin Suposiciones Incondicionales de Cero-TOCTOU, Suite Adversarial de 7 Casos de Error CSV y Verificación Multiplataforma en CI (Linux y Windows).  
**Proyecto:** BinPack Enterprise SDK  
**Ramas Auditadas:** `audit/bp1-core-safety`  
**Hito Previo:** BP-008D-R8 (Commit `1f2b3215a1f50382d4d193e953290790c952363a`)  
**Fecha:** 7 de Noviembre de 2026  
**Autor:** Santiago Robledo (<santiagorobledo198@gmail.com>)  
**Responsable Técnico:** Antigravity AI — Advanced Agentic Coding & Systems Architecture  

---

## 1. Resumen Ejecutivo y Respuesta a la Auditoría BP-008D-R8

El presente documento constituye la **Revisión R9 del Expediente BP-008D**, formalizada en estricta y rigurosa respuesta a las observaciones y requerimientos del Dictamen Técnico de Auditoría R8.

En esta revisión se abordan y subsanan integralmente las cuatro observaciones técnicas formuladas:

1. **Trazabilidad Estricta de Flags ISA y Corrección del Script de Desensamblado (R9-01):**
   - En [`.github/workflows/build-multiplatform.yml`](file:///C:/Users/usuario/Desktop/binpack/.github/workflows/build-multiplatform.yml), el paso de compilación de Linux exporta explícitamente las variables de entorno `PORTABLE_CFLAGS="-O3 -march=x86-64 -mno-avx -mno-avx2 -fPIC"` y `AVX2_CFLAGS="-O3 -mavx2 -fPIC"`, pasándolas idénticas tanto al comando de compilación `gcc` como al script [`scripts/audit_isa_isolation.sh`](file:///C:/Users/usuario/Desktop/binpack/scripts/audit_isa_isolation.sh).
   - Se eliminó del script y del reporte la declaración espuria de banderas no pasadas (`-Wall -Wextra`), garantizando una correspondencia 1:1 verificable con el flujo de CI.
   - En [`scripts/audit_isa_isolation.sh`](file:///C:/Users/usuario/Desktop/binpack/scripts/audit_isa_isolation.sh), se corrigió el error sintáctico de Bash originado por el uso de `continue` fuera de un bucle en el bloque de inspección del objeto AVX2 (`binpack_core_avx2.o`), reestructurándolo con ramificaciones condicionales limpias (`if/else`) que preservan el código de retorno cero y evitan advertencias en `stderr`.
2. **Identidad de Archivo Robusta y Política Defensiva de Limpieza Post-Fallo (R9-02):**
   - En [`tests/benchmarks/benchmark_decisive_suite.c`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c), se implementó una verificación de identidad de archivo a nivel de kernel tanto para Windows como para POSIX:
     - **Windows Nativo:** Se consulta el descriptor de archivo subyacente mediante `_get_osfhandle(fd)` y la API Win32 `GetFileInformationByHandle()`, comparando `dwVolumeSerialNumber`, `nFileIndexHigh` y `nFileIndexLow` (el File ID NTFS de 64 bits) con los metadatos de un handle abierto concurrentemente sobre la ruta con `FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE`.
     - **POSIX / Linux / macOS:** Se comparan `st_dev` y `st_ino` obtenidos mediante `fstat(fd)` contra los de la ruta consultada vía `lstat(path)`.
   - **Política Defensiva:** Se reconoce formalmente que, bajo operaciones de borrado basadas en ruta (`unlink` / `_unlink`), existe una ventana teórica TOCTOU si un proceso externo reemplaza maliciosamente el enlace entre la verificación y el desvinculado. En consecuencia:
     - Se suprime categóricamente cualquier afirmación de "cero TOCTOU incondicional".
     - Si la identidad del archivo no puede verificarse de forma concluyente (p. ej., error al consultar metadatos), **se cancela la eliminación para proteger archivos ajenos**.
     - Si la desvinculación falla o se cancela, se emite una advertencia formal por `stderr` indicando la presencia de un posible archivo residual inerte.
     - En ningún caso se trunca ni se sobrescribe un archivo CSV preexistente.
3. **Suite Unitaria Adversarial de 7 Casos de Error CSV (R9-03):**
   - La suite [`run_csv_safety_unit_tests()`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c) se expandió de 3 a **7 casos exhaustivos**:
     - **Caso 1:** Creación exclusiva en ruta limpia (`CSV_OPEN_SUCCESS`).
     - **Caso 2:** Detección de colisión `EEXIST` y preservación bit a bit del archivo previo sin truncamiento.
     - **Caso 3:** Detección de directorio inexistente `ENOENT` y autorización de fallback (`CSV_OPEN_DIR_NOT_FOUND`).
     - **Caso 4:** Agotamiento de descriptores de archivo (`EMFILE`) mediante loop controlado; confirmación de aborto fatal (`CSV_OPEN_FATAL_ERROR`).
     - **Caso 5:** Permiso denegado (`EACCES`) en directorio sin permisos de escritura; confirmación de aborto fatal (`CSV_OPEN_FATAL_ERROR`).
     - **Caso 6:** Manejo de colisión de sufijos (`_run1`..`_run999`) y asignación segura en `open_safe_csv_r5()`.
     - **Caso 7:** Verificación de identidad distinguiendo archivos distintos (prevención de borrado accidental de archivos ajenos).
4. **Integración Continua Multiplataforma de la Suite CSV (R9-04):**
   - En [`.github/workflows/build-multiplatform.yml`](file:///C:/Users/usuario/Desktop/binpack/.github/workflows/build-multiplatform.yml), se integró la compilación de `benchmark_decisive_suite.c` y la ejecución del flag `--test-csv-errors` tanto en el runner **Linux x86_64** como en el runner **Windows x86_64**, garantizando que el comportamiento ante errores atómicos se valide en ambos sistemas operativos antes de autorizar cualquier benchmark.

### Matriz Consolidada de Subsanaciones R9:
| Control de Auditoría | Estado en R8 | Subsanación Efectiva en R9 | Evidencia en Código / Repositorio |
| :--- | :--- | :--- | :--- |
| **R9-01 — Trazabilidad de Flags y Bash sin Errores** | Flags hardcodeadas `-Wall -Wextra`; `continue` fuera de loop en Bash | **SUBSANADO**: Sincronización 1:1 de `PORTABLE_CFLAGS` y `AVX2_CFLAGS` en CI y script. Eliminación de flags espurias. Reemplazo de `continue` por condicionales `if/else` limpios. | [`scripts/audit_isa_isolation.sh:22-38,180-210`](file:///C:/Users/usuario/Desktop/binpack/scripts/audit_isa_isolation.sh) + [`.github/workflows/build-multiplatform.yml`](file:///C:/Users/usuario/Desktop/binpack/.github/workflows/build-multiplatform.yml) |
| **R9-02 — Identidad de Handle Win32 y Política Defensiva** | Identidad Win32 limitada; afirmación imprudente de inmunidad TOCTOU | **SUBSANADO**: `BY_HANDLE_FILE_INFORMATION` en Windows (`dwVolumeSerialNumber`, File Index 64-bit) y `fstat`/`lstat` en POSIX. Política defensiva: supresión de borrado ante duda, advertencia de residuales, sin truncamiento. | [`tests/benchmarks/benchmark_decisive_suite.c:1660-1790`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c) |
| **R9-03 — Suite Adversarial de 7 Casos CSV** | Suite limitada a 3 pruebas | **SUBSANADO**: 7 casos completos cubriendo `EEXIST`, `ENOENT`, `EMFILE`, `EACCES`, rotación de sufijos, creación limpia y validación de identidad. | [`tests/benchmarks/benchmark_decisive_suite.c:1980-2140`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c) |
| **R9-04 — CI Multiplataforma de Pruebas CSV** | Solo validación de compilación sin `--test-csv-errors` en CI | **SUBSANADO**: Compilación y ejecución de `--test-csv-errors` integradas en GitHub Actions en Linux (`gcc`) y Windows (`zig cc`). | [`.github/workflows/build-multiplatform.yml`](file:///C:/Users/usuario/Desktop/binpack/.github/workflows/build-multiplatform.yml) |

---

## 2. Inventario y Trazabilidad del Entorno Experimental

### 2.1. Harness de Benchmark Certificado
* **Ubicación en Repositorio:** [`tests/benchmarks/benchmark_decisive_suite.c`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c).
* **Control de Cierre Previo:** **BP-008C-R6** (Precedencia estricta de virtualización `CPUID.1:ECX[31]`, validación de rango de hojas básicas y de hipervisor en `0x40000000`, y lectura estrictamente no negativa de `core_id`).
* **Aislamiento de Memoria:** Subprocesos independientes instanciados vía `fork()` y sincronizados mediante `wait4()` en Linux / WSL2, con monitoreo del consumo de memoria residente (*Peak RSS*) vía `struct rusage`.
* **Temporización de Alta Resolución:** `clock_gettime(CLOCK_MONOTONIC)` en sistemas POSIX / Linux, con resolución de nanosegundos; `QueryPerformanceCounter` / `QueryPerformanceFrequency` en Windows nativo.

### 2.2. Aislamiento de ISA y Procedimiento Formal de Inspección (R9-01)

La arquitectura de compilación garantiza que los módulos portables se generen con banderas estrictamente escalares, impidiendo que el compilador autovectorice bucles utilizando extensiones AVX/AVX2/AVX-512:

```
[ UNIDADES PORTABLES (TU 1..8) ]              [ UNIDAD SIMD AISLADA (TU 9) ]
gcc -O3 -march=x86-64 -fPIC                   gcc -O3 -mavx2 -fPIC
    -mno-avx -mno-avx2                              (binpack_core_avx2.c)
(binpack_core.c, cpu, eval, stream, etc.)                     |
             |                                                |
             +-----------------------+------------------------+
                                     |
                                     v
                       [ ENLACE DINÁMICO / ESTÁTICO ]
```

#### Reglas de Validación Estricta en [`scripts/audit_isa_isolation.sh`](file:///C:/Users/usuario/Desktop/binpack/scripts/audit_isa_isolation.sh):
1. **Trazabilidad 1:1 de Flags de Compilación:**
   El script consume `PORTABLE_CFLAGS` y `AVX2_CFLAGS` provistas por el pipeline de CI:
   ```bash
   PORTABLE_CFLAGS="${PORTABLE_CFLAGS:--O3 -march=x86-64 -mno-avx -mno-avx2 -fPIC}"
   AVX2_CFLAGS="${AVX2_CFLAGS:--O3 -mavx2 -fPIC}"
   ```
2. **Exigencia de Código de Retorno Cero:**
   El script exige formalmente que el desensamblador retorne `exit code 0`. Si falla, aborta inmediatamente con fallo crítico.
3. **Rechazo de Salidas Truncas o Vacías:** Se valida que la salida desensamblada posea longitud mayor a cero y contenga bytes de codificación antes de efectuar el conteo.
4. **Validación Obligatoria de Bytes Crudos:**
   ```bash
   HAS_BYTES=$(echo "$DISASM" | grep -E "^[[:space:]]*[0-9a-f]+:[[:space:]]+[0-9a-f]{2}" | head -n 1 || true)
   ```
   Si la herramienta no emite bytes hexadecimales junto a los mnemónicos, el script emite `[FALLO CRITICO]` y aborta con código de salida `exit 1`.
5. **Inspección Simétrica de 4 Capas:**
   - Capa 1: Mnemónicos VEX (`v[a-z0-9]+`).
   - Capa 2: Opcodes binarios x86-64 VEX (`0xC4`, `0xC5`).
   - Capa 3: Mnemónicos y decoradores EVEX (`zmm`, `%k[0-7]`, `{1toN}`, `{z}`).
   - Capa 4: Opcodes binarios x86-64 EVEX (`0x62`).
6. **Estructura de Ejecución Limpia:**
   Se eliminaron sentencias de control de flujo no válidas (`continue` fuera de bucles), garantizando que el análisis de `binpack_core_avx2.o` concluya sin advertencias ni códigos de error espurios.
7. **Diferenciación entre Informe Estático y Artefacto Runtime:**
   - **Informe Estático Versionado ([`docs/audit/isa_verification_report.log`](file:///C:/Users/usuario/Desktop/binpack/docs/audit/isa_verification_report.log)):** Documento de referencia base que describe el formato esperado y los estándares de certificación del repositorio.
   - **Artefacto Runtime de CI (`isa-verification-report`):** Informe emitido dinámicamente en tiempo de compilación por la máquina virtual Linux en cada ejecución de GitHub Actions, capturando los hashes SHA-256 reales generados por el compilador del runner.

---

## 3. Generación Pseudoaleatoria y Permutación Fisher-Yates

### 3.1. Algoritmo LCG Determinista Multiplataforma
Para garantizar bit a bit la misma secuencia de aleatorización entre compiladores y sistemas operativos (Windows, Linux, macOS), el harness implementa su propio generador LCG de 32 bits conforme al estándar de Knuth / Numerical Recipes:
$$X_{n+1} = (1664525 \times X_n + 1013904223) \bmod 2^{32}$$

### 3.2. Reducción Lemire sin Sesgo y Análisis de Correlación:
1. **Eliminación del Sesgo de Partición Modular:**
   En un mapeo por resto `X % bound`, cuando $2^{32}$ no es divisible por `bound`, ciertos índices reciben $\lceil 2^{32} / \text{bound} \rceil$ preimágenes y otros $\lfloor 2^{32} / \text{bound} \rfloor$. Lemire calcula $m = X \times \text{bound}$ y rechaza los valores residuales en $t = (-b) \pmod b$, garantizando que cada índice en $[0, \text{bound}-1]$ tenga exactamente $\lfloor 2^{32} / \text{bound} \rfloor$ preimágenes bajo entradas uniformes.
2. **Propagación de Acarreos y Ausencia de Independencia Estadística:**
   La extracción del índice mediante `m >> 32` no utiliza directamente el LSB como índice. **Sin embargo, la mitad superior del producto depende matemáticamente de los bits inferiores a través de los acarreos aritméticos de la multiplicación de 64 bits.** Por consiguiente, Lemire elimina el sesgo de mapeo, pero **no suprime las correlaciones seriales intrínsecas del LCG**. Se distingue formalmente entre *ausencia de sesgo de reducción* y *ausencia de correlación estadística*.
3. **Validez Matemática de la Semilla Cero (`--seed 0`):**
   Conforme al Teorema de Hull-Dobell, para $X_{n+1} = (a X_n + c) \bmod m$ con $m = 2^{32}$, $a = 1664525$, $c = 1013904223$:
   1. $\gcd(c, m) = \gcd(1013904223, 2^{32}) = 1$ (coprimos).
   2. $a - 1 = 1664524$, divisible por 2.
   3. $a - 1$ es divisible por 4 ($1664524 / 4 = 416131$).
   El período es estrictamente $2^{32}$ para cualquier semilla en $[0, 2^{32}-1]$, incluyendo el estado inicial $0$.

---

## 4. Matriz de Comparabilidad, Memoria y Contratos de Integridad

### 4.1. Verificación Activa del Checksum de Zstandard
1. **Configuración de Checksum:** `ZSTD_CCtx_setParameter(cctx, ZSTD_c_checksumFlag, 1);` activo en todos los runners de Zstandard.
2. **Inspección de Encabezado FHD:** Se valida en caliente que el bit 2 (`Content_Checksum_flag`) esté activo en el byte 4 del frame (`(comp_buf[4] & 0x04) != 0`).
3. **Pre-flight con Comprobación Específica de Error:**
   En [`pre_flight_validate_zstd_checksum_rejection()`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c), se corrompe el checksum y se valida formalmente:
   ```c
   if (ZSTD_getErrorCode(corrupt_ret) != ZSTD_error_checksum_wrong) { return 0; }
   ```

### 4.2. Reconciliación Cuantitativa y Taxonomía de Memoria en Cuatro Capas

#### A. Identificación del Camino Real de Ejecución del SDK:
La auditoría del código en [`src/binpack_stream.c`](file:///C:/Users/usuario/Desktop/binpack/src/binpack_stream.c) establece:
1. `cctx = ZSTD_createCCtx();` (línea 67).
2. Chunk 0: `c_size = ZSTD_compress2(cctx, cmp_chunk, cmp_bound, raw_chunk, bytes_read);` (línea 340).
3. Chunks subsecuentes (> 0): `ZSTD_CCtx_refPrefix(cctx, prefix_ptr, prefix_sz);` y `ZSTD_compressStream2(cctx, &out_buf, &in_buf, ZSTD_e_end);` (líneas 308, 320).

Por tanto, **`ZSTD_estimateCStreamSize()` es el único estimador pertinente para el flujo streaming del motor**, mientras que `ZSTD_estimateCCtxSize()` se mantiene exclusivamente como dato informativo no aplicable.

#### B. Componentes Propios del Motor BinPack:
* **Búferes de Streaming:**
  - `raw_chunk`: $16\text{ MB} = 16.777.216\text{ B}$ ($16.00\text{ MiB}$).
  - `prev_raw`: $16\text{ MB} = 16.777.216\text{ B}$ ($16.00\text{ MiB}$).
  - Límite de compresión: $ZSTD\_compressBound(16.777.216) = \mathbf{16.842.752\text{ bytes}}$.
  - Reserva `cmp_chunk`: $cmp\_bound = 16.842.752 + 65.536 = \mathbf{16.908.288\text{ bytes}}$ ($16.125\text{ MiB}$).
  - **Subtotal Búferes:** $2(16.777.216) + 16.908.288 = \mathbf{50.462.720\text{ bytes}}$ ($48.125\text{ MiB}$).
* **Tabla de Índices de Chunks (`idx_table`):**
  - Capacidad preasignada: $8.192\text{ entradas}$ $\times$ $36\text{ bytes}$ (`binpack_chunk_info_t`) = $\mathbf{294.912\text{ bytes}}$ ($288.0\text{ KiB}$).
* **Total Búferes Propios BinPack:**
  $$50.462.720 + 294.912 = \mathbf{50.757.632\text{ bytes}} \quad (48.406\text{ MiB} \approx 50.76\text{ MB})$$

#### C. Presupuesto Cuantitativo de Memoria (Estimador Streaming Aplicable):
| Perfil de Ejecución | Estimador Zstd Aplicable | Estimación Zstd | Memoria Propia BinPack | Total Calculado | Presupuesto Límite | Margen Holgado |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Runtime Mode (Nivel 4 / dfast)** | `ZSTD_estimateCStreamSize(4)` | $1.626.112\text{ B}$ ($1.55\text{ MiB}$) | $50.757.632\text{ B}$ ($48.41\text{ MiB}$) | **$52.383.744\text{ B}$ ($49.96\text{ MiB}$ / $52.38\text{ MB}$)** | **$\le 64.0\text{ MB}$** | **$+11.62\text{ MB}$** |
| **Default Mode (Nivel 19 / btultra)** | `ZSTD_estimateCStreamSize(19)` | $67.112.960\text{ B}$ ($64.00\text{ MiB}$) | $50.757.632\text{ B}$ ($48.41\text{ MiB}$) | **$117.870.592\text{ B}$ ($112.41\text{ MiB}$ / $117.87\text{ MB}$)** | **$\le 128.0\text{ MB}$** | **$+10.13\text{ MB}$** |

*Dato informativo (no aplicable a streaming):* `ZSTD_estimateCCtxSize(4) = 1.314.816 B` ($1.25\text{ MiB}$), `ZSTD_estimateCCtxSize(19) = 50.335.744 B` ($48.00\text{ MiB}$).

#### D. Deslinde Taxonómico Cuatripartito de Memoria:
Para evitar cualquier confusión ontológica entre fórmulas teóricas y memoria de proceso:
1. **Memoria propia reservada por BinPack:** $50.757.632\text{ B}$, correspondiente a los búferes de trabajo fijos `raw_chunk`, `prev_raw`, `cmp_chunk` y la tabla `idx_table`.
2. **Memoria estimada para el contexto Zstandard:** Computada analíticamente vía `ZSTD_estimateCStreamSize()` ($1.63\text{ MB}$ en L4; $67.11\text{ MB}$ en L19).
3. **Memoria efectivamente reservada por el proceso:** Memoria virtual asignada en runtime (`malloc` interno del motor y buffers de I/O).
4. **Peak RSS medido durante la ejecución:** Resident Set Size registrado por el sistema operativo (`struct rusage`). En las pruebas monobloque (`silesia_mozilla` de 51.2 MB), el harness retiene en RAM tanto el búfer de entrada `d_moz` (51.2 MB) como el de descompresión `dbuf` (51.2 MB) para ejecutar la validación `memcmp`, sumando $102.4\text{ MB}$ adicionales que elevan el Peak RSS a 115–165 MB. Este valor refleja la instrumentación del harness y no la memoria del motor de streaming ($\le 64\text{ MB}$).

---

## 5. Procedimiento de Validación y Análisis Estadístico Dual

### 5.1. Reporte Dual Obligatorio (Crudo vs. Filtrado)
* **Tendencia Central:** Mediana cruda ($\text{Mediana}_{\text{raw}}$) y Mediana filtrada tras Tukey ($\text{Mediana}_{\text{filt}}$).
* **Dispersión:** IQR crudo y filtrado ($\text{IQR}_{\text{raw}}$, $\text{IQR}_{\text{filt}}$), Coeficiente de Variación ($\text{CV}_{\text{raw}}$, $\text{CV}_{\text{filt}}$).
* **Auditoría de Outliers:** Conteo exacto $N_{\text{out}}$ y porcentaje de descarte.
* **Criterios de Estabilidad:** $\text{CV}_{\text{filt}} \le 5.0\%$, $\text{CV}_{\text{raw}} \le 10.0\%$, $N_{\text{out}} \le 2$.

---

## 6. Destino CSV, Identidad a Nivel de Kernel y Suite Adversarial (R9-02, R9-03)

Para garantizar la protección incondicional tanto de los CSV históricos (`pilot`, `r1`, `r2`, `r3`, `r4`) como de cualquier ejecución previa de R5, se implementa creación atómica en el kernel del SO, con discriminación estricta de códigos de error y verificación de identidad de handle / inodo:

```c
typedef enum {
    CSV_OPEN_SUCCESS = 0,
    CSV_OPEN_COLLISION_EEXIST, /* File already exists: safe collision, try next suffix */
    CSV_OPEN_DIR_NOT_FOUND,    /* Directory does not exist (ENOENT): permitted to fallback to next directory */
    CSV_OPEN_FATAL_ERROR       /* Permission denied (EACCES), fd exhaustion (EMFILE/ENFILE), or other fatal error: abort */
} csv_open_result_t;
```

### Reglas de Conducta Exigidas y Manejo de Identidad:
1. **`EEXIST`:** El archivo ya existe. Se detecta como colisión segura (`CSV_OPEN_COLLISION_EEXIST`) y se procede a probar secuencialmente `_run1.._run999` dentro del mismo directorio. **Jamás se sobrescribe ni se trunca el archivo existente.**
2. **`ENOENT`:** El directorio preferido no existe (`CSV_OPEN_DIR_NOT_FOUND`). **Únicamente ante este error se autoriza el fallback al siguiente directorio candidato** (`docs/audit` $\rightarrow$ `.`).
3. **`EACCES`, `EMFILE`, `ENFILE` u otros errores:** Se catalogan como errores operativos no recuperables (`CSV_OPEN_FATAL_ERROR`). **Se aborta inmediatamente sin intentar directorios alternativos**, impidiendo que fallos de permisos o de agotamiento de descriptores se enmascaren como indisponibilidad de ruta.
4. **Manejo de Errores de Conversión a `FILE*` y Limpieza Defensiva:**
   Si `fdopen()` / `_fdopen()` falla:
   - Se comprueba la identidad física del archivo:
     - En **Windows**, mediante `_get_osfhandle(fd)` y `CreateFileA()` para comparar `BY_HANDLE_FILE_INFORMATION` (`dwVolumeSerialNumber`, `nFileIndexHigh`, `nFileIndexLow`).
     - En **POSIX**, mediante `fstat(fd)` y `lstat(path)` para comparar `st_dev` y `st_ino`.
   - **Política Defensiva:** Si la identidad no coincide o no puede ser verificada positivamente, **se suprime el intento de desvinculación para no afectar un archivo ajeno**.
   - Si la desvinculación falla o se cancela, se emite una advertencia formal indicando que ha quedado un archivo residual inerte.
   - Se prescinde formalmente de afirmaciones de inmunidad absoluta a TOCTOU en llamadas de desvinculación basadas en ruta.
5. **Aborto Obligatorio en `main()`:**
   Si `open_safe_csv_r5()` retorna `NULL`, el harness no continúa la ejecución a ciegas:
   ```c
   g_csv_file = open_safe_csv_r5(g_csv_path, sizeof(g_csv_path));
   if (!g_csv_file) {
       fprintf(stderr, "[FATAL] Unable to open safe exclusive CSV target for benchmark logging. Aborting.\n");
       return 1;
   }
   ```
6. **Suite Unitaria Adversarial de 7 Casos:**
   Invocable mediante `./benchmark_decisive_suite --test-csv-errors`:
   - **Caso 1:** Creación exclusiva en ruta limpia (`CSV_OPEN_SUCCESS`).
   - **Caso 2:** Detección de colisión `EEXIST` y preservación bit a bit de contenido previo sin truncamiento.
   - **Caso 3:** Detección de directorio inexistente `ENOENT` (`CSV_OPEN_DIR_NOT_FOUND`).
   - **Caso 4:** Agotamiento de descriptores (`EMFILE`) y aborto fatal (`CSV_OPEN_FATAL_ERROR`).
   - **Caso 5:** Permiso denegado (`EACCES`) en directorio protegido y aborto fatal (`CSV_OPEN_FATAL_ERROR`).
   - **Caso 6:** Rotación y asignación segura de sufijos en `open_safe_csv_r5()`.
   - **Caso 7:** Validación de identidad distinguiendo dos archivos físicos distintos.

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
- [x] **3. Endurecimiento de Auditoría ISA (R9-01):** Script `scripts/audit_isa_isolation.sh` con flags sincronizadas 1:1 (`PORTABLE_CFLAGS` y `AVX2_CFLAGS`), eliminación de flags espurias, ausencia de `continue` fuera de bucle, exigencia de exit code 0, rechazo de salidas vacías y detección de bytes crudos.
- [x] **4. Creación Atómica Exclusiva CSV Robusta y Suite Adversarial (R9-02, R9-03):** Identidad verificada vía handle Win32 (`BY_HANDLE_FILE_INFORMATION`) y `fstat`/`lstat` POSIX, política defensiva de limpieza sin reclamos espurios de cero-TOCTOU, fallback restringido a `ENOENT`, aborto ante `EACCES`/`EMFILE`, suite de 7 casos `--test-csv-errors` y aborto fatal en `main()`.
- [x] **5. Corrección Aritmética y Taxonomía de Memoria:** Fórmulas exactas ($50.757.632\text{ B}$ propios), `ZSTD_estimateCStreamSize()` como estimador streaming oficial ($52.38\text{ MB}$ en L4 $\le 64\text{ MB}$; $117.87\text{ MB}$ en L19 $\le 128\text{ MB}$) y deslinde de las 4 categorías.
- [x] **6. Enforzamiento de Monohilo:** `opt.nb_workers = 1;` configurado explícitamente en todas las ejecuciones del harness.
- [ ] **7. Integración Continua Multiplataforma Certificada sobre R9 (R9-04):** Ejecución y verificación del workflow en GitHub Actions sobre el commit exacto de R9, validando la suite CSV en Linux y Windows, y la auditoría ISA en Linux.
- [ ] **8. Aprobación Formal de BP-008D:** Dictamen técnico favorable de la auditoría independiente.
- [ ] **9. Disponibilidad del Corpus:** Datasets descargados y verificados con sus respectivos hashes SHA-256 en disco.
- [ ] **10. Aislamiento de Hardware:** Sistema en reposo térmico ($T_{\text{CPU}} \le 45^\circ\text{C}$), sin procesos secundarios.

---

## 9. Criterio de Cierre del Hito BP-008D

El Hito **BP-008D** se considerará preparado para su **cierre técnico definitivo** una vez que el commit de R9 sea verificado en CI, confirmando:
1. El endurecimiento y trazabilidad estricta de [`scripts/audit_isa_isolation.sh`](file:///C:/Users/usuario/Desktop/binpack/scripts/audit_isa_isolation.sh) sin errores sintácticos de Bash.
2. La verificación de identidad de handle Win32 / POSIX y la suite de 7 casos CSV en [`tests/benchmarks/benchmark_decisive_suite.c`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c) ejecutada en CI en Linux y Windows.
3. La corrección aritmética de la memoria streaming bajo `ZSTD_estimateCStreamSize()`.
4. La desactivación irreversible de paralelismo automático mediante `opt.nb_workers = 1`.
5. La ausencia total de ejecución de la campaña de benchmarking de 20 iteraciones.
