# Protocolo de Reproducibilidad Experimental, Trazabilidad Metodológica y Baselines — Hito BP-008D (Revisión R10)

**Expediente:** BP-008D-R10 — Validez Probatoria Estricta de la Suite Adversarial CSV: Determinismo EMFILE con Validación Forzada de Retorno y Restauración de Límites (`setrlimit` y `_setmaxstdio`), Demostración Real de EACCES sin Colisión EEXIST (POSIX y Win32 DACL Restrictiva), Rotación Exhaustiva de Sufijos con Verificación Bit a Bit de Integridad y Detección de Agotamiento, Identidad de Kernel con Prueba de Sustitución Concurrente de Rutas (Prevención Demostrada de Borrado de Archivos Ajenos), Semántica Estricta de Suite (Contabilización Explícita de Passed/Failed/Skipped, Sin Tolerancia a Omisiones) e Integración Multiplataforma en CI (Linux y Windows).  
**Proyecto:** BinPack Enterprise SDK  
**Ramas Auditadas:** `audit/bp1-core-safety`  
**Hito Previo:** BP-008D-R9 (Commit `c747603d0ecf133d5d37a350285f23616236858f`, Run `42333437899`)  
**Fecha:** 7 de Noviembre de 2026  
**Autor:** Santiago Robledo (<santiagorobledo198@gmail.com>)  
**Responsable Técnico:** Antigravity AI — Advanced Agentic Coding & Systems Architecture  

---

## 1. Resumen Ejecutivo y Respuesta a la Auditoría BP-008D-R9

El presente documento constituye la **Revisión R10 del Expediente BP-008D**, formalizada en estricta y rigurosa respuesta a las observaciones y requerimientos del Dictamen Técnico de Auditoría R9.

En esta revisión se abordan y subsanan integralmente las seis observaciones técnicas formuladas en la orden de trabajo BP-008D-R10:

1. **Determinismo Estricto en Agotamiento de Descriptores (`EMFILE`, R10-01):**
   - En POSIX: se valida explícitamente el valor de retorno de `getrlimit()` y `setrlimit()`. Se comprueba que el bucle de agotamiento alcance `fd < 0` con `errno == EMFILE`. Se valida la restauración del límite original de descriptores al finalizar la prueba, reportando fallo si la restauración fracasa.
   - En Windows: se valida que `_setmaxstdio(512)` retorne 512 y se induce el agotamiento en la tabla CRT hasta verificar `fd < 0` y `errno == EMFILE`.
   - Se elimina categóricamente la posibilidad de omitir (`SKIP`) la prueba sin marcar fallo: si no se alcanza `EMFILE`, el test falla explícitamente (`tests_failed++`).
2. **Inducción Real de Permiso Denegado (`EACCES`, R10-02):**
   - En Windows: se crea un directorio temporal con un Descriptor de Seguridad (DACL) vacío que deniega incondicionalmente todos los permisos de escritura/creación (`InitializeAcl` / `SetSecurityDescriptorDacl`). Se intenta la creación de un archivo inexistente dentro de dicho directorio, verificando que el kernel retorne `ERROR_ACCESS_DENIED` (mapeado a `errno = EACCES`) y que `open_atomic_exclusive_csv()` retorne con exactitud `CSV_OPEN_FATAL_ERROR`. Se erradica por completo la prueba espuria sobre archivos preexistentes (`EEXIST`).
   - En POSIX: se crea un directorio con modo restrictivo `0555` (sin permisos de escritura) y se comprueba la generación forzada de `EACCES` y `CSV_OPEN_FATAL_ERROR`.
   - No se admite `SKIP` como aprobado: cualquier imposibilidad de inducir el error se registra como fallo.
3. **Rotación Real de Sufijos y Verificación Bit a Bit (R10-03):**
   - Se pre-crean el archivo base y tres sufijos consecutivos (`_run1`, `_run2`, `_run3`) con firmas de contenido y longitudes diferenciadas.
   - Se ejecuta la función `open_safe_csv_r5_ex()` y se verifica que seleccione de forma determinista y exacta el sufijo `_run4.csv`.
   - Se verifica mediante lectura y comparación bit a bit que el archivo base y los sufijos 1 a 3 permanecieron completamente intactos, sin truncamientos de 0 bytes ni alteraciones.
   - Se prueba el escenario de agotamiento de sufijos (`max_runs = 3` sobre 3 sufijos ocupados), demostrando que la función aborta limpiamente (`NULL`) sin corromper ningún archivo.
4. **Validación de Identidad a Nivel de Kernel ante Sustitución Concurrente de Rutas (R10-04):**
   - Se prueba la comparación entre descriptor abierto y ruta de archivo.
   - Se simula un escenario de sustitución concurrente de ruta: se crea un archivo, se mantiene abierto su descriptor de archivo (`fd`), se elimina el enlace en el sistema de archivos y se crea un archivo ajeno diferente en la misma ruta con contenido específico.
   - Se comprueba que el comparador a nivel de kernel (File ID Win32 y dev/ino POSIX) detecta la discrepancia (`mismatch`).
   - Se ejecuta el bloque defensivo de limpieza y se verifica que **se suprime el intento de desvinculación (`unlink`)**, preservando bit a bit el archivo sustituto ajeno sin eliminarlo.
   - Se mantiene plenamente documentada la limitación inherente a TOCTOU en operaciones de borrado basadas en ruta.
5. **Semántica Estricta del Resultado de la Suite (R10-05):**
   - Se implementa un conteo independiente y exhaustivo de `Passed`, `Failed` y `Skipped`.
   - La condición de éxito global exige formalmente `tests_passed == 7 && tests_failed == 0 && tests_skipped == 0`. Si cualquier caso no se ejecuta o falla, la suite aborta con código de salida `1` (`exit 1`).
   - El mensaje `[SUCCESS] ALL CSV SAFETY UNIT TESTS PASSED (7/7 STRICT WITHOUT SKIPS)` se emite única y exclusivamente cuando los 7 casos se ejecutaron y aprobaron.
   - Se imprime el resumen discriminado en `stdout` para su registro en los logs de CI.
6. **Integración Continua Multiplataforma y Trazabilidad (R10-06):**
   - Se mantiene la compilación y ejecución de `--test-csv-errors` tanto en el runner Linux x86_64 como en el runner Windows x86_64 de GitHub Actions, con enlace a `-lpsapi` y `-ladvapi32`.
   - Se corrige la transcripción del SHA auditado en el expediente oficial.

### Matriz Consolidada de Subsanaciones R10:
| Control de Auditoría | Estado en R9 | Subsanación Efectiva en R10 | Evidencia en Código / Repositorio |
| :--- | :--- | :--- | :--- |
| **R10-01 — EMFILE Determinista** | Posible `SKIP` sin fallo; validación de retornos incompleta | **SUBSANADO**: Validación estricta de `getrlimit`/`setrlimit` y `_setmaxstdio`. Exigencia de `last_errno == EMFILE`. Restauración verificada de límites. Fallo forzado ante cualquier desviación. | [`tests/benchmarks/benchmark_decisive_suite.c:1960-2040`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c) |
| **R10-02 — EACCES Real sin EEXIST** | En Windows colisionaba como `EEXIST` sobre archivo ya creado | **SUBSANADO**: Win32 DACL restrictiva (DACL vacía) sobre directorio de prueba; `mkdir 0555` en POSIX. Prueba de creación en archivo inexistente. Verificación forzada de `CSV_OPEN_FATAL_ERROR`. Cero skips. | [`tests/benchmarks/benchmark_decisive_suite.c:2045-2115`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c) |
| **R10-03 — Rotación Real de Sufijos** | Solo probaba asignación básica sin verificar sufijos | **SUBSANADO**: Precreación de base y `_run1.._run3`. Selección verificada de `_run4`. Validación bit a bit de contenido intacto en preexistentes. Prueba demostrada de aborto ante agotamiento. | [`tests/benchmarks/benchmark_decisive_suite.c:1835-1875,2120-2210`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c) |
| **R10-04 — Sustitución y Borrado Ajeno** | Comparación estática sin simular sustitución de ruta | **SUBSANADO**: Simulación activa de sustitución de ruta tras creación. Detección demostrada de discrepancia por kernel. Supresión verificada de `unlink` y comprobación de que el archivo ajeno permanece intacto. | [`tests/benchmarks/benchmark_decisive_suite.c:2215-2315`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c) |
| **R10-05 — Semántica Estricta 7/7** | Contador booleano que podía reportar 7/7 con omisiones | **SUBSANADO**: Contadores independientes `Passed`/`Failed`/`Skipped`. Fallo fatal (`exit 1`) ante cualquier omisión. Mensaje 7/7 reservado exclusivamente para aprobación plena de los 7 casos. | [`tests/benchmarks/benchmark_decisive_suite.c:2320-2335`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c) |
| **R10-06 — CI Multiplataforma y Trazabilidad** | Corrección de SHA transcripto y logs detallados | **SUBSANADO**: Enlace con `-ladvapi32` y `-lpsapi` en Windows; ejecución verificada de `--test-csv-errors` en Linux y Windows; rectificación del SHA auditado. | [`.github/workflows/build-multiplatform.yml`](file:///C:/Users/usuario/Desktop/binpack/.github/workflows/build-multiplatform.yml) + Sección 8 |

---

## 2. Inventario y Trazabilidad del Entorno Experimental

### 2.1. Harness de Benchmark Certificado
* **Ubicación en Repositorio:** [`tests/benchmarks/benchmark_decisive_suite.c`](file:///C:/Users/usuario/Desktop/binpack/tests/benchmarks/benchmark_decisive_suite.c).
* **Control de Cierre Previo:** **BP-008C-R6** (Precedencia estricta de virtualización `CPUID.1:ECX[31]`, validación de rango de hojas básicas y de hipervisor en `0x40000000`, y lectura estrictamente no negativa de `core_id`).
* **Aislamiento de Memoria:** Subprocesos independientes instanciados vía `fork()` y sincronizados mediante `wait4()` en Linux / WSL2, con monitoreo del consumo de memoria residente (*Peak RSS*) vía `struct rusage`.
* **Temporización de Alta Resolución:** `clock_gettime(CLOCK_MONOTONIC)` en sistemas POSIX / Linux, con resolución de nanosegundos; `QueryPerformanceCounter` / `QueryPerformanceFrequency` en Windows nativo.

### 2.2. Aislamiento de ISA y Procedimiento Formal de Inspección (Aprobado en R9)

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

## 6. Destino CSV, Identidad a Nivel de Kernel y Suite Adversarial (R10-01 a R10-05)

### Reglas de Conducta Exigidas y Manejo de Identidad:
1. **`EEXIST`:** El archivo ya existe. Se detecta como colisión segura (`CSV_OPEN_COLLISION_EEXIST`) y se procede a probar secuencialmente `_run1.._run999` dentro del mismo directorio. **Jamás se sobrescribe ni se trunca el archivo existente.**
2. **`ENOENT`:** El directorio preferido no existe (`CSV_OPEN_DIR_NOT_FOUND`). **Únicamente ante este error se autoriza el fallback al siguiente directorio candidato** (`docs/audit` $\rightarrow$ `.`).
3. **`EACCES`, `EMFILE`, `ENFILE` u otros errores:** Se catalogan como errores operativos no recuperables (`CSV_OPEN_FATAL_ERROR`). **Se aborta inmediatamente sin intentar directorios alternativos**.
4. **Manejo de Errores de Conversión a `FILE*` y Limpieza Defensiva:**
   Si `fdopen()` / `_fdopen()` falla:
   - Se comprueba la identidad física del archivo:
     - En **Windows**, mediante `_get_osfhandle(fd)` y `CreateFileA()` para comparar `BY_HANDLE_FILE_INFORMATION` (`dwVolumeSerialNumber`, `nFileIndexHigh`, `nFileIndexLow`).
     - En **POSIX**, mediante `fstat(fd)` y `lstat(path)` para comparar `st_dev` y `st_ino`.
   - **Política Defensiva:** Si la identidad no coincide o no puede ser verificada positivamente, **se suprime el intento de desvinculación para no afectar un archivo ajeno**.
   - Si la desvinculación falla o se cancela, se emite una advertencia formal indicando que ha quedado un archivo residual inerte.
   - Se reconoce formalmente la existencia de la ventana TOCTOU inherente a operaciones de borrado basadas en ruta, implementando la política defensiva más estricta posible en el espacio de usuario.
5. **Aborto Obligatorio en `main()`:**
   Si `open_safe_csv_r5()` retorna `NULL`, el harness aborta fatalmente con código de salida `1`.
6. **Suite Unitaria Adversarial de 7 Casos sin Omisiones (R10-05):**
   - **Caso 1:** Creación limpia exclusiva (`CSV_OPEN_SUCCESS`).
   - **Caso 2:** Detección de colisión `EEXIST` y preservación bit a bit sin truncamiento.
   - **Caso 3:** Detección de directorio inexistente `ENOENT` y autorización de fallback.
   - **Caso 4:** Agotamiento determinista de descriptores (`EMFILE`) verificado con retornos de kernel y límites restaurados.
   - **Caso 5:** Inducción real de `EACCES` mediante directorio con DACL restrictiva en Windows y `0555` en POSIX.
   - **Caso 6:** Rotación verificada sobre sufijos preexistentes (`_run1.._run3`), selección de `_run4`, comprobación de integridad y detección de agotamiento.
   - **Caso 7:** Identidad a nivel de kernel y preservación demostrada de archivo ajeno ante sustitución concurrente de ruta.

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
- [x] **3. Aislamiento ISA Aprobado (R9-01):** Script `scripts/audit_isa_isolation.sh` con flags sincronizadas 1:1 (`PORTABLE_CFLAGS` y `AVX2_CFLAGS`), sin `continue` fuera de bucle, con exigencia de exit code 0 y detección de bytes crudos.
- [x] **4. Creación Atómica Exclusiva CSV Robusta y Suite Adversarial Estricta (R10-01 a R10-05):** Determinismo EMFILE comprobado, EACCES real demostrado, rotación y agotamiento de sufijos con integridad bit a bit, prevención demostrada de borrado de archivos ajenos ante sustitución de ruta, y semántica estricta 7/7 sin skips.
- [x] **5. Corrección Aritmética y Taxonomía de Memoria:** Fórmulas exactas ($50.757.632\text{ B}$ propios), `ZSTD_estimateCStreamSize()` como estimador streaming oficial ($52.38\text{ MB}$ en L4 $\le 64\text{ MB}$; $117.87\text{ MB}$ en L19 $\le 128\text{ MB}$) y deslinde de las 4 categorías.
- [x] **6. Enforzamiento de Monohilo:** `opt.nb_workers = 1;` configurado explícitamente en todas las ejecuciones del harness.
- [ ] **7. Integración Continua Multiplataforma Certificada sobre R10 (R10-06):** Ejecución y verificación del workflow en GitHub Actions sobre el commit exacto de R10, validando la suite CSV 7/7 en Linux y Windows, y la auditoría ISA en Linux.
- [ ] **8. Aprobación Formal de BP-008D:** Dictamen técnico favorable de la auditoría independiente.
- [ ] **9. Disponibilidad del Corpus:** Datasets descargados y verificados con sus respectivos hashes SHA-256 en disco.
- [ ] **10. Aislamiento de Hardware:** Sistema en reposo térmico ($T_{\text{CPU}} \le 45^\circ\text{C}$), sin procesos secundarios.

---

## 9. Criterio de Cierre del Hito BP-008D

El Hito **BP-008D** se considerará preparado para su **cierre técnico definitivo** una vez que el commit de R10 sea verificado en CI, confirmando:
1. El aislamiento de instrucciones SIMD verificado y aprobado.
2. La suite adversarial CSV ejecutada y aprobada con 7/7 casos estrictos sin omisiones en Linux y Windows.
3. La corrección aritmética de la memoria streaming bajo `ZSTD_estimateCStreamSize()`.
4. La desactivación irreversible de paralelismo automático mediante `opt.nb_workers = 1`.
5. La ausencia total de ejecución de la campaña de benchmarking de 20 iteraciones.
