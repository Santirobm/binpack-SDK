# BP-007A — Diagnóstico y Línea Base Global Integrada (Sanitizers & Memoria)

## 1. Entorno de Ejecución y Configuración de Compilación
- **Commit Base:** `9c4f3da886acd5688ef5bb3a38039fb873fbf3a6` (Rama `audit/bp1-core-safety`).
- **Plataformas de Prueba:**
  - Linux Debian WSL2 (Kernel 6.6.x x86_64).
  - Windows 10/11 x86_64 nativo (PowerShell).
- **Compiladores Utilizados:**
  - GCC 14.2.0 (Debian 14.2.0-19)
  - Clang 19.1.7 (Debian 19.1.7-1)
  - MinGW-w64 GCC 14.2.0 (x86_64-w64-mingw32-gcc)
- **Configuración de Flags por Sanitizer:**
  - **ASan + LSan:** `-fsanitize=address,leak -O1 -g -mavx2 -Iinclude -Isrc -Ideps/include -Ideps/lzma -D_7ZIP_ST`
  - **UBSan:** `-fsanitize=undefined -fsanitize=function -O1 -g -mavx2 -Iinclude -Isrc -Ideps/include -Ideps/lzma -D_7ZIP_ST`
  - **TSan:** `-fsanitize=thread -O1 -g -mavx2 -Iinclude -Isrc -Ideps/include -Ideps/lzma -D_7ZIP_ST` (ejecutado bajo `setarch x86_64 -R`).
- **Estado del Repositorio:** 0 archivos modificados en `src/`, `deps/`, `include/`, `tests/` o `Makefile`. Árbol de trabajo 100% limpio.

---

## 2. Matriz Global de Pruebas y Resultados por Sanitizer

| Suite de Pruebas | Casos | ASan (Address) | LSan (Leaks) | UBSan (Undefined) | TSan (Data Races) | Estado Global |
|---|---|---|---|---|---|---|
| **`test_sdk` (SDK Core)** | 14 | **PASS (0)** | **PASS (0)** | **13 advertencias (LZMA upstream)** | **PASS (0)** | Válido funcionalmente |
| **`test_bounds_safety` (BP-004)** | 45 | **PASS (0)** | **PASS (0)** | **PASS (0)** | **PASS (0)** | Limpio |
| **`test_patch_safety` (BP-006B)** | 18 | **PASS (0)** | **PASS (0)** | **PASS (0)** | **PASS (0)** | Limpio |
| **`test_dict_safety` (BP-006C)** | 15 | **PASS (0)** | **PASS (0)** | **PASS (0)** | **PASS (0)** | Limpio (TLS auto-destruct) |
| **`test_eval_safety` (BP-006D)** | 17 | **PASS (0)** | **PASS (0)** | **PASS (0)** | **PASS (0)** | Limpio (FLS/TLS auto-destruct) |
| **`test_archive_prefix` (BP-006A)** | 14 | **PASS (0)** | **PASS (0)** | **PASS (0)** | **PASS (0)** | Limpio |
| **`test_cpu_dispatch` (BP-003)** | 4 | **PASS (0)** | **PASS (0)** | **PASS (0)** | **PASS (0)** | Limpio |
| **`test_concurrency` (BP-005)** | 11 | **PASS (0)** | **HALLAZGO (15,98 MB en 170 allocs)** | **PASS (0)** | **PASS (0 races)** | Retención TLS en hilos efímeros |
| **`test_crc32_concurrency`** | 80k ops | **PASS (0)** | **PASS (0)** | **PASS (0)** | **PASS (0 races)** | Limpio |
| **`test_unaligned_access`** | 16k ops | **PASS (0)** | **PASS (0)** | **PASS (0)** | **PASS (0)** | Limpio |
| **`test_pe_elf_bounds`** | Sweep | **PASS (0)** | **PASS (0)** | **PASS (0)** | **PASS (0)** | Limpio |
| **TOTAL GENERAL** | **138+** | **0 errores** | **1 hallazgo reproducible** | **1 hallazgo upstream** | **0 data races** | **138/138 PASS funcional** |

---

## 3. Investigación Prioritaria A: Diagnóstico UBSan en Dependencia LZMA

### 3.1. Evidencia Reproducible
Al compilar con Clang 19 y `-fsanitize=undefined -fsanitize=function`:
```text
deps/lzma/LzmaEnc.c:2100:5: runtime error: call to function MatchFinder_Init through pointer to incorrect function type 'void (*)(void *)'
deps/lzma/LzFind.c:338: note: MatchFinder_Init defined here
SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior deps/lzma/LzmaEnc.c:2100:5 

deps/lzma/LzmaEnc.c:2115:9: runtime error: call to function MatchFinder_GetNumAvailableBytes through pointer to incorrect function type 'unsigned int (*)(void *)'
deps/lzma/LzFind.c:49: note: MatchFinder_GetNumAvailableBytes defined here

deps/lzma/LzmaEnc.c:929:14: runtime error: call to function Bt4_MatchFinder_GetMatches through pointer to incorrect function type 'unsigned int (*)(void *, unsigned int *)'
deps/lzma/LzFind.c:611: note: Bt4_MatchFinder_GetMatches defined here

deps/lzma/LzmaEnc.c:2120:17: runtime error: call to function MatchFinder_GetPointerToCurrentPos through pointer to incorrect function type 'const unsigned char *(*)(void *)'
deps/lzma/LzFind.c:47: note: MatchFinder_GetPointerToCurrentPos defined here

deps/lzma/LzmaEnc.c:1125:7: runtime error: call to function Bt4_MatchFinder_Skip through pointer to incorrect function type 'void (*)(void *, unsigned int)'
deps/lzma/LzFind.c:934: note: Bt4_MatchFinder_Skip defined here
```

### 3.2. Análisis Técnico
1. **Firma declarada en VTable:** En `deps/lzma/LzFind.h:91`:
   ```c
   typedef void (*Mf_Init_Func)(void *object);
   typedef UInt32 (*Mf_GetNumAvailableBytes_Func)(void *object);
   typedef const Byte * (*Mf_GetPointerToCurrentPos_Func)(void *object);
   typedef UInt32 (*Mf_GetMatches_Func)(void *object, UInt32 *distances);
   typedef void (*Mf_Skip_Func)(void *object, UInt32);
   ```
   Todas declaran el primer argumento como puntero genérico `void *object`.
2. **Firma efectiva de las funciones:** En `deps/lzma/LzFind.c`:
   ```c
   void MatchFinder_Init(CMatchFinder *p);
   UInt32 MatchFinder_GetNumAvailableBytes(CMatchFinder *p);
   const Byte * MatchFinder_GetPointerToCurrentPos(CMatchFinder *p);
   UInt32 Bt4_MatchFinder_GetMatches(CMatchFinder *p, UInt32 *distances);
   void Bt4_MatchFinder_Skip(CMatchFinder *p, UInt32 num);
   ```
   Todas implementan el primer argumento tipado como `CMatchFinder *p`.
3. **Punto de conversión:** En `deps/lzma/LzFind.c:1029-1065` (`MatchFinder_CreateVTable`):
   ```c
   vTable->Init = (Mf_Init_Func)MatchFinder_Init;
   vTable->GetNumAvailableBytes = (Mf_GetNumAvailableBytes_Func)MatchFinder_GetNumAvailableBytes;
   ...
   ```
4. **Punto de invocación indirecta:** En `deps/lzma/LzmaEnc.c:2100`:
   `p->matchFinder.Init(p->matchFinderObj);`
5. **Evaluación de Estándar C vs. ABI x86_64:**
   - **Estándar ISO C (§6.5.2.2.p9):** Invocar una función mediante un puntero a una función incompatible produce comportamiento indefinido. En C, `void*` y `struct*` no son tipos de función compatibles.
   - **ABI x86_64 (Linux SysV y Windows x64):** Todos los punteros (`void*`, punteros a estructuras u objetos) comparten exactamente el mismo tamaño (64 bits), misma representación binaria y se transfieren en el mismo registro general (`%rdi` en SysV, `%rcx` en Windows x64). Por tanto, a nivel de máquina x86_64 **no existe riesgo de desalineación de pila, truncamiento ni corrupción de registros**.
6. **Origen / Clasificación:** **Defecto heredado de Upstream (7-Zip LZMA SDK v19.00 de Igor Pavlov)**. No es una alteración introducida por BinPack. Se recomienda para BP-007B una adaptación no invasiva de firmas (wrapper inline o unificación de tipos) sin alterar el algoritmo LZMA.

---

## 4. Investigación Prioritaria B: Diagnóstico de Retención TLS y Fugas en Hilos Efímeros

### 4.1. Evidencia Reproducible bajo LeakSanitizer (LSan)
Al ejecutar la suite de concurrencia `tests/test_concurrency.c` bajo `-fsanitize=address,leak`:
```text
=================================================================
==ERROR: LeakSanitizer: detected memory leaks

Direct leak of 1052672 byte(s) in 18 object(s) allocated from:
    #0 realloc .../asan_malloc_linux.cpp:85
    #1 bp_acquire_core_scratch src/binpack_core.c:431

Direct leak of 380736 byte(s) in 72 object(s) allocated from:
    #0 malloc .../asan_malloc_linux.cpp:69
    #1 ZSTD_createCCtx_advanced (libzstd.so.1)
    #2 binpack_compress_into_fast / binpack_compress_ex src/binpack_sdk.c:230

Direct leak of 8192 byte(s) in 4 object(s) allocated from:
    #0 realloc .../asan_malloc_linux.cpp:85
    #1 bp_acquire_sdk_scratch src/binpack_sdk.c:119

Indirect leak of 14533079 byte(s) in 72 object(s) allocated from:
    (Tablas internas de búsqueda de coincidencias y secuencias de ZSTD_createCCtx)

Indirect leak of 8192 byte(s) in 4 object(s) allocated from:
    (bp_acquire_core_scratch)

SUMMARY: AddressSanitizer: 15982871 byte(s) leaked in 170 allocation(s).
```

### 4.2. Clasificación del Ciclo de Vida de los Workspaces TLS

| Puntero TLS | Ubicación | Tamaño / Asignaciones | Categoría 1: Intencional durante la vida del hilo | Categoría 2: Liberación al terminar el hilo | Categoría 3: Fuga permanente tras terminación |
|---|---|---|---|---|---|
| `s_core_scratch` | `src/binpack_core.c:423, 431` | **1.060.864 bytes** (18 hilos) | **SÍ.** Evita `VirtualAlloc`/`mmap` en hot path hasta 64 MB. | **NO.** Sin destructor POSIX (`pthread_key_create`) ni Windows (`FlsAlloc`). | **FUGADO.** Al morir el hilo, la memoria queda permanentemente huérfana en el heap. |
| `s_fast_cctx` | `src/binpack_sdk.c:111, 230` | **14.913.815 bytes** (~14,2 MiB) | **SÍ.** Reutiliza tablas ZSTD para compresión multi-hilo en ráfaga. | **NO.** Sin destructor de hilo. | **FUGADO.** 72 contextos retenidos por hilos efímeros que finalizaron. |
| `s_sdk_scratch` | `src/binpack_sdk.c:112, 119` | **8.192 bytes** (4 hilos) | **SÍ.** Búfer intermedio para transformaciones de bloque. | **NO.** Sin destructor de hilo. | **FUGADO.** Retenido permanentemente al terminar el hilo. |
| `s_dict_tls` | `src/binpack_dict.c` | 0 bytes | **SÍ.** Protegido por BP-006C. | **SÍ.** Reclamado por OS destructor. | **0 bytes.** Limpio. |
| `s_eval_tls` | `src/binpack_eval.c` | 0 bytes | **SÍ.** Protegido por BP-006D. | **SÍ.** Reclamado por OS destructor. | **0 bytes.** Limpio. |

### 4.3. Conclusión del Diagnóstico TLS
- **Diagnóstico confirmatorio:** Los ~15,98 MB reportados por LSan provienen **exclusivamente de hilos efímeros** creados por `test_concurrency` que consumieron `s_core_scratch`, `s_sdk_scratch` y `s_fast_cctx`.
- **Riesgo comercial:** En aplicaciones de servidor o motores de juego que empleen un pool de hilos permanente, la memoria no crece (se estabiliza en la meseta observada en el soak test). Sin embargo, en arquitecturas que creen y destruyan hilos dinámicamente (hilos efímeros), el SDK acumula aproximadamente **850 KiB por cada hilo que invoque compresión rápida**.
- **Solución arquitectónica recomendada para BP-007C:** Extender el patrón unificado de destructores TLS/FLS implementado en BP-006C y BP-006D hacia `binpack_core.c` y `binpack_sdk.c`.

---

## 5. Medición de Memoria y Comportamiento de Estrés (Soak Test)

Bajo la suite de estrés `test_concurrency` (10.000 operaciones simultáneas multi-hilo bajo ThreadSanitizer):
- **Memoria inicial (Warmup):** **65.116 KB**.
- **Memoria tras Ronda 1 (5.000 ops):** **65.916 KB** ($\Delta = +800\text{ KB}$ correspondiente a la inicialización de los hilos de trabajo).
- **Memoria tras Ronda 2 (10.000 ops):** **65.916 KB** ($\Delta = \mathbf{0\text{ KB}}$ exactos).
- **Crecimiento monotónico:** **0%**. La memoria se estabiliza de forma absolutamente plana una vez inicializado el workspace de cada hilo.
- **Data Races:** **0 carreras de datos** reportadas por ThreadSanitizer en 10.000 operaciones mixtas.

---

## 6. Dictamen de Cierre de BP-007A
1. **Fotografía Global Completa:** Se ejecutaron las 138 pruebas del catálogo y los arneses de estrés masivo. La funcionalidad de compresión, descompresión, streaming, archivos, parches y diccionarios está 100% verificada.
2. **Defectos Aislados y Clasificados:**
   - El hallazgo UBSan en LZMA está atribuido con certeza a código upstream de 7-Zip SDK.
   - El hallazgo LSan de ~16 MB está acotado exactamente a la ausencia de destructores en `s_core_scratch`, `s_sdk_scratch` y `s_fast_cctx` para hilos efímeros.
3. **Integridad del Repositorio:** El árbol de trabajo permanece limpio sin modificaciones de código ni commits.
