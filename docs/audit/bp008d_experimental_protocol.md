# Protocolo de Reproducibilidad Experimental, Trazabilidad Metodológica y Baselines — Hito BP-008D (Revisión R11)

**Expediente:** BP-008D-R11 — Cierre de Brechas Probatorias en Suite Adversarial CSV: Determinismo EMFILE en UCRT/POSIX con Verificación de Límites y Error de Kernel (`errno == EMFILE`), Inducción Real de EACCES con Win32 `icacls` Verificado (Exit Code 0) y Comprobación Forzada de `errno == EACCES`, Preservación Binaria Bit a Bit (Payloads Binarios de 256 B, `fwrite`/`fread`/`memcmp`), Sustitución Concurrente Real de Rutas en Ambas Plataformas (Win32 `FILE_SHARE_DELETE` + `DeleteFileA` y POSIX `unlink`, Rutina Compartida `defensive_cleanup_failed_exclusive_fd` con Supresión Demostrada de Borrado Ajeno) y Semántica Estricta 7/7 en CI Multiplataforma (Linux y Windows).  
**Proyecto:** BinPack Enterprise SDK  
**Ramas Auditadas:** `audit/bp1-core-safety`  
**Hito Previo:** BP-008D-R10 (Commit `e637cef6ee22246478ca55129c0802cc3edc8b62`, Run `42338549776`)  
**Fecha:** 7 de Noviembre de 2026  
**Autor:** Santiago Robledo (<santiagorobledo198@gmail.com>)  
**Responsable Técnico:** Antigravity AI — Advanced Agentic Coding & Systems Architecture  

---

## 1. Resumen Ejecutivo y Respuesta a la Auditoría BP-008D-R10

El presente documento constituye la **Revisión R11 del Expediente BP-008D**, formalizada en respuesta directa a las observaciones formuladas en el Dictamen Técnico de Auditoría R10.

En esta revisión se cierran categóricamente las brechas probatorias identificadas en los escenarios de prueba adversarial:

1. **Determinismo Estricto en Agotamiento de Descriptores (`EMFILE`, R11-01):**
   - **POSIX:** Se valida explícitamente el valor de retorno de `getrlimit()` y `setrlimit()`. Se agota la tabla hasta inducir `fd < 0` con `errno == EMFILE`. Al invocar `open_atomic_exclusive_csv()`, se verifica que retorne `NULL`, `res == CSV_OPEN_FATAL_ERROR` **y** que `errno == EMFILE`. Finalmente se valida la restauración estricta del límite original (`setrlimit(orig) == 0`).
   - **Windows (UCRT):** Se comprueba que `_setmaxstdio(512)` retorne exactamente 512. Se explica y demuestra que la capacidad de descriptores de bajo nivel de la UCRT de Windows asciende a 8.192 descriptores (`_IOB_ENTRIES` / `_pioinfo`); por ello, un bucle de 8.300 descriptores agota la tabla alcanzando `_open() == -1` con `errno == EMFILE` (24) de forma determinista (~8.189 descriptores). Se comprueba que `open_atomic_exclusive_csv()` falle con `CSV_OPEN_FATAL_ERROR` y `errno == EMFILE`. Se cierran todos los descriptores y se valida que la restauración de `_setmaxstdio(orig_max)` retorne `orig_max`.
2. **Inducción Real y Verificable de Permiso Denegado (`EACCES`, R11-02):**
   - **Windows:** Se erradica la dependencia de shells interactivos o variables `COMSPEC` invocando directamente `C:\Windows\System32\icacls.exe` mediante `CreateProcessA()`. Se valida que la creación del proceso sea exitosa (`cp_ok != FALSE`) y que el código de terminación devuelto por `GetExitCodeProcess()` sea estrictamente `0` tras aplicar la ACE denegatoria `*S-1-1-0:(W)` (Everyone Deny Write) sobre un directorio temporal. Se intenta la creación de un archivo inexistente, verificando que `open_atomic_exclusive_csv()` falle con `CSV_OPEN_FATAL_ERROR` **y** que preserve de forma forzada `errno == EACCES` (13). Se retira la ACE mediante `icacls ... /remove:d *S-1-1-0` y se elimina el directorio.
   - **POSIX:** Se crea un directorio restrictivo con `mkdir(..., 0555)`, comprobando que `open_atomic_exclusive_csv()` retorne `CSV_OPEN_FATAL_ERROR` y `errno == EACCES`.
3. **Preservación Binaria Completa y Agotamiento de Sufijos (R11-03):**
   - Se reemplaza la validación basada en líneas de texto (`fgets`) por validación binaria de bajo nivel. Se generan buffers pseudoaleatorios deterministas de 256 bytes (incluyendo bytes nulos y valores arbitrarios `0x00..0xFF`) para el archivo base y tres sufijos (`_run1`, `_run2`, `_run3`).
   - Se escriben mediante `fwrite(..., 1, 256, ...)` en modo binario (`"wb"`).
   - Se ejecuta `open_safe_csv_r5_ex()` y se verifica la asignación exacta de `_run4.csv`.
   - Se valida la integridad bit a bit abriendo el archivo base y cada uno de los tres sufijos en modo `"rb"`, verificando la longitud exacta de 256 bytes (`fseek(..., SEEK_END)` / `ftell() == 256`), leyendo el bloque completo (`fread() == 256`) y comparando la memoria contra el patrón original mediante `memcmp(..., 256) == 0`.
   - Se comprueba la detección determinista de agotamiento (`max_runs = 3` sobre 3 sufijos existentes), verificando que la función aborta limpiamente (`NULL`) sin alterar los archivos preexistentes.
4. **Sustitución Concurrente Real de Ruta en Ambas Plataformas (R11-04):**
   - Se extrae la rutina compartida de saneamiento defensivo:  
     `static int defensive_cleanup_failed_exclusive_fd(int fd, const char *path)`  
     utilizada idénticamente por `open_atomic_exclusive_csv()` ante fallos de `fdopen()`/`_fdopen()` y por la prueba adversarial Test 7.
   - **POSIX:** Se crea el archivo original de forma exclusiva (`open(O_RDWR | O_CREAT | O_EXCL)`), se desvincula de la ruta con `unlink(path)` manteniendo abierto el descriptor `orig_fd`, y se crea un archivo ajeno sustituto en la **misma ruta**. La comparación de `st_dev` y `st_ino` detecta la sustitución (`same_file == 0`), `defensive_cleanup_failed_exclusive_fd()` suprime el borrado (retorna 0), y se certifica que el archivo ajeno permanece íntegro en disco (`memcmp == 0`).
   - **Windows:** Se crea el archivo original mediante `CreateFileA()` con `FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE` y se asocia a un descriptor CRT vía `_open_osfhandle()`. Esto permite ejecutar `DeleteFileA(path)` mientras `orig_fd` permanece abierto, posibilitando la creación inmediata de un archivo sustituto ajeno con contenido binario en la **misma ruta**. La comparación a nivel de kernel de `BY_HANDLE_FILE_INFORMATION` (`dwVolumeSerialNumber`, `nFileIndexHigh`, `nFileIndexLow`) detecta la discrepancia (`same_file == 0`), `defensive_cleanup_failed_exclusive_fd()` suprime el `_unlink` (retorna 0), y se certifica que el archivo ajeno permanece íntegro en disco (`memcmp == 0`).
   - Se reitera y documenta la limitación insalvable de TOCTOU en operaciones de borrado basadas en ruta en espacio de usuario.
5. **Semántica Estricta y Registro Exhaustivo en CI (R11-05):**
   - Contadores independientes de `Passed`, `Failed` y `Skipped` con exigencia formal de `7/7` aprobados sin omisiones.
   - Registro detallado en los runners de GitHub Actions tanto en Linux x86_64 como en Windows x86_64.

### Matriz Consolidada de Subsanaciones R11:
| Control de Auditoría | Estado en R10 | Subsanación Efectiva en R11 | Evidencia en Código / Repositorio |
| :--- | :--- | :--- | :--- |
| **R11-01 — EMFILE Determinista** | `_setmaxstdio(512)` sin validar retorno; límite UCRT no documentado | **SUBSANADO**: Validación de `_setmaxstdio(512) == 512`. Agotamiento de 8.192 descriptores UCRT (`fds[8300]`) alcanzando `EMFILE`. Verificación forzada de `errno == EMFILE` en `open_atomic_exclusive_csv()`. Restauración verificada de `orig_max`. POSIX análogo con `rlimit`. | [`tests/benchmarks/benchmark_decisive_suite.c`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c) |
| **R11-02 — EACCES Real Verificable** | `CreateProcessA` sin validar exit code; no se verificaba `errno == EACCES` | **SUBSANADO**: `CreateProcessA` llamando a `icacls.exe /deny *S-1-1-0:(W)`. Comprobación de retorno de proceso y `GetExitCodeProcess == 0`. Comprobación forzada de `CSV_OPEN_FATAL_ERROR` **y** `errno == EACCES` (13). Limpieza con `/remove:d`. POSIX `0555` análogo. | [`tests/benchmarks/benchmark_decisive_suite.c`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c) |
| **R11-03 — Preservación Binaria** | Lectura basada en `fgets` y strings ASCII | **SUBSANADO**: Payloads binarios de 256 bytes (`0x00..0xFF`). Modos `"wb"` / `"rb"`. Verificación estricta de tamaño (`ftell == 256`), bloque (`fread == 256`) y comparación bit a bit (`memcmp == 0`). Verificación de aborto por agotamiento (`max_runs = 3`). | [`tests/benchmarks/benchmark_decisive_suite.c`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c) |
| **R11-04 — Sustitución Real en Ambas Plataformas** | En Windows se usaba ruta secundaria; lógica de limpieza no modular | **SUBSANADO**: Extracción de función compartida `defensive_cleanup_failed_exclusive_fd()`. En Windows, `FILE_SHARE_DELETE` + `_open_osfhandle` permite `DeleteFileA` sobre archivo abierto y recreación de archivo ajeno en la **misma ruta**. Kernel detecta discrepancia, suprime borrado y preserva archivo ajeno. | [`tests/benchmarks/benchmark_decisive_suite.c`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c) |
| **R11-05 — Semántica Estricta 7/7** | Verificación en CI con ejecución de los 7 casos sin skips | **SUBSANADO**: 7/7 casos ejecutados en Linux y Windows, sin skips ni tolerancias. | [`.github/workflows/build-multiplatform.yml`](file:///C:/Users/usuario/Desktop/binpack/.github/workflows/build-multiplatform.yml) |

---

## 2. Inventario y Trazabilidad del Entorno Experimental

### 2.1. Harness de Benchmark Certificado
* **Ubicación en Repositorio:** [`tests/benchmarks/benchmark_decisive_suite.c`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c).
* **Control de Cierre Previo:** **BP-008C-R6** (Precedencia estricta de virtualización `CPUID.1:ECX[31]`, validación de rango de hojas básicas y de hipervisor en `0x40000000`, y lectura estrictamente no negativa de `core_id`).
* **Aislamiento de Memoria:** Subprocesos independientes instanciados vía `fork()` y sincronizados mediante `wait4()` en Linux / WSL2, con monitoreo del consumo de memoria residente (*Peak RSS*) vía `struct rusage`.
* **Temporización de Alta Resolución:** `clock_gettime(CLOCK_MONOTONIC)` en sistemas POSIX / Linux, con resolución de nanosegundos; `QueryPerformanceCounter` / `QueryPerformanceFrequency` en Windows nativo.

### 2.2. Aislamiento de ISA y Procedimiento Formal de Inspección (Aprobado en R9 y R10)

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

---

## 3. Generación Pseudoaleatoria y Permutación Fisher-Yates

### 3.1. Algoritmo LCG Determinista Multiplataforma
Para garantizar bit a bit la misma secuencia de aleatorización entre compiladores y sistemas operativos (Windows, Linux, macOS), el harness implementa su propio generador LCG de 32 bits conforme al estándar de Knuth / Numerical Recipes:
$$X_{n+1} = (1664525 \times X_n + 1013904223) \bmod 2^{32}$$

### 3.2. Reducción Lemire sin Sesgo y Análisis de Correlación:
1. **Eliminación del Sesgo de Partición Modular:**
   Lemire calcula $m = X \times \text{bound}$ y rechaza los valores residuales en $t = (-b) \pmod b$, garantizando que cada índice en $[0, \text{bound}-1]$ tenga exactamente $\lfloor 2^{32} / \text{bound} \rfloor$ preimágenes bajo entradas uniformes.
2. **Propagación de Acarreos y Ausencia de Independencia Estadística:**
   La mitad superior del producto depende matemáticamente de los bits inferiores a través de los acarreos aritméticos de la multiplicación de 64 bits. Lemire elimina el sesgo de mapeo, pero **no suprime las correlaciones seriales intrínsecas del LCG**.
3. **Validez Matemática de la Semilla Cero (`--seed 0`):**
   Conforme al Teorema de Hull-Dobell, para el generador LCG adoptado el período es estrictamente $2^{32}$ para cualquier semilla en $[0, 2^{32}-1]$, incluyendo el estado inicial $0$.

---

## 4. Matriz de Comparabilidad, Memoria y Contratos de Integridad

### 4.1. Verificación Activa del Checksum de Zstandard
1. **Configuración de Checksum:** `ZSTD_CCtx_setParameter(cctx, ZSTD_c_checksumFlag, 1);` activo en todos los runners de Zstandard.
2. **Inspección de Encabezado FHD:** Se valida en caliente que el bit 2 (`Content_Checksum_flag`) esté activo en el byte 4 del frame (`(comp_buf[4] & 0x04) != 0`).
3. **Pre-flight con Comprobación Específica de Error:**
   En [`pre_flight_validate_zstd_checksum_rejection()`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c), se corrompe el checksum y se valida formalmente `ZSTD_error_checksum_wrong`.

### 4.2. Reconciliación Cuantitativa y Taxonomía de Memoria en Cuatro Capas
* **Total Búferes Propios BinPack:**
  $$2(16.777.216) + 16.908.288 + 294.912 = \mathbf{50.757.632\text{ bytes}} \quad (48.406\text{ MiB} \approx 50.76\text{ MB})$$
* **Estimador Streaming Pertinente:** `ZSTD_estimateCStreamSize()`:
  - Runtime Mode (L4): $52.383.744\text{ B}$ ($49.96\text{ MiB} \le 64.0\text{ MB}$).
  - Default Mode (L19): $117.870.592\text{ B}$ ($112.41\text{ MiB} \le 128.0\text{ MB}$).

---

## 5. Procedimiento de Validación y Análisis Estadístico Dual

* **Tendencia Central:** Mediana cruda ($\text{Mediana}_{\text{raw}}$) y Mediana filtrada tras Tukey ($\text{Mediana}_{\text{filt}}$).
* **Dispersión:** IQR crudo y filtrado ($\text{IQR}_{\text{raw}}$, $\text{IQR}_{\text{filt}}$), Coeficiente de Variación ($\text{CV}_{\text{raw}}$, $\text{CV}_{\text{filt}}$).
* **Auditoría de Outliers:** Conteo exacto $N_{\text{out}}$ y porcentaje de descarte.
* **Criterios de Estabilidad:** $\text{CV}_{\text{filt}} \le 5.0\%$, $\text{CV}_{\text{raw}} \le 10.0\%$, $N_{\text{out}} \le 2$.

---

## 6. Destino CSV, Identidad a Nivel de Kernel y Suite Adversarial (R11-01 a R11-05)

### Reglas de Conducta Exigidas y Manejo de Identidad:
1. **`EEXIST`:** El archivo ya existe. Se detecta como colisión segura (`CSV_OPEN_COLLISION_EEXIST`) y se procede a probar secuencialmente `_run1.._run999` dentro del mismo directorio. **Jamás se sobrescribe ni se trunca el archivo existente.**
2. **`ENOENT`:** El directorio preferido no existe (`CSV_OPEN_DIR_NOT_FOUND`). **Únicamente ante este error se autoriza el fallback al siguiente directorio candidato** (`docs/audit` $\rightarrow$ `.`).
3. **`EACCES`, `EMFILE`, `ENFILE` u otros errores:** Se catalogan como errores operativos no recuperables (`CSV_OPEN_FATAL_ERROR`). **Se aborta inmediatamente sin intentar directorios alternativos**.
4. **Manejo de Errores de Conversión a `FILE*` y Limpieza Defensiva Compartida:**
   Si `fdopen()` / `_fdopen()` falla:
   - Se invoca la función modular `defensive_cleanup_failed_exclusive_fd(int fd, const char *path)`.
   - Se comprueba la identidad física del archivo:
     - En **Windows**, mediante `_get_osfhandle(fd)` y `CreateFileA()` para comparar `BY_HANDLE_FILE_INFORMATION` (`dwVolumeSerialNumber`, `nFileIndexHigh`, `nFileIndexLow`).
     - En **POSIX**, mediante `fstat(fd)` y `lstat(path)` para comparar `st_dev` y `st_ino`.
   - **Política Defensiva:** Si la identidad no coincide o no puede ser verificada positivamente, **se suprime el intento de desvinculación para no afectar un archivo ajeno**.
   - Si la desvinculación falla o se cancela, se emite una advertencia formal indicando que ha quedado un archivo residual inerte.
   - Se reconoce formalmente la existencia de la ventana TOCTOU inherente a operaciones de borrado basadas en ruta, implementando la política defensiva más estricta posible en el espacio de usuario.
5. **Aborto Obligatorio en `main()`:**
   Si `open_safe_csv_r5()` retorna `NULL`, el harness aborta fatalmente con código de salida `1`.
6. **Suite Unitaria Adversarial de 7 Casos sin Omisiones (R11-05):**
   - **Caso 1:** Creación limpia exclusiva (`CSV_OPEN_SUCCESS`).
   - **Caso 2:** Detección de colisión `EEXIST` y preservación bit a bit sin truncamiento.
   - **Caso 3:** Detección de directorio inexistente `ENOENT` y autorización de fallback.
   - **Caso 4:** Agotamiento determinista de descriptores (`EMFILE`) verificado con retornos de kernel, límites restaurados y comprobación forzada de `errno == EMFILE`.
   - **Caso 5:** Inducción real de `EACCES` mediante directorio con DACL restrictiva en Windows (`icacls` con exit code 0) y `0555` en POSIX, con verificación de `errno == EACCES`.
   - **Caso 6:** Rotación verificada sobre sufijos preexistentes (`_run1.._run3`), selección de `_run4`, comprobación binaria bit a bit (`memcmp == 0`) y detección de agotamiento.
   - **Caso 7:** Identidad a nivel de kernel y preservación demostrada de archivo ajeno ante sustitución concurrente de ruta en Windows y POSIX mediante `defensive_cleanup_failed_exclusive_fd()`.

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
- [x] **3. Aislamiento ISA Aprobado (R9-01 / R10):** Script `scripts/audit_isa_isolation.sh` con flags sincronizadas 1:1 (`PORTABLE_CFLAGS` y `AVX2_CFLAGS`), sin `continue` fuera de bucle, con exigencia de exit code 0 y detección de bytes crudos.
- [x] **4. Creación Atómica Exclusiva CSV Robusta y Suite Adversarial Estricta (R11-01 a R11-05):** Determinismo EMFILE comprobado con `errno == EMFILE`, EACCES real demostrado con `icacls` exit code 0 y `errno == EACCES`, rotación y agotamiento de sufijos con integridad binaria de 256 bytes (`memcmp`), sustitución concurrente demostrada en Windows y POSIX con supresión de borrado de archivos ajenos vía `defensive_cleanup_failed_exclusive_fd()`, y semántica estricta 7/7 sin skips.
- [x] **5. Corrección Aritmética y Taxonomía de Memoria:** Fórmulas exactas ($50.757.632\text{ B}$ propios), `ZSTD_estimateCStreamSize()` como estimador streaming oficial ($52.38\text{ MB}$ en L4 $\le 64\text{ MB}$; $117.87\text{ MB}$ en L19 $\le 128\text{ MB}$) y deslinde de las 4 categorías.
- [x] **6. Enforzamiento de Monohilo:** `opt.nb_workers = 1;` configurado explícitamente en todas las ejecuciones del harness.
- [ ] **7. Integración Continua Multiplataforma Certificada sobre R11 (R11-05):** Ejecución y verificación del workflow en GitHub Actions sobre el commit exacto de R11, validando la suite CSV 7/7 en Linux y Windows, y la auditoría ISA en Linux.
- [ ] **8. Aprobación Formal de BP-008D:** Dictamen técnico favorable de la auditoría independiente.
- [ ] **9. Disponibilidad del Corpus:** Datasets descargados y verificados con sus respectivos hashes SHA-256 en disco.
- [ ] **10. Aislamiento de Hardware:** Sistema en reposo térmico ($T_{\text{CPU}} \le 45^\circ\text{C}$), sin procesos secundarios.

---

## 9. Criterio de Cierre del Hito BP-008D

El Hito **BP-008D** se considerará preparado para su **cierre técnico definitivo** una vez que el commit de R11 sea verificado en CI, confirmando:
1. El aislamiento de instrucciones SIMD verificado y aprobado.
2. La suite adversarial CSV ejecutada y aprobada con 7/7 casos estrictos sin omisiones en Linux y Windows.
3. La corrección aritmética de la memoria streaming bajo `ZSTD_estimateCStreamSize()`.
4. La desactivación irreversible de paralelismo automático mediante `opt.nb_workers = 1`.
5. La ausencia total de ejecución de la campaña de benchmarking de 20 iteraciones.
