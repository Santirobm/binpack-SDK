# Protocolo de Reproducibilidad Experimental, Trazabilidad Metodológica y Baselines — Hito BP-008D (Revisión R13)

**Expediente:** BP-008D-R13 — Certificación Exhaustiva de E/S en Rutas de Error de la Suite CSV, Precisión Documental de CI (Eventos `push` vs `pull_request`, Matriz de Tareas Linux vs Windows), Deslinde Criptográfico de Identificación de Commits (Hash SHA-1 Inmutable vs Firmas de Autor) y Preparación para Cierre Técnico del Protocolo Experimental.  
**Proyecto:** BinPack Enterprise SDK  
**Ramas Auditadas:** `audit/bp1-core-safety`  
**Hito Previo:** BP-008D-R12 (Commit `b9d1137edcd787b3defdd14fa17a7bf88d3a1e28`, Workflow Runs `42341269554` [evento `push`] y `42341273160` [evento `pull_request`], con 8/8 trabajos verificados exitosos en GitHub Actions)  
**Fecha:** 7 de Noviembre de 2026  
**Autor:** Santiago Robledo (<santiagorobledo198@gmail.com>)  
**Responsable Técnico:** Antigravity AI — Advanced Agentic Coding & Systems Architecture  

---

## 1. Resumen Ejecutivo y Respuesta a la Auditoría BP-008D-R12

El presente documento formaliza la **Revisión R13 del Expediente BP-008D**, dando cumplimiento exhaustivo a las observaciones formuladas en el Dictamen Técnico Independiente sobre R12 (donde la implementación técnica fue declarada plenamente conforme, requiriéndose precisiones en rutas de error y trazabilidad documental).

En esta revisión se abordan y subsanan los siguientes aspectos requeridos por el auditor independiente:

1. **R13-01 — Validación de E/S en Rutas de Error de la Suite CSV (`tests/benchmarks/benchmark_decisive_suite.c`):**
   - **Cierre y Comprobación en Test 6:** En la prueba de rotación de sufijos, se verifica obligatoriamente el retorno de `fclose(f_chosen)` ante ruta no coincidente (`strcmp != 0`) y de `fclose(f_exhaust)` ante creación errónea cuando los sufijos están agotados (`f_exhaust != NULL`). En ambos casos, cualquier fallo de cierre reporta error en `stderr` e incrementa irreversiblemente `tests_failed++`.
   - **Inspección Sistemática de Descriptores y Streams (Tests 1 a 7):**
     - **Test 2:** Se comprueba `fclose(f2)` ante creación errónea sobre archivo preexistente y `fclose(f_check)` durante la comprobación de contenido, forzando `tests_failed++` ante cualquier fallo de `fclose`.
     - **Test 3:** Se valida el retorno de `fclose(f3)` ante creación anómala en directorio inexistente.
     - **Test 4:** Se valida `fclose(f_emfile)` en la rama de error tanto en POSIX como en Windows, y se comprueba el retorno de `close(fds[i])` (POSIX) y `_close(fds[i])` (Windows) en el bucle de liberación de descriptores auxiliares, forzando fallo si algún descriptor no se cierra limpiamente.
     - **Test 5:** Se valida `fclose(f_eacces)` (POSIX) y `fclose(f_win)` (Windows) si la creación llegara a triunfar indebidamente en directorios protegidos.
     - **Test 7:** Se valida explícitamente el cierre defensivo de `orig_fd` (`close(orig_fd)` en POSIX, `_close(orig_fd)` en Windows) verificando su código de salida y reportando fallo si no es `0`.
   - Ninguna operación de cierre de archivo o descriptor queda desatendida o silenciada a lo largo de toda la suite.

2. **R13-02 — Precisión Documental, Trazabilidad de CI y Clarificación Criptográfica:**
   - **Trazabilidad Exacta de Eventos en GitHub Actions:**
     - En la entrega R12, el empuje a la rama `audit/bp1-core-safety` disparó dos workflow runs debido a la existencia de un Pull Request abierto sobre la misma rama:
       - **Workflow Run `42341269554`:** Correspondiente al evento `push` sobre la rama `audit/bp1-core-safety`.
       - **Workflow Run `42341273160`:** Correspondiente al evento `pull_request` (asociado al PR sobre la rama).
     - Ambos runs ejecutaron la matriz completa de 4 trabajos (Linux x86_64, Windows x86_64, macOS Apple Silicon / Universal, y Unity UPM Package), completando los 8 trabajos con resultado `Success` pleno sobre el commit exacto `b9d1137edcd787b3defdd14fa17a7bf88d3a1e28`.
   - **Descripción Precisa del Entorno de Ejecución en Linux:**
     - Se documenta con exactitud la secuencia ejecutada en el job de Ubuntu Linux:
       - Compilación del núcleo LZMA y generación de biblioteca compartida `libbinpack.so`.
       - Compilación y ejecución de la suite de pruebas unitarias del SDK (`test_sdk`).
       - Ejecución de la auditoría estricta de aislamiento ISA (`scripts/audit_isa_isolation.sh`).
       - Compilación del harness `bench_suite` y ejecución de la suite de fallos y concurrencia CSV (`./bench_suite --test-csv-errors`).
     - Se clarifica expresamente que el script batch de Windows (`build.bat`) y sus 14 suites de regresión C son privativas del trabajo de Windows en CI y no aplican al contenedor de Linux.
   - **Deslinde Criptográfico: Identificación por Hash SHA vs Firmas de Autor:**
     - Los commits del repositorio se encuentran identificados y garantizados en su inmutabilidad mediante el árbol de objetos Git y su identificador unívoco de 40 caracteres hexadecimales (hash criptográfico SHA-1).
     - Se aclara formalmente que los commits no cuentan con firmas criptográficas de autoría (GPG/SSH commit signatures), por lo que la integridad y trazabilidad se sustentan estrictamente en la inmutabilidad matemática del grafo Git y la verificación directa contra la API de GitHub.

3. **R13-03 — Verificación Final, Ejecución Multiplataforma y Cierre de Protocolo:**
   - Ejecución de la suite CSV 7/7 en Linux y Windows, verificando 0 fallos y 0 omisiones.
   - Se mantiene intacto el código fuente (`src/`, `include/`, `deps/`).
   - Se declara el Hito BP-008D como preparado para cierre técnico en calidad de **protocolo experimental y de reproducibilidad certificado**, quedando su cierre formal supeditado al dictamen favorable de la auditoría técnica independiente.

---

### Matriz Consolidada de Subsanaciones R13:
| Control de Auditoría | Estado en R12 | Subsanación Efectiva en R13 | Evidencia en Repositorio |
| :--- | :--- | :--- | :--- |
| **R13-01 — Validación de E/S en Errores** | `fclose` no verificado en Test 6 y paths de error secundarios | **SUBSANADO**: Validación exhaustiva de `fclose`, `close`, `_close` en todas las ramas de error (Tests 1-7). Fallos incrementan `tests_failed++`. | [`tests/benchmarks/benchmark_decisive_suite.c`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c) |
| **R13-02 — Precisión Documental CI** | Identificación genérica de runs y matriz Linux | **SUBSANADO**: Desglose formal de run `42341269554` (`push`) y run `42341273160` (`pull_request`). Descripción fiel de tareas en Linux. Deslinde entre hash SHA y firmas de autor. | Secciones 1, 2 y 6 de este documento |
| **R13-03 — Semántica Estricta 7/7** | Conforme en R12 | **MANTENIDO**: Conteo estricto 7/7 sin skips ni fallos en Linux y Windows. | CI GitHub Actions y log de ejecución |

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

## 6. Destino CSV, Identidad a Nivel de Kernel y Suite Adversarial (R13-01 a R13-03)

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
6. **Suite Unitaria Adversarial de 7 Casos con Cierre de E/S Exhaustivo (R13-01):**
   - **Caso 1:** Creación limpia exclusiva (`CSV_OPEN_SUCCESS`) con validación de cierre.
   - **Caso 2:** Detección de colisión `EEXIST`, preservación bit a bit, prevención de truncamiento y comprobación rigurosa de `fclose`.
   - **Caso 3:** Detección de directorio inexistente `ENOENT`, autorización de fallback y validación de `fclose` en error.
   - **Caso 4:** Agotamiento determinista de descriptores (`EMFILE`) verificado con retornos de kernel, límites restaurados, registro en runtime de `n_fds`, comprobación forzada de `errno == EMFILE`, y validación exhaustiva del bucle de cierre de descriptores.
   - **Caso 5:** Inducción real de `EACCES` mediante directorio nuevo con DACL restrictiva en Windows (`icacls` con exit code 0 y `WaitForSingleObject` validado) y `0555` en POSIX, con verificación forzada de `errno == EACCES`, validación de `fclose` en error y limpieza integral certificada.
   - **Caso 6:** Rotación verificada sobre sufijos preexistentes (`_run1.._run3`), selección de `_run4`, comprobación binaria bit a bit (`memcmp == 0`), validación estricta de retornos de E/S (`fwrite`/`fclose`/`fseek`/`ftell`/`fread`), detección de agotamiento y validación exhaustiva de `fclose(f_chosen)` y `fclose(f_exhaust)` en rutas de error.
   - **Caso 7:** Identidad a nivel de kernel y preservación demostrada de archivo ajeno ante sustitución concurrente de ruta en Windows y POSIX mediante `defensive_cleanup_failed_exclusive_fd()`, con validación estricta del cierre de `orig_fd`.

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
- [x] **4. Creación Atómica Exclusiva CSV Robusta y Suite Adversarial Estricta (R13-01):** Validación completa de retornos de E/S y cierre de archivos/descriptores en todas las ramas de error, determinismo EMFILE comprobado con `n_fds` explícito y `errno == EMFILE`, EACCES real demostrado con `icacls` exit code 0, rotación y agotamiento de sufijos con integridad binaria de 256 bytes (`memcmp`), sustitución concurrente demostrada en Windows y POSIX con supresión de borrado de archivos ajenos vía `defensive_cleanup_failed_exclusive_fd()`, y semántica estricta 7/7 sin skips.
- [x] **5. Corrección Aritmética y Taxonomía de Memoria:** Fórmulas exactas ($50.757.632\text{ B}$ propios), `ZSTD_estimateCStreamSize()` como estimador streaming oficial ($52.38\text{ MB}$ en L4 $\le 64\text{ MB}$; $117.87\text{ MB}$ en L19 $\le 128\text{ MB}$) y deslinde de las 4 categorías.
- [x] **6. Enforzamiento de Monohilo:** `opt.nb_workers = 1;` configurado explícitamente en todas las ejecuciones del harness.
- [ ] **7. Integración Continua Multiplataforma Certificada sobre R13:** Ejecución y verificación del workflow en GitHub Actions sobre el commit exacto de R13, validando la suite CSV 7/7 en Linux y Windows, y la auditoría ISA en Linux.
- [ ] **8. Aprobación Formal de BP-008D:** Dictamen técnico favorable de la auditoría independiente.
- [ ] **9. Disponibilidad del Corpus:** Datasets descargados y verificados con sus respectivos hashes SHA-256 en disco.
- [ ] **10. Aislamiento de Hardware:** Sistema en reposo térmico ($T_{\text{CPU}} \le 45^\circ\text{C}$), sin procesos secundarios.

---

## 9. Criterio de Cierre del Hito BP-008D

El Hito **BP-008D** se considerará preparado para su **cierre técnico definitivo como fase preparatoria del protocolo experimental**, una vez que el commit de R13 sea verificado en CI, confirmando:
1. El aislamiento estricto de instrucciones SIMD verificado y aprobado (cero emisiones VEX/EVEX en portables).
2. La suite adversarial CSV ejecutada y aprobada con 7/7 casos estrictos sin omisiones en Linux y Windows, con validación exhaustiva de retornos y cierres en todas las rutas de error.
3. La corrección aritmética y taxonomía de memoria streaming bajo `ZSTD_estimateCStreamSize()`.
4. La desactivación irreversible de paralelismo automático mediante `opt.nb_workers = 1`.
5. La ausencia total de ejecución de la campaña de benchmarking de 20 iteraciones.
6. La preservación intacta del núcleo del SDK (`src/`, `include/`, `deps/`).

El cierre definitivo queda condicionado a la emisión del dictamen formal aprobatorio por parte de la auditoría técnica independiente.
