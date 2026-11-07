# Protocolo de Reproducibilidad Experimental, Trazabilidad Metodológica y Baselines — Hito BP-008D (Revisión R12)

**Expediente:** BP-008D-R12 — Cierre Definitivo de Evidencias en Suite Adversarial CSV: Distinción Formal entre Límite de Streams (`_setmaxstdio`, 512..2048) y Límite UCRT de Descriptores de Bajo Nivel (`_open`, 8192) con Registro en Runtime de `n_fds`, Endurecimiento de `EACCES` con Validación Completa de Procesos Win32 (`WaitForSingleObject`, `GetExitCodeProcess`, Retirada Verificada de ACE y Directorio Fresco sin `ERROR_ALREADY_EXISTS`) y Verificación de Restauración POSIX (`chmod`/`rmdir`), Endurecimiento de E/S Binaria con Comprobación Exhaustiva de Retornos (`fwrite`, `fclose`, `fseek`, `ftell`, `fread`, `memcmp`), Trazabilidad Corregida de CI y Documentación Expresa de la Ventana TOCTOU Residual.  
**Proyecto:** BinPack Enterprise SDK  
**Ramas Auditadas:** `audit/bp1-core-safety`  
**Hito Previo:** BP-008D-R11 (Commit `84afb61f548179fdc4d8b4c666198ef7174142b7`, Workflow Runs `42340324059` y `42340320491`)  
**Fecha:** 7 de Noviembre de 2026  
**Autor:** Santiago Robledo (<santiagorobledo198@gmail.com>)  
**Responsable Técnico:** Antigravity AI — Advanced Agentic Coding & Systems Architecture  

---

## 1. Resumen Ejecutivo y Respuesta a la Auditoría BP-008D-R11

El presente documento constituye la **Revisión R12 del Expediente BP-008D**, formalizada en estricta y rigurosa respuesta a las observaciones y requerimientos del Dictamen Técnico de Auditoría R11.

En esta revisión se subsanan los aspectos probatorios y documentales señalados en la orden de trabajo BP-008D-R12:

1. **Cierre de Evidencia en Agotamiento de Descriptores (`EMFILE`, R12-01):**
   - **Distinción Formal entre Límite de Streams y Descriptores de Bajo Nivel:**
     - En la arquitectura de Microsoft Windows Universal CRT (UCRT), la función `_setmaxstdio()` gobierna exclusivamente el límite de flujos estándar `FILE*` de alto nivel (I/O stream buffer pool), el cual posee un valor predeterminado de 512 y un techo máximo configurable de 2.048 flujos simultáneos.
     - Por el contrario, las primitivas de E/S de bajo nivel de estilo POSIX (`_open`, `_close`, `_read`, `_write`) operan sobre una tabla interna de descriptores indexada por punteros en la estructura interna `_pioinfo` (un arreglo de punteros que aloja bloques de 32 descriptores hasta un máximo fijo de 8.192 descriptores en UCRT moderna).
     - Por lo tanto, `_setmaxstdio(512)` configura y valida la capacidad a nivel de streams de alto nivel, mientras que el bucle de agotamiento mediante `_open("NUL", _O_RDONLY)` sobre un arreglo de 8.300 elementos agota de forma determinista el pool de 8.192 descriptores de bajo nivel.
   - **Registro en Runtime de la Cifra Concreta de Descriptores:**
     - Se imprime explícitamente en `stdout` el número exacto de descriptores alcanzados (`n_fds`) al momento de producirse la saturación, junto con el código de error `errno == EMFILE` (24) del descriptor que falla.
     - Se verifica en caliente que `open_atomic_exclusive_csv()` falle retornando `CSV_OPEN_FATAL_ERROR` y `errno == EMFILE`.
     - Se comprueba la restauración obligatoria `_setmaxstdio(orig_max) == orig_max`.
     - En POSIX, se replica idéntica disciplina con `rlimit` (soft limit = 40) e impresión explícita de `n_fds`.
2. **Endurecimiento del Escenario `EACCES` (R12-02):**
   - **Windows:**
     - Se exige la creación de un directorio de prueba completamente nuevo y limpio. Si existiera un directorio residual se elimina previamente con `RemoveDirectoryA()`, y se falla la prueba si `CreateDirectoryA()` no logra crear un directorio nuevo (se rechaza la reutilización de directorios con `ERROR_ALREADY_EXISTS`).
     - Se valida formalmente el retorno de `WaitForSingleObject()` exigiendo `WAIT_OBJECT_0` con timeout acotado.
     - Se comprueba que `GetExitCodeProcess()` retorne `TRUE` y que el código de terminación de `icacls.exe` sea exactamente `0`.
     - Se intenta la creación de un archivo inexistente, verificando `CSV_OPEN_FATAL_ERROR` y `errno == EACCES` (13).
     - Se ejecuta el proceso de desasignación de la ACE denegatoria (`icacls ... /remove:d *S-1-1-0`), validando el retorno de `WaitForSingleObject` y código de salida 0.
     - Se valida que la eliminación final del directorio mediante `RemoveDirectoryA()` sea exitosa.
   - **POSIX:**
     - Se valida la creación del directorio `mkdir(denied_dir, 0555) == 0`.
     - Tras la prueba de denegación, se comprueban obligatoriamente los retornos de restauración y limpieza: `chmod(denied_dir, 0755) == 0` y `rmdir(denied_dir) == 0`.
3. **Endurecimiento de Operaciones de E/S Binaria (R12-03):**
   - Se valida explícitamente el código de retorno de todas las operaciones de sistema de archivos en los casos de prueba:
     - `fwrite()`: se comprueba que el total de bytes transferidos coincida exactamente con el tamaño del buffer (256 bytes).
     - `fclose()`: se comprueba que el retorno sea estrictamente `0`.
     - `fseek()`: se valida el retorno `0` tanto para `SEEK_END` como para `SEEK_SET`.
     - `ftell()`: se comprueba que retorne un valor no negativo y exactamente igual a 256 bytes.
     - `fread()`: se valida que lea exactamente los 256 bytes esperados.
     - `memcmp()`: se certifica la identidad binaria bit a bit contra los patrones en memoria.
4. **Trazabilidad Corregida y Reconocimiento de Limitación TOCTOU (R12-04):**
   - Se subsanan los identificadores de ejecución en el expediente: el commit R11 (`84afb61f548179fdc4d8b4c666198ef7174142b7`) generó los workflow runs de GitHub Actions `42340324059` y `42340320491`, ambos finalizados con éxito pleno en sus 4 trabajos.
   - **Limitación TOCTOU Expresa:** La rutina compartida `defensive_cleanup_failed_exclusive_fd()` compara la identidad a nivel de kernel (`fstat` en POSIX, `BY_HANDLE_FILE_INFORMATION` en Windows) garantizando que el archivo abierto no ha sido suplantado en el momento del chequeo, cancelando el borrado si detecta discrepancia. No obstante, en espacio de usuario existe inevitablemente una micro-ventana temporal entre el cierre del descriptor y la invocación de `unlink(path)` / `_unlink(path)` en la cual un atacante local concurrente podría manipular el enlace. Dicha limitación se asume formalmente en este protocolo como inherente a la semántica de borrado por ruta en sistemas de archivos.
5. **Semántica Estricta 7/7 (R12-05):**
   - Se mantiene la exigencia innegociable de 7 casos aprobados, 0 fallos y 0 omisiones en Linux y Windows.

### Matriz Consolidada de Subsanaciones R12:
| Control de Auditoría | Estado en R11 | Subsanación Efectiva en R12 | Evidencia en Código / Repositorio |
| :--- | :--- | :--- | :--- |
| **R12-01 — EMFILE y Límites UCRT** | Distinción documental ausente; falta imprimir `n_fds` | **SUBSANADO**: Documentación formal del límite de streams (2048) vs bajo nivel (8192). Registro explícito en `stdout` de `n_fds` en saturación, `failing_errno` y `probe_errno`. Restauración verificada. | [`tests/benchmarks/benchmark_decisive_suite.c`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c) |
| **R12-02 — EACCES Endurecido** | `WaitForSingleObject` y `GetExitCodeProcess` sin validar; directorio reutilizable | **SUBSANADO**: Comprobación estricta de `WaitForSingleObject == WAIT_OBJECT_0` y `exitCode == 0` en aplicación y retirada de ACE. Exigencia de directorio nuevo. Comprobación de retornos de `chmod`/`rmdir` en POSIX. | [`tests/benchmarks/benchmark_decisive_suite.c`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c) |
| **R12-03 — E/S Binaria Validada** | `fwrite`/`fclose`/`fseek`/`ftell` sin comprobación explícita de retorno | **SUBSANADO**: Validación exhaustiva de cada retorno de llamada a `fwrite`, `fclose`, `fseek` (ambos sentidos), `ftell` y `fread`, reportando fallo ante cualquier anomalía de I/O. | [`tests/benchmarks/benchmark_decisive_suite.c`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c) |
| **R12-04 — Trazabilidad y TOCTOU** | Discrepancia de Run ID / Job IDs en informe | **SUBSANADO**: Subsanación de referencias de CI (Run `42340324059` verificado). Documentación formal de la limitación TOCTOU residual en espacio de usuario. | Secciones 1, 2 y 6 de este documento |
| **R12-05 — Semántica Estricta 7/7** | Conforme en R11 | **MANTENIDO**: Conteo estricto 7/7 sin tolerancia a omisiones. | [`tests/benchmarks/benchmark_decisive_suite.c`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c) |

---

## 2. Inventario y Trazabilidad del Entorno Experimental

### 2.1. Harness de Benchmark Certificado
* **Ubicación en Repositorio:** [`tests/benchmarks/benchmark_decisive_suite.c`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c).
* **Control de Cierre Previo:** **BP-008C-R6** (Precedencia estricta de virtualización `CPUID.1:ECX[31]`, validación de rango de hojas básicas y de hipervisor en `0x40000000`, y lectura estrictamente no negativa de `core_id`).
* **Aislamiento de Memoria:** Subprocesos independientes instanciados vía `fork()` y sincronizados mediante `wait4()` en Linux / WSL2, con monitoreo del consumo de memoria residente (*Peak RSS*) vía `struct rusage`.
* **Temporización de Alta Resolución:** `clock_gettime(CLOCK_MONOTONIC)` en sistemas POSIX / Linux, con resolución de nanosegundos; `QueryPerformanceCounter` / `QueryPerformanceFrequency` en Windows nativo.

### 2.2. Aislamiento de ISA y Procedimiento Formal de Inspección (Aprobado)

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

## 6. Destino CSV, Identidad a Nivel de Kernel y Suite Adversarial (R12-01 a R12-05)

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
   - **Ventana TOCTOU Residual Documentada:** Se reconoce formalmente que la verificación de identidad del archivo abierto (`fd`) previa a `unlink(path)` / `_unlink(path)` previene el borrado ante sustituciones ya ocurridas; no obstante, subsiste una ventana atómica residual de espacio de usuario entre el cierre del descriptor y la desvinculación de la ruta, gobernada por el sistema operativo subyacente.
5. **Aborto Obligatorio en `main()`:**
   Si `open_safe_csv_r5()` retorna `NULL`, el harness aborta fatalmente con código de salida `1`.
6. **Suite Unitaria Adversarial de 7 Casos sin Omisiones (R12-05):**
   - **Caso 1:** Creación limpia exclusiva (`CSV_OPEN_SUCCESS`) con validación de cierre.
   - **Caso 2:** Detección de colisión `EEXIST`, preservación bit a bit y prevención de truncamiento.
   - **Caso 3:** Detección de directorio inexistente `ENOENT` y autorización de fallback.
   - **Caso 4:** Agotamiento determinista de descriptores (`EMFILE`) verificado con retornos de kernel, límites restaurados, registro en runtime de `n_fds` y comprobación forzada de `errno == EMFILE`.
   - **Caso 5:** Inducción real de `EACCES` mediante directorio nuevo con DACL restrictiva en Windows (`icacls` con exit code 0 y `WaitForSingleObject` validado) y `0555` en POSIX, con verificación forzada de `errno == EACCES` y limpieza integral certificada.
   - **Caso 6:** Rotación verificada sobre sufijos preexistentes (`_run1.._run3`), selección de `_run4`, comprobación binaria bit a bit (`memcmp == 0`), validación estricta de retornos de E/S (`fwrite`/`fclose`/`fseek`/`ftell`/`fread`) y detección de agotamiento.
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
- [x] **3. Aislamiento ISA Aprobado:** Script `scripts/audit_isa_isolation.sh` con flags sincronizadas 1:1 (`PORTABLE_CFLAGS` y `AVX2_CFLAGS`), sin `continue` fuera de bucle, con exigencia de exit code 0 y detección de bytes crudos.
- [x] **4. Creación Atómica Exclusiva CSV Robusta y Suite Adversarial Estricta (R12-01 a R12-05):** Determinismo EMFILE comprobado con `n_fds` explícito y `errno == EMFILE`, EACCES real demostrado con `icacls` exit code 0, `WaitForSingleObject` validado y `errno == EACCES`, rotación y agotamiento de sufijos con integridad binaria de 256 bytes (`memcmp`) y retornos I/O validados, sustitución concurrente demostrada en Windows y POSIX con supresión de borrado de archivos ajenos vía `defensive_cleanup_failed_exclusive_fd()`, y semántica estricta 7/7 sin skips.
- [x] **5. Corrección Aritmética y Taxonomía de Memoria:** Fórmulas exactas ($50.757.632\text{ B}$ propios), `ZSTD_estimateCStreamSize()` como estimador streaming oficial ($52.38\text{ MB}$ en L4 $\le 64\text{ MB}$; $117.87\text{ MB}$ en L19 $\le 128\text{ MB}$) y deslinde de las 4 categorías.
- [x] **6. Enforzamiento de Monohilo:** `opt.nb_workers = 1;` configurado explícitamente en todas las ejecuciones del harness.
- [ ] **7. Integración Continua Multiplataforma Certificada sobre R12:** Ejecución y verificación del workflow en GitHub Actions sobre el commit exacto de R12, validando la suite CSV 7/7 en Linux y Windows, y la auditoría ISA en Linux.
- [ ] **8. Aprobación Formal de BP-008D:** Dictamen técnico favorable de la auditoría independiente.
- [ ] **9. Disponibilidad del Corpus:** Datasets descargados y verificados con sus respectivos hashes SHA-256 en disco.
- [ ] **10. Aislamiento de Hardware:** Sistema en reposo térmico ($T_{\text{CPU}} \le 45^\circ\text{C}$), sin procesos secundarios.

---

## 9. Criterio de Cierre del Hito BP-008D

El Hito **BP-008D** se considerará preparado para su **cierre técnico definitivo** una vez que el commit de R12 sea verificado en CI, confirmando:
1. El aislamiento de instrucciones SIMD verificado y aprobado.
2. La suite adversarial CSV ejecutada y aprobada con 7/7 casos estrictos sin omisiones en Linux y Windows.
3. La corrección aritmética de la memoria streaming bajo `ZSTD_estimateCStreamSize()`.
4. La desactivación irreversible de paralelismo automático mediante `opt.nb_workers = 1`.
5. La ausencia total de ejecución de la campaña de benchmarking de 20 iteraciones.
