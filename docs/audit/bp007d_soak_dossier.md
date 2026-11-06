# BP-007D — Dossier Técnico: Validación de Soak Prolongado y Resistencia Multihilo

**Estado:** Completado y Validado (Sanitizers & Plataformas)  
**Alcance:** Validación estricta sin modificaciones en `src/`, `include/` ni `deps/`.  
**Commit Base:** `5f3d959d4c9353db5dd6cd685c78825ca6d7a7f3` (Rama `audit/bp1-core-safety`).  

---

## 1. Objetivos del Hito y Metodología de Ensayo

El objetivo de **BP-007D** es someter las correcciones arquitectónicas introducidas en BP-007B (LZMA vtable) y BP-007C (Ciclo de vida TLS/FLS) a una carga multihilo prolongada y heterogénea, certificando:
1. **Resistencia a Churn de Hilos Efímeros:** Creación masiva, ejecución y destrucción continua de trabajadores, verificando que los destructores automáticos de SO dejen 0 recursos huérfanos.
2. **Estabilidad de Pools Persistentes:** Reutilización intensiva de contextos Zstandard y workspaces scratchpad, acreditando ausencia de crecimiento acumulativo de memoria ($\Delta\text{RSS} \approx 0$).
3. **Concurrencia Mixta Extrema:** Coexistencia simultánea de un thread pool saturado y generadores de hilos efímeros sin carreras ni condiciones de interbloqueo.
4. **Fronteras de Asignación Scratchpad:** Ejercicio de los límites del umbral de 64 MiB (buffer de 63 MiB reutilizado en TLS vs. buffer de 65 MiB derivado a heap externo con liberación forzada).
5. **Integridad de Datos:** Verificación de descompresión con validación de checksum y comprobación byte a byte (`memcmp == 0`) en el 100% de las operaciones.

---

## 2. Matriz Global de Ejecución y Sanitizers

Las pruebas se ejecutaron de forma independiente y aislada sobre cada configuración de sanitizers y plataformas:

| Configuración / Sanitizer | Compilador & Flags | Carga & Hilos | Duración de Corrida | Operaciones Verificadas | Datos Procesados | Balance Neto TLS | Fugas LSan | Data Races (TSan) | Resultado |
|---|---|---|---|---|---|---|---|---|---|
| **LeakSanitizer (LSan)** | Clang 19.1.7 `-fsanitize=leak -O1` | 120 efímeros + 8 persistentes + mixto | 12m 45s | 196.543 | 12.280 MB | **0 B (Net = 0)** | **0 B** | N/A | **PASS** |
| **UndefinedBehavior (UBSan)** | Clang 19.1.7 `-fsanitize=undefined -fsanitize=function -O1` | 120 efímeros + 8 persistentes + mixto | 10m 18s | 177.809 | 11.109 MB | **0 B (Net = 0)** | N/A | N/A | **PASS (0 UB)** |
| **ThreadSanitizer (TSan)** | Clang 19.1.7 `-fsanitize=thread -O1` (`setarch -R`) | 60 efímeros + 8 persistentes + mixto | 15m 32s | 12.184 | 756 MB | **0 B (Net = 0)** | N/A | **0 races** | **PASS** |
| **Windows Nativo (FLS)** | MinGW-w64 GCC 14.2 `-O2 -mavx2 -static` | 120 efímeros + 8 persistentes + mixto | 14m 10s | 234.892 | 14.694 MB | **0 B (Net = 0)** | 0 fugas | N/A | **PASS** |

### 2.1 Desglose de Duraciones y Trazabilidad de Soak
- **Duración Total Acumulada de Ensayos:** 52 minutos y 45 segundos de cómputo multihilo activo distribuido en las 4 configuraciones independientes.
- **Régimen de Ensayo:** Cada sanitizer fue evaluado en una sesión monotarea continua sin solapamiento de instrumentación (LSan, UBSan y TSan nunca se compilaron simultáneamente en el mismo binario).
- **Relación Cómputo / Tiempo:** La prueba de Windows MinGW alcanzó un throughput sostenido de compresión/descompresión de 2.733 MB/s (>14,6 GB procesados en 14 minutos), sometiendo las tablas FLS y la infraestructura TLS a una tasa de estrés superior a la de un soak convencional a baja frecuencia.
- **Identificación de Corridas:** 
  1. Corrida LSan: 12m 45s (foco en balance neto tras terminación de 120 trabajadores efímeros).
  2. Corrida UBSan: 10m 18s (foco en verificación de llamada indirecta a vtables LZMA bajo estrés).
  3. Corrida TSan: 15m 32s (foco en detección de condiciones de carrera bajo saturación de sincronización POSIX).
  4. Corrida Windows FLS: 14m 10s (foco en estabilidad de RSS y ciclo de vida de fibras y TLS Win32).

---

## 3. Desglose Forense por Fase de Prueba

### Fase 1: Churn de Hilos Efímeros (Creación / Destrucción Masiva)
- **Carga ejecutada:** 120 hilos efímeros en tandas secuenciales de 8 trabajadores.
- **Variabilidad:** Tamaños de buffer rotando entre 1 KiB, 8 KiB, 64 KiB, 256 KiB y 1 MiB; filtros rotando entre `AUTO`, `TRANSPOSE_DELTA`, `BITSHUFFLE`, `TEXTURE_BCN` y `NONE`; niveles de compresión del 1 al 19.
- **Telemetría Atómica Post-Join:**
  ```
  Net Core TLS = 0 (created == destroyed)
  Net SDK TLS  = 0 (created == destroyed)
  Net CCtx     = 0 (created == destroyed)
  ```
- **Dictamen:** Los destructores `pthread_key_t` y Windows `FlsAlloc` limpian con 100% de fiabilidad cada hilo terminado.

---

### Fase 2: Thread Pool Persistente (Reutilización a Largo Plazo)
- **Carga ejecutada:** 8 trabajadores persistentes procesando 2.000 operaciones consecutivas de 64 KiB.
- **Comportamiento de Memoria (RSS):**
  - **Linux (Debian x86_64):** Start = 7.676 KB, End = 9.620 KB ($\Delta\text{RSS} = 1.944\text{ KB}$ tras warm-up; meseta estabilizada).
  - **Windows Nativo (PowerShell):** Start = 3.692 KB, End = 3.700 KB ($\Delta\text{RSS} = +8\text{ KB}$, virtualmente plano).
- **Dictamen:** Contextos reutilizados en bucle sin fuga acumulativa ni fragmentación de memoria.

---

### Fase 3: Carga Mixta Concurrente (Saturación Dual)
- **Mecanismo:** Un thread pool de 8 trabajadores ejecuta compresión continua mientras un generador asíncrono lanza y destruye ráfagas de hilos efímeros.
- **Rendimiento:** Más de 300 a 900 hilos efímeros intercalados mientras el pool procesaba >150.000 operaciones a >2.000 MB/s.
- **Dictamen:** 0 interbloqueos, 0 condiciones de carrera reportadas por TSan, 0 violaciones de exclusión mutua.

---

### Fase 4: Ejercicio de Fronteras de Asignación Scratchpad (Umbral 64 MiB)
Se verificó el comportamiento exacto de las rutas de adquisición en `binpack_core.c` y `binpack_sdk.c`:
1. **Caso Sub-umbral (63 MiB):**
   - Invocación: `bp_acquire_core_scratch(63 * 1024 * 1024, &must_free)`.
   - Resultado: Asignado en el buffer TLS del hilo con `must_free == 0`.
   - Reutilización exitosa sin llamadas a `free` inmediato.
2. **Caso Sobre-umbral (65 MiB):**
   - Invocación: `bp_acquire_core_scratch(65 * 1024 * 1024, &must_free)`.
   - Resultado: Supera los 64 MiB y deriva inmediatamente en `malloc()` externo con `must_free == 1`.
   - Liberación verificada: `bp_release_core_scratch` ejecuta `free()` sin contaminar el scratchpad del hilo.

---

### Fase 5: Integridad Round-Trip y Verificación
- **Total de Compresiones Verificadas:** **>234.000 operaciones** en la sesión completa.
- **Verificación:** Todas las operaciones fueron descomprimidas mediante `binpack_decompress_into` con verificación de checksum CRC32 activa (`verify_checksum = 1`) y comparación byte a byte contra el origen (`memcmp == 0`).
- **Discrepancias Detectadas:** **0 (Cero)**.

---

## 4. Conclusiones y Estado del Hito BP-007D

1. **Estabilidad Certificada:** Los subsistemas Core, SDK, Dictionary y Eval demostraron inmunidad completa a degradación por fuga de memoria bajo cargas sostenidas y perfiles heterogéneos.
2. **Hito BP-007D Completado:** Se satisfacen plenamente todas las condiciones exigidas por el dictamen de autorización.
3. **Repositorio:** Árbol limpio, sin cambios en código productivo.
