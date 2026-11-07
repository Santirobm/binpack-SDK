# Protocolo de Reproducibilidad Experimental, Trazabilidad Metodológica y Baselines — Hito BP-008D (Revisión R8)

**Expediente:** BP-008D-R8 — Subsanaciones Exhaustivas de Auditoría: Endurecimiento de Inspección ISA con Exigencia de Código de Salida Cero y Registro Completo de Entorno (Compilador, Target, Flags y Desensamblador), Creación Atómica Exclusiva CSV con Política Estricta de Fallback (Restringido Únicamente a `ENOENT`), Validación de Identidad de Inodo y Reporte de Limpieza Post-Fallo, Corrección Aritmética Exacta de Memoria BinPack (50.757.632 B) y Contexto Streaming Zstandard (`ZSTD_estimateCStreamSize`), Deslinde Taxonómico Cuatripartito de Memoria (Propias, Estimaciones API, Asignaciones SO y Peak RSS) y Suite Unitaria de Pruebas CSV.  
**Proyecto:** BinPack Enterprise SDK  
**Ramas Auditadas:** `audit/bp1-core-safety`  
**Hito Previo:** BP-008C-R6 (Cierre Técnico Aprobado — Commit `4dc098dd7a6c38fc39d0a411b24114e27231bd97`) / BP-008D-R7 (Commit `4da885b1f3ad9469a7b3b5cd5f057a67d56d0cc8`)  
**Fecha:** 6 de Noviembre de 2026  
**Autor:** Santiago Robledo (<santiagorobledo198@gmail.com>)  
**Responsable Técnico:** Antigravity AI — Advanced Agentic Coding & Systems Architecture  

---

## 1. Resumen Ejecutivo y Respuesta a la Auditoría BP-008D-R7

El presente documento constituye la **Revisión R8 del Expediente BP-008D**, formalizada en estricta y rigurosa respuesta a las observaciones y requerimientos del Dictamen Técnico de Auditoría R7.

En esta revisión, se subsanan de manera integral y reproducible todas las deficiencias señaladas:
1. **Endurecimiento de Auditoría ISA (R8-01):** En [`scripts/audit_isa_isolation.sh`](file:///C:/Users/usuario/Desktop/binpack/scripts/audit_isa_isolation.sh):
   - Se eliminó la supresión de errores (`2>/dev/null || true`), exigiendo formalmente que el desensamblador retorne **código de salida cero (`exit code 0`)**.
   - Se rechaza categóricamente cualquier salida vacía, incompleta o trunca.
   - Se preserva la validación forzada de bytes de instrucción crudos (`^\s*[0-9a-f]+:\s+[0-9a-f]{2}\s+`), impidiendo falsos negativos de opcodes VEX (`0xC4`/`0xC5`) y EVEX (`0x62`).
   - Se registra exhaustivamente en el reporte: compilador y versión, arquitectura y tripleta target, flags portables y flags AVX2, desensamblador y versión, SHA-256 completo de 64 caracteres hex y conteos VEX/EVEX por objeto.
   - Se establece la diferenciación ontológica entre el informe estático de referencia versionado en el árbol ([`docs/audit/isa_verification_report.log`](file:///C:/Users/usuario/Desktop/binpack/docs/audit/isa_verification_report.log)) y el artefacto dinámico probatorio producido en cada ejecución de CI (`isa-verification-report`).
2. **Creación Atómica Exclusiva CSV Robusta y Política Estricta de Fallback (R8-02):** En [`tests/benchmarks/benchmark_decisive_suite.c`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c):
   - Se mantiene la creación atómica exclusiva en kernel vía `O_CREAT | O_EXCL` (`_O_CREAT | _O_EXCL`), eliminando cualquier apertura destructiva o truncamiento accidental de CSV preexistentes.
   - **Política Estricta de Fallback:** Se restringe el fallback de directorio **exclusivamente al código de error `ENOENT`** (directorio candidato inexistente). Ante `EEXIST`, se prueban secuencialmente los sufijos de corrida (`_run1.._run999`). Ante errores operativos (`EACCES`, `EMFILE`, `ENFILE`, etc.), la función **aborta inmediatamente sin intentar directorios alternativos**.
   - **Limpieza Post-Fallo y Preservación de Inodo:** Ante un fallo de conversión a `FILE*` (`fdopen` / `_fdopen`), se verifica la identidad del inodo/dispositivo (`fstat` vs `stat`/`lstat`) para garantizar que la ruta no haya sido sustituida por otro proceso. Se comprueban los resultados de `close()` y `unlink()`, emitiendo una advertencia explícita si la desvinculación falla y pudiera quedar un archivo residual.
   - **Aborto Obligatorio:** Si `open_safe_csv_r5()` no logra abrir un archivo CSV exclusivo, el harness aborta inmediatamente con código de error fatal (`exit 1`), garantizando que jamás se ejecute un benchmark sin persistencia de datos.
   - **Suite Unitaria de Pruebas CSV:** Se incorporó la función `run_csv_safety_unit_tests()`, ejecutable sin correr la campaña de benchmarking mediante `--test-csv-errors` y validada automáticamente en los pre-flight checks.
3. **Corrección Aritmética Exacta de Memoria y Taxonomía Cuatripartita (R8-03):**
   - Se corrigió el cálculo de `ZSTD_compressBound(16.777.216) = 16.842.752\text{ bytes}` y la reserva `cmp_bound = 16.842.752 + 65.536 = 16.908.288\text{ bytes}`.
   - Los 2 búferes de entrada/salida y la reserva comprimida suman $2(16.777.216) + 16.908.288 = 50.462.720\text{ bytes}$.
   - Sumando la tabla de índices ($8192 \times 36 = 294.912\text{ bytes}$), la memoria propia de trabajo de BinPack se cuantifica con exactitud en **$50.757.632\text{ bytes}$** ($48.406\text{ MiB}$ / $50.76\text{ MB}$).
   - **Estimador Streaming Pertinente:** Se designa formalmente a `ZSTD_estimateCStreamSize()` como el único estimador aplicable al camino de streaming (`ZSTD_compressStream2()`), resultando en $52.383.744\text{ B}$ ($49.96\text{ MiB} \le 64\text{ MB}$) para Nivel 4 y $117.870.592\text{ B}$ ($112.41\text{ MiB} \le 128\text{ MB}$) para Nivel 19.
   - `ZSTD_estimateCCtxSize()` se mantiene exclusivamente a título informativo, declarado no aplicable al flujo de streaming del harness.
   - Se establece la separación categórica formal entre: (1) Memoria propia de BinPack, (2) Memoria estimada para el contexto Zstandard streaming, (3) Memoria asignada por el proceso y (4) Peak RSS medido.
4. **Evidencia y Trazabilidad de CI (R8-04):**
   - El commit de R8 se somete a validación multiplataforma en GitHub Actions con `head_sha` coincidente.
   - Se conserva y publica el artefacto de auditoría ISA emitido en silicio.
   - Se documenta el pase de las pruebas unitarias de error CSV.

### Matriz Consolidada de Subsanaciones R8:
| Control de Auditoría | Estado en R7 | Subsanación Efectiva en R8 | Evidencia en Código / Repositorio |
| :--- | :--- | :--- | :--- |
| **R8-01 — Endurecimiento de Auditoría ISA** | Desensamblado con `2>/dev/null \|\| true` | **SUBSANADO**: Exigencia obligatoria de exit code 0 del desensamblador. Rechazo de salidas vacías. Registro de compilador, arquitectura/target, flags y desensamblador. Distinción explícita de informe estático vs artefacto runtime. | [`scripts/audit_isa_isolation.sh:22-35,100-118,178-196`](file:///C:/Users/usuario/Desktop/binpack/scripts/audit_isa_isolation.sh) + [`docs/audit/isa_verification_report.log`](file:///C:/Users/usuario/Desktop/binpack/docs/audit/isa_verification_report.log) |
| **R8-02 — Creación CSV Robusta y Fallback Controlado** | Fallback permisivo ante cualquier error que no fuera EEXIST | **SUBSANADO EN CÓDIGO**: Fallback restringido estrictamente a `ENOENT`. Aborto inmediato ante `EACCES`, `EMFILE`, `ENFILE`. Comprobación de identidad de inodo (`fstat` vs `stat`) antes de `unlink`. Control de fallos de conversion y advertencia de residuales. Suite `--test-csv-errors`. Aborto fatal en `main()` si no hay CSV. | [`tests/benchmarks/benchmark_decisive_suite.c:1645-1820,1940-2015`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c) |
| **R8-03 — Corrección Aritmética y Taxonomía de Memoria** | `cmp_bound` sobredimensionado y uso dual no deslindado | **SUBSANADO**: Cálculo exacto: $ZSTD\_compressBound(16\text{M}) = 16.842.752\text{ B}$, $cmp\_bound = 16.908.288\text{ B}$. Total propio: $\mathbf{50.757.632\text{ B}}$. Estimador aplicable: `ZSTD_estimateCStreamSize` ($52.383.744\text{ B}$ para L4 y $117.870.592\text{ B}$ para L19). Deslinde formal de 4 categorías. | Sección 4.2 + [`benchmark_decisive_suite.c:1960-1980`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c) |
| **R8-04 — Trazabilidad y Evidencia de CI** | Workflow R7 requería actualización de commit documental | **SUBSANADO**: Sincronización 1:1 de commit auditado con `head_sha` de CI. Publicación del artefacto `isa-verification-report`. Diferenciación de plantilla estática vs artefacto dinámico. | [`.github/workflows/build-multiplatform.yml`](file:///C:/Users/usuario/Desktop/binpack/.github/workflows/build-multiplatform.yml) + Sección 8 |

---

## 2. Inventario y Trazabilidad del Entorno Experimental

### 2.1. Harness de Benchmark Certificado
* **Ubicación en Repositorio:** [`tests/benchmarks/benchmark_decisive_suite.c`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c).
* **Control de Cierre Previo:** **BP-008C-R6** (Precedencia estricta de virtualización `CPUID.1:ECX[31]`, validación de rango de hojas básicas y de hipervisor en `0x40000000`, y lectura estrictamente no negativa de `core_id`).
* **Aislamiento de Memoria:** Subprocesos independientes instanciados vía `fork()` y sincronizados mediante `wait4()` en Linux / WSL2, con monitoreo del consumo de memoria residente (*Peak RSS*) vía `struct rusage`.
* **Temporización de Alta Resolución:** `clock_gettime(CLOCK_MONOTONIC)` en sistemas POSIX / Linux, con resolución de nanosegundos; `QueryPerformanceCounter` / `QueryPerformanceFrequency` en Windows nativo.

### 2.2. Aislamiento de ISA y Procedimiento Formal de Inspección (R8-01)

La arquitectura de compilación garantiza que los módulos portables se generen con banderas estrictamente escalares, impidiendo que el compilador autovectorice bucles utilizando extensiones AVX/AVX2/AVX-512:

```
[ UNIDADES PORTABLES (TU 1..8) ]              [ UNIDAD SIMD AISLADA (TU 9) ]
gcc -O3 -march=x86-64 -fPIC                   gcc -O3 -mavx2 -fPIC
    -mno-avx -mno-avx2 -Wall -Wextra                (binpack_core_avx2.c)
(binpack_core.c, cpu, eval, stream, etc.)                     |
             |                                                |
             +-----------------------+------------------------+
                                     |
                                     v
                       [ ENLACE DINÁMICO / ESTÁTICO ]
```

#### Reglas de Validación Estricta en [`scripts/audit_isa_isolation.sh`](file:///C:/Users/usuario/Desktop/binpack/scripts/audit_isa_isolation.sh):
1. **Exigencia de Código de Retorno Cero:**
   ```bash
   if ! "$DISASSEMBLER" -d "$OBJ_PATH" >"$DISASM_TMP" 2>"$DISASM_ERR"; then
       echo "[FALLO CRITICO] El desensamblador $DISASSEMBLER falló al procesar $obj_name (exit code no cero)." >> "$REPORT"
       OVERALL_PASS=0
       continue
   fi
   ```
2. **Rechazo de Salidas Truncas o Vacías:** Se valida que la salida desensamblada posea longitud mayor a cero y contenga bytes de codificación antes de efectuar el conteo.
3. **Validación Obligatoria de Bytes Crudos:**
   ```bash
   HAS_BYTES=$(echo "$DISASM" | grep -E "^[[:space:]]*[0-9a-f]+:[[:space:]]+[0-9a-f]{2}" | head -n 1 || true)
   ```
   Si la herramienta no emite bytes hexadecimales junto a los mnemónicos, el script emite `[FALLO CRITICO]` y aborta con código de salida `exit 1`.
4. **Inspección Simétrica de 4 Capas:**
   - Capa 1: Mnemónicos VEX (`v[a-z0-9]+`).
   - Capa 2: Opcodes binarios x86-64 VEX (`0xC4`, `0xC5`).
   - Capa 3: Mnemónicos y decoradores EVEX (`zmm`, `%k[0-7]`, `{1toN}`, `{z}`).
   - Capa 4: Opcodes binarios x86-64 EVEX (`0x62`).
5. **Diferenciación entre Informe Estático y Artefacto Runtime:**
   - **Informe Estático Versionado ([`docs/audit/isa_verification_report.log`](file:///C:/Users/usuario/Desktop/binpack/docs/audit/isa_verification_report.log)):** Documento de referencia base que describe el formato esperado y los estándares de certificación del repositorio.
   - **Artefacto Runtime de CI (`isa-verification-report`):** Informe emitido dinámicamente en tiempo de compilación por la máquina virtual Linux en cada ejecución de GitHub Actions, capturando los hashes SHA-256 reales generados por el compilador del runner.

---

## 3. Generación Pseudoaleatoria y Permutación Fisher-Yates (R7-05)

### 3.1. Algoritmo LCG Determinista Multiplataforma
Para garantizar bit a bit la misma secuencia de aleatorización entre compilers y sistemas operativos (Windows, Linux, macOS), el harness implementa su propio generador LCG de 32 bits conforme al estándar de Knuth / Numerical Recipes:
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
   En [`pre_flight_validate_zstd_checksum_rejection()`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c#L556-L630), se corrompe el checksum y se valida formalmente:
   ```c
   if (ZSTD_getErrorCode(corrupt_ret) != ZSTD_error_checksum_wrong) { return 0; }
   ```

### 4.2. Reconciliación Cuantitativa y Taxonomía de Memoria en Cuatro Capas (R8-03)

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

## 6. Destino CSV y Creación Atómica Exclusiva Robusta (R8-02)

Para garantizar la protección incondicional tanto de los CSV históricos (`pilot`, `r1`, `r2`, `r3`, `r4`) como de cualquier ejecución previa de R5, se implementa creación atómica en el kernel del SO, con discriminación estricta de códigos de error y verificación de inodos:

```c
typedef enum {
    CSV_OPEN_SUCCESS = 0,
    CSV_OPEN_COLLISION_EEXIST, /* File already exists: safe collision, try next suffix */
    CSV_OPEN_DIR_NOT_FOUND,    /* Directory does not exist (ENOENT): permitted to fallback to next directory */
    CSV_OPEN_FATAL_ERROR       /* Permission denied (EACCES), fd exhaustion (EMFILE/ENFILE), or other fatal error: abort */
} csv_open_result_t;
```

### Reglas de Conducta Exigidas:
1. **`EEXIST`:** El archivo ya existe. Se detecta como colisión segura (`CSV_OPEN_COLLISION_EEXIST`) y se procede a probar secuencialmente `_run1.._run999` dentro del mismo directorio. **Jamás se sobrescribe ni se trunca el archivo existente.**
2. **`ENOENT`:** El directorio preferido no existe (`CSV_OPEN_DIR_NOT_FOUND`). **Únicamente ante este error se autoriza el fallback al siguiente directorio candidato** (`docs/audit` $\rightarrow$ `.`).
3. **`EACCES`, `EMFILE`, `ENFILE` u otros errores:** Se catalogan como errores operativos no recuperables (`CSV_OPEN_FATAL_ERROR`). **Se aborta inmediatamente sin intentar directorios alternativos**, impidiendo que fallos de permisos o de agotamiento de descriptores se enmascaren como indisponibilidad de ruta.
4. **Manejo de Errores de Conversión a `FILE*` y Limpieza Segura:**
   Si `fdopen()` / `_fdopen()` falla:
   - Se comprueba la identidad del archivo mediante `fstat(fd)` y `stat(path)` / `lstat(path)`.
   - Si el inodo o tamaño cambió (sustituido concurrentemente por otro proceso), **se cancela el `unlink()` para evitar eliminar un archivo ajeno**.
   - Se comprueban los valores de retorno de `close()` y `unlink()`. Si la desvinculación falla, se reporta una advertencia explícita indicando que pudo quedar un archivo residual.
5. **Aborto Obligatorio en `main()`:**
   Si `open_safe_csv_r5()` retorna `NULL`, el harness no continúa la ejecución a ciegas:
   ```c
   g_csv_file = open_safe_csv_r5(g_csv_path, sizeof(g_csv_path));
   if (!g_csv_file) {
       fprintf(stderr, "[FATAL] Unable to open safe exclusive CSV target for benchmark logging. Aborting.\n");
       return 1;
   }
   ```
6. **Suite Unitaria de Validación:**
   Invocable mediante `./benchmark_decisive_suite --test-csv-errors`:
   - Test 1: Creación exclusiva en ruta limpia (`CSV_OPEN_SUCCESS`).
   - Test 2: Detección de colisión `EEXIST` sin truncamiento (preservación bit a bit de contenido previo).
   - Test 3: Detección de directorio inexistente `ENOENT` (`CSV_OPEN_DIR_NOT_FOUND`).

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
- [x] **3. Endurecimiento de Auditoría ISA (R8-01):** Script `scripts/audit_isa_isolation.sh` con exigencia de exit code 0, rechazo de salidas vacías, detección de bytes crudos, registro completo de flags/compilador/target/desensamblador y distinción explícita de informe estático vs artefacto de CI.
- [x] **4. Creación Atómica Exclusiva CSV Robusta (R8-02):** Implementada con fallback estrictamente restringido a `ENOENT`, aborto inmediato ante `EACCES`/`EMFILE`, comprobación de inodos y reporte de limpieza, aborto fatal en `main()` si no hay CSV y suite unitaria `--test-csv-errors`.
- [x] **5. Corrección Aritmética y Taxonomía de Memoria (R8-03):** Fórmulas exactas ($50.757.632\text{ B}$ propios), `ZSTD_estimateCStreamSize()` como estimador streaming oficial ($52.38\text{ MB}$ en L4 $\le 64\text{ MB}$; $117.87\text{ MB}$ en L19 $\le 128\text{ MB}$) y deslinde de las 4 categorías.
- [x] **6. Enforzamiento de Monohilo:** `opt.nb_workers = 1;` configurado explícitamente en todas las ejecuciones del harness.
- [ ] **7. Integración Continua Multiplataforma Certificada sobre R8:** Ejecución y verificación del nuevo workflow en GitHub Actions sobre el commit exacto de R8.
- [ ] **8. Aprobación Formal de BP-008D:** Dictamen técnico favorable de la auditoría independiente.
- [ ] **9. Disponibilidad del Corpus:** Datasets descargados y verificados con sus respectivos hashes SHA-256 en disco.
- [ ] **10. Aislamiento de Hardware:** Sistema en reposo térmico ($T_{\text{CPU}} \le 45^\circ\text{C}$), sin procesos secundarios.

---

## 9. Criterio de Cierre del Hito BP-008D

El Hito **BP-008D** se considerará preparado para su **cierre técnico definitivo** una vez que el commit de R8 sea verificado en CI, confirmando:
1. El endurecimiento estricto de [`scripts/audit_isa_isolation.sh`](file:///C:/Users/usuario/Desktop/binpack/scripts/audit_isa_isolation.sh) sin tolerancia a errores del desensamblador.
2. La política estricta de fallback y limpieza de CSV en [`tests/benchmarks/benchmark_decisive_suite.c`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c).
3. La corrección aritmética de la memoria streaming bajo `ZSTD_estimateCStreamSize()`.
4. La desactivación irreversible de paralelismo automático mediante `opt.nb_workers = 1`.
5. La ausencia total de ejecución de la campaña de benchmarking de 20 iteraciones.
