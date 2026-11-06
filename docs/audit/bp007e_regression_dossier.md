# BP-007E — Dossier Consolidado: Regresión Integrada Final y Cierre Global de BP-007

**Estado:** Aprobado Técnicamente / Listo para Cierre Global  
**Rama:** `audit/bp1-core-safety`  
**Commit Auditado:** `0cf7be42bb49dfef920285891ada0cc3a691c63a`  
**Árbol Git:** Limpio (`working tree clean`, 0 archivos modificados en `src/`, `include/` o `deps/`).  

---

## 1. Resumen Ejecutivo del Ciclo BP-007

El ciclo de auditoría y endurecimiento **BP-007** tuvo como misión diagnosticar, certificar y sanear integralmente el comportamiento en tiempo de ejecución del SDK de BinPack bajo sanitizers de compilador (ASan, LSan, UBSan, TSan) en arquitecturas POSIX (Linux x86_64) y Windows Nativo (MinGW-w64).

A lo largo del ciclo se lograron los siguientes hitos:
1. **BP-007A (Baseline Global Integrado):** Caracterización de 138 pruebas históricas, identificando dos hallazgos prioritarios: 13 advertencias UBSan por llamada indirecta a través de punteros de función incompatibles en la vtable de LZMA, y 15.982.871 bytes retenidos en hilos efímeros en `test_concurrency` por variables TLS sin destructor.
2. **BP-007B (Saneamiento LZMA / UBSan):** Especialización formal de `IMatchFinder` para monohilo (`_7ZIP_ST`) con tipado estricto `CMatchFinder*` y `Byte*`. Eliminación de todos los casts forzados en la asignación de la vtable, preservando el layout de 40 bytes. **13/13 advertencias UBSan eliminadas a 0.**
3. **BP-007C (Ciclo de Vida TLS/FLS y Auto-Destructores):** Reemplazo de variables desnudas `__thread` por estructuras encapsuladas con auto-destructores nativos (`pthread_key_create` en POSIX y `FlsAlloc` en Windows). Retención de memoria en hilos efímeros reducida de 15,98 MB a **0 bytes netos**.
4. **BP-007D (Soak Prolongado y Estrés Multihilo):** Batería de estrés de más de 579.000 operaciones con churn de hilos, pools persistentes, saturación mixta, frontera de 64 MiB y validación round-trip byte por byte sin discrepancias.
5. **BP-007E (Regresión Integrada Final):** Certificación global de todas las suites históricas y modernas bajo todos los perfiles de compilación y ejecución.

---

## 2. Matriz de Regresión Funcional Integrada

De acuerdo con el mandato de auditoría, las pruebas históricas (138 casos) y las nuevas suites de seguridad se contabilizan e informan con estricta distinción y separación:

### 2.1. Suites Históricas de Regresión (138 Casos)

| Suite de Pruebas | Archivo Fuente | Casos Evaluados | PASS | FAIL | Sanitizers Verificados |
|---|---|---|---|---|---|
| **SDK Core API** | `tests/test_sdk.c` | **14** | 14 | 0 | GCC, Clang, ASan, LSan, UBSan (0 UB) |
| **Prefijos Rodantes & V-08** | `tests/test_archive_prefix.c` | **14** | 14 | 0 | GCC, Clang, ASan, LSan, UBSan |
| **Parche Diferencial OTA** | `tests/test_patch_safety.c` | **18** | 18 | 0 | GCC, Clang, ASan, LSan, UBSan |
| **Motor de Diccionarios** | `tests/test_dict_safety.c` | **15** | 15 | 0 | GCC, Clang, ASan, LSan, UBSan |
| **Evaluación de Filtros** | `tests/test_eval_safety.c` | **17** | 17 | 0 | GCC, Clang, ASan, LSan, UBSan |
| **Límites de Seguridad (Bounds)** | `tests/test_bounds_safety.c` | **45** | 45 | 0 | GCC, Clang, ASan, LSan, UBSan |
| **Concurrencia y Multihilo** | `tests/test_concurrency.c` | **11** | 11 | 0 | GCC, Clang, LSan (0 fugas), TSan (0 races) |
| **Despacho SIMD (CPU)** | `tests/test_cpu_dispatch.c` | **4** | 4 | 0 | GCC, Clang, AVX2 bit-for-bit equivalence |
| **SUBTOTAL HISTÓRICO** | **8 suites integradas** | **138** | **138** | **0** | **100% PASS** |

### 2.2. Nuevas Suites de Seguridad y Fatiga (BP-007C / BP-007D)

| Suite Nueva | Archivo Fuente | Tipo / Cobertura | Casos / Fases | Resultado |
|---|---|---|---|---|
| **TLS & FLS Lifecycle** | `tests/test_tls_safety.c` | Contabilidad atómica, balance neto, reuso pool, FLS fibers | **5 tests** | **PASS (5/5)** en Linux & Windows Nativo |
| **Soak & Thread Stress** | `tests/test_soak_stress.c` | Churn efímero, pool persistente, saturación mixta, frontera 64MB | **5 fases (>579k ops)** | **PASS** en LSan, UBSan, TSan y Win32 |
| **SUBTOTAL SUITES NUEVAS** | **2 suites adicionales** | **Lifecycle & Soak** | **10 verificaciones** | **100% PASS** |

---

## 3. Auditoría de Compilación, Toolchains y Portabilidad

Se verificó el comportamiento de compilación con flags estrictos (`-Wall -Wextra -Werror`):

| Toolchain / Compilador | Código Auditado | Flags Evaluados | Diagnóstico y Aislamiento Técnico | Estado |
|---|---|---|---|---|
| **GCC 14.2 (Debian Linux)** | `src/*.c` (con `-DBINPACK_TESTING`) | `-Wall -Wextra -Werror` | Compilación limpia, 0 advertencias, 0 errores. | **PASS** |
| **GCC 14.2 (Debian Linux)** | `src/*.c` (modo productivo) | `-Wall -Wextra` | `had_prev` advertido como no usado en `core.c`/`sdk.c` (variable de instrumentación) y `-Wtype-limits` en `archive.c:74`. | **Aislado** |
| **Clang 19.1.7 (Debian Linux)**| `src/*.c` (con `-DBINPACK_TESTING`) | `-Wall -Wextra -Werror` | Compilación limpia, 0 advertencias, 0 errores. | **PASS** |
| **MinGW-w64 GCC 14.2 (Win64)**| `src/*.c` (con `-DBINPACK_TESTING`) | `-O2 -mavx2 -Wall` | Compilación limpia y generación binaria exitosa de ejecutables `.exe`. | **PASS** |
| **Código Terceros (`deps/lzma`)**| `deps/lzma/LzmaEnc.c` | `-Wall -Wextra` | Advertencias de upstream (Igor Pavlov 7-Zip v19): `-Wmisleading-indentation` y `-Wdangling-pointer` (GCC), `-Wempty-body` (Clang). Sin cambios productivos. | **Aislado** |

> [!NOTE]
> En estricto cumplimiento con las restricciones obligatorias de BP-007E, **no se modificó ninguna línea de `src/`, `include/` ni `deps/`** para enmascarar o suprimir artificialmente estas advertencias de upstream.

---

## 4. Verificación de Sanitizers: Antes vs. Después

| Aspecto Evaluado | Estado en Línea Base (BP-007A) | Estado en BP-007E (Final) | Solución Técnica Aplicada |
|---|---|---|---|
| **UBSan: VTable LZMA** | 13 advertencias de tipo en `LzFind.h` / `LzmaEnc.c` | **0 advertencias (Limpio)** | Especialización estricta de `IMatchFinder` monohilo (BP-007B). |
| **LSan: Fuga TLS Efímero**| 15.982.871 bytes retenidos en `test_concurrency` | **0 bytes netos (Limpio)** | Auto-destructores nativos `pthread_key` y Windows `FlsAlloc` (BP-007C). |
| **Windows Fibers (FLS)** | Sin cobertura cuantitativa de FLS callbacks | **Validado cuantitativamente** | Test 5 verifica ejecución de destructor FLS tras `DeleteFiber` (BP-007C). |
| **TSan: Concurrencia** | 0 carreras en pruebas base | **0 carreras** en `test_concurrency`, `test_tls_safety`, `test_soak_stress`. | Exclusión mutua y atomicidad preservadas. |
| **RSS Stability** | No medido bajo churn sostenido | **$\Delta\text{RSS} \le 8\text{ KB}$ en Windows**, meseta plana tras warm-up. | Reutilización de contextos Zstd y scratchpad (BP-007D). |

---

## 5. Delimitación y Alcance de la Certificación

En consonancia con las buenas prácticas de ingeniería y auditoría de software:
- **Alcance Certificado:** Se certifica que en las plataformas probadas (Linux Debian 6.6 x86_64 y Windows 10/11 x86_64 nativo), bajo los arneses de prueba descritos y con los compiladores GCC 14, Clang 19 y MinGW-w64, el SDK BinPack no presenta fugas de recursos TLS/FLS, no incurre en comportamiento indefinido en la vtable LZMA ni genera condiciones de carrera en los escenarios concurrentes ejecutados.
- **Congelamiento de Interfaz:** Se certifica que `include/binpack_sdk.h` no ha sufrido ninguna modificación de API, ABI o firma pública a lo largo de BP-007. El formato binario, el framing de archivos y el protocolo de parches se mantienen 100% compatibles hacia atrás y hacia adelante.
- **Árbol de Trabajo:** Limpio, sincronizado con `origin/audit/bp1-core-safety` en el commit auditado `0cf7be42bb49dfef920285891ada0cc3a691c63a`.
