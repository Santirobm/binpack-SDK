# Protocolo de Reproducibilidad Experimental, Trazabilidad Metodológica y Baselines — Hito BP-008D (Revisión R14)

**Expediente:** BP-008D-R14 — Subsanación Documental Final: Precisión Criptográfica sobre Identificadores de Contenido SHA-1 vs Firmas de Autor, Actualización Certificada del Checklist de CI Multiplataforma (8/8 Trabajos Exitosos en R13) y Preparación para Cierre Formal del Protocolo Experimental.  
**Proyecto:** BinPack Enterprise SDK  
**Ramas Auditadas:** `audit/bp1-core-safety`  
**Hito Previo:** BP-008D-R13 (Commit `35a82ef4d89d7739232da87f826f8033428f17a2`, Workflow Runs `42344207762` [evento `push`] y `42344210926` [evento `pull_request`], con 8/8 trabajos verificados exitosos en GitHub Actions)  
**Fecha:** 7 de Noviembre de 2026  
**Autor:** Santiago Robledo (<santiagorobledo198@gmail.com>)  
**Responsable Técnico:** Antigravity AI — Advanced Agentic Coding & Systems Architecture  

---

## 1. Resumen Ejecutivo y Respuesta a la Auditoría BP-008D-R13

El presente documento formaliza la **Revisión R14 del Expediente BP-008D**, en respuesta directa a las dos observaciones documentales formuladas en el Dictamen Técnico Independiente sobre R13 (revisión en la cual la implementación técnica fue plenamente aprobada sin nuevas deficiencias bloqueantes):

1. **R14-01 — Precisión Criptográfica sobre el Modelo de Objetos Git (SHA-1 vs Firmas de Autor):**
   - Se rectifica la caracterización previa de "inmutabilidad matemática absoluta del grafo Git".
   - Se precisa con rigor técnico que Git emplea un modelo de almacenamiento direccionado por contenido (*content-addressable storage*) sustentado en hashes SHA-1 de 40 caracteres hexadecimales.
   - Si bien SHA-1 no proporciona una garantía matemática absoluta contra colisiones teóricas ni acredita la identidad del autor (función que corresponde exclusivamente a firmas criptográficas GPG/SSH, las cuales no están implementadas en los commits del proyecto), sí provee una integridad referencial práctica y sólida detección de manipulaciones dentro del árbol de objetos del repositorio, verificable externamente mediante la API de GitHub.

2. **R14-02 — Actualización y Coherencia del Checklist de Integración Continua (Sección 8, Punto 7):**
   - En la entrega R13, el punto 7 del checklist permanecía desmarcado (`[ ]`) de forma contradictoria con la evidencia empírica verificada de CI.
   - En esta revisión R14, se actualiza a marcado formal (`[x]`), reflejando la ejecución y aprobación plena de los 8/8 trabajos de CI sobre el commit exacto de R13 (`35a82ef4d89d7739232da87f826f8033428f17a2`) en ambos eventos de GitHub Actions (`push` [Run `42344207762`] y `pull_request` [Run `42344210926`]), con ejecución certificada de la suite CSV 7/7 en Linux y Windows y la auditoría estricta de aislamiento ISA en Linux.

3. **Mantenimiento Estricto del Congelamiento de Código de Producción y Campaña Decisiva:**
   - La revisión R14 es de carácter **exclusivamente documental**.
   - No se introduce modificación alguna en el código del SDK (`src/`, `include/`, `deps/`).
   - El harness y los scripts conservan la implementación aprobada en R13.
   - La campaña decisiva de 20 iteraciones permanece estrictamente **congelada** y no autorizada hasta la formalización del cierre por parte de la auditoría técnica independiente.

---

### Matriz Consolidada de Estado Documental R14:
| Control de Auditoría | Estado en R13 | Subsanación Efectiva en R14 | Evidencia en Repositorio |
| :--- | :--- | :--- | :--- |
| **R14-01 — Precisión Criptográfica** | Calificación categórica de "inmutabilidad matemática" | **SUBSANADO**: Delimitación rigurosa de SHA-1 como direccionamiento por contenido con integridad referencial práctica, sin atribuirle invulnerabilidad teórica a colisiones ni acreditación de autoría. | Sección 1 y Sección 2.3 de este documento |
| **R14-02 — Coherencia de Checklist CI** | Punto 7 desmarcado `[ ]` pese a 8/8 jobs exitosos | **SUBSANADO**: Checklist actualizado a `[x]` con trazabilidad explícita de Runs `42344207762` y `42344210926`. | Sección 8, Punto 7 de este documento |
| **Congelamiento de Campaña Decisiva** | Conforme / Congelada | **MANTENIDO**: Sin ejecución de benchmark de 20 iteraciones ni cambios en `src/`, `include/`, `deps/`. | Repositorio general |

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

### 2.3. Trazabilidad de CI y Deslinde Criptográfico de Commits

1. **Trazabilidad Exacta de Eventos en GitHub Actions:**
   - En el flujo de CI del repositorio, cada empuje a la rama `audit/bp1-core-safety` dispara simultáneamente dos workflow runs debido a la existencia de un Pull Request abierto sobre la misma rama:
     - **Evento `push`:** Run ID correspondiente al push directo sobre la rama.
     - **Evento `pull_request`:** Run ID asociado a la evaluación del pull request sobre el commit de destino.
   - En R13, ambos runs completaron la matriz integral de 4 trabajos (Windows x86_64, Linux x86_64, macOS Apple Silicon / Universal, y Bundle Unity UPM Package) con conclusión `Success` pleno en los 8/8 trabajos.
2. **Matriz de Tareas por Sistema Operativo:**
   - **Linux x86_64:** Compilación de LZMA Core y `libbinpack.so`, ejecución de la suite unitaria `test_sdk`, auditoría formal de aislamiento ISA (`scripts/audit_isa_isolation.sh`), y ejecución de la suite de fallos y concurrencia CSV (`bench_suite --test-csv-errors`).
   - **Windows x86_64:** Compilación nativa, ejecución de las 14 suites C de regresión mediante `build.bat`, verificación de bindings de Python, y ejecución de la suite de fallos y concurrencia CSV (`bench_suite.exe --test-csv-errors`).
3. **Deslinde Criptográfico: Identificación de Contenido vs Firmas de Autor:**
   - Los commits del repositorio se encuentran referenciados mediante el modelo de objetos direccionados por contenido (*content-addressable storage*) de Git a través de sus hashes SHA-1 de 40 caracteres hexadecimales.
   - Se hace constar formalmente que SHA-1 no constituye una prueba matemática absoluta contra colisiones teóricas, ni valida por sí mismo la autoría humana del cambio (función privativa de firmas GPG/SSH, las cuales no están configuradas en los commits).
   - Su valor estriba en la integridad referencial práctica dentro del historial de Git y su verificación auditable contra la API pública de GitHub.

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

## 6. Destino CSV, Identidad a Nivel de Kernel y Suite Adversarial

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
6. **Suite Unitaria Adversarial de 7 Casos con Cierre de E/S Exhaustivo:**
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
- [x] **4. Creación Atómica Exclusiva CSV Robusta y Suite Adversarial Estricta:** Validación completa de retornos de E/S y cierre de archivos/descriptores en todas las ramas de error, determinismo EMFILE comprobado con `n_fds` explícito y `errno == EMFILE`, EACCES real demostrado con `icacls` exit code 0, rotación y agotamiento de sufijos con integridad binaria de 256 bytes (`memcmp`), sustitución concurrente demostrada en Windows y POSIX con supresión de borrado de archivos ajenos vía `defensive_cleanup_failed_exclusive_fd()`, y semántica estricta 7/7 sin skips.
- [x] **5. Corrección Aritmética y Taxonomía de Memoria:** Fórmulas exactas ($50.757.632\text{ B}$ propios), `ZSTD_estimateCStreamSize()` como estimador streaming oficial ($52.38\text{ MB}$ en L4 $\le 64\text{ MB}$; $117.87\text{ MB}$ en L19 $\le 128\text{ MB}$) y deslinde de las 4 categorías.
- [x] **6. Enforzamiento de Monohilo:** `opt.nb_workers = 1;` configurado explícitamente en todas las ejecuciones del harness.
- [x] **7. Integración Continua Multiplataforma Certificada sobre R13:** Ejecución y verificación del workflow en GitHub Actions sobre el commit exacto de R13 (Runs `42344207762` [push] y `42344210926` [pull_request]), validando la suite CSV 7/7 en Linux y Windows, y la auditoría ISA en Linux, completando 8/8 trabajos con resultado `Success` pleno.
- [ ] **8. Aprobación Formal de BP-008D:** Dictamen técnico favorable de cierre emitido por la auditoría independiente.
- [ ] **9. Disponibilidad del Corpus:** Datasets descargados y verificados con sus respectivos hashes SHA-256 en disco.
- [ ] **10. Aislamiento de Hardware:** Sistema en reposo térmico ($T_{\text{CPU}} \le 45^\circ\text{C}$), sin procesos secundarios.

---

## 9. Criterio de Cierre del Hito BP-008D

Habiéndose completado y certificado en R13 y afinado documentalmente en R14:
1. El aislamiento estricto de instrucciones SIMD verificado y aprobado (cero emisiones VEX/EVEX en portables).
2. La suite adversarial CSV ejecutada y aprobada con 7/7 casos estrictos sin omisiones en Linux y Windows, con validación exhaustiva de retornos y cierres en todas las rutas de error.
3. La corrección aritmética y taxonomía de memoria streaming bajo `ZSTD_estimateCStreamSize()`.
4. La desactivación irreversible de paralelismo automático mediante `opt.nb_workers = 1`.
5. La ausencia total de ejecución de la campaña de benchmarking de 20 iteraciones.
6. La preservación intacta del núcleo del SDK (`src/`, `include/`, `deps/`).
7. La verificación multiplataforma de CI con 8/8 trabajos exitosos en GitHub Actions sobre el commit exacto.

El Hito **BP-008D** queda plenamente preparado y sometido para su **cierre técnico formal y definitivo** como fase preparatoria del protocolo experimental, supeditado exclusivamente al dictamen de cierre formal por parte del auditor técnico independiente.
