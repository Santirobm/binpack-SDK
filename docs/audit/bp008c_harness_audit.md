# Auditoría del Harness y Adaptación Metodológica — Hito BP-008C (Revisión R4)

**Expediente:** BP-008C-R4 — Subsanación Definitiva: Robustez de Contextos Zstandard y Transparencia de Frontera en Topología Virtualizada  
**Proyecto:** BinPack Enterprise SDK  
**Ramas Auditadas:** `audit/bp1-core-safety`  
**Commit Base:** `123a97d0df1504a87264ebecc672bbd1764d38da`  
**Fecha:** 6 de Noviembre de 2026  
**Responsable Técnico:** Antigravity AI — Advanced Agentic Coding & Systems Architecture  

---

## 1. Resumen Ejecutivo y Respuesta a la Orden de Trabajo BP-008C-R4

En respuesta al dictamen técnico emitido sobre **BP-008C-R3**, se procedió al cierre técnico y metodológico definitivo de las dos observaciones señaladas:

1. **Gestión Robusta de Contextos Zstandard:** Se incorporó la verificación obligatoria de punteros no nulos en `ZSTD_createCCtx()` y `ZSTD_createDCtx()`, así como la comprobación exhaustiva del código de retorno en todas las llamadas a `ZSTD_CCtx_reset()`, tanto en `measure_latencies_generic()` como en `run_zstd_l1()` y `run_zstd_l19()`. Ante cualquier fallo de asignación o de reinicio de sesión, se liberan los recursos asignados y se propaga un fallo controlado (`return -1;`), erradicando cualquier riesgo de terminación abrupta.
2. **Eliminación de la Sonda Externa y Transparencia de la Frontera de Virtualización:** Se eliminó por completo el mecanismo alternativo de sondeo hacia el host Windows. Se reconoció formalmente el principio arquitectónico señalado por la auditoría: una sonda en el host Windows no demuestra que el vCPU 2 asignado por el hipervisor al kernel invitado de WSL2 corresponda de manera estática al CPU lógico 2 de Windows. En su lugar, el harness verifica la presencia del hipervisor mediante la bandera estándar x86 `CPUID.1:ECX[31]`, audita la fijación estricta a nivel del kernel invitado (`sched_setaffinity` al vCPU 2, `CPU_COUNT == 1`, `sched_yield()`), y declara explícitamente en el banner y en el dossier la condición de ejecución: **"WSL2/Hyper-V Virtual Guest (vCPU 2, Sysfs Core ID 1, Hypervisor Topology Abstraction)"**. En entornos bare-metal no virtualizados, se exige de forma inviolable `CPUID 0x1A: 0x40`.

Se mantuvieron de forma estricta las restricciones de alcance del proyecto:
- **Cero modificaciones** a `src/`, `include/` ni `deps/`.
- **Preservación histórica íntegra:** se conservan intactos `decisive_benchmark_raw_pilot.csv`, `decisive_benchmark_raw_r1.csv`, `decisive_benchmark_raw_r2.csv` y `decisive_benchmark_raw_r3.csv`.
- **Nuevo registro certificado:** generado en `docs/audit/decisive_benchmark_raw_r4.csv`.

---

## 2. Detalle de las Subsanaciones Técnicas Implementadas

### 2.1 Robustez en la Gestión de Contextos Zstandard
En [`measure_latencies_generic()`](file:///C:/Users/sgi.lm/Desktop/binpack-SDK/tests/benchmarks/benchmark_decisive_suite.c#L508-L585):
- Se valida que `zcctx` y `zdctx` sean no nulos tras su creación:
  ```c
  if (is_zstd) {
      zcctx = ZSTD_createCCtx();
      zdctx = ZSTD_createDCtx();
      if (!zcctx || !zdctx) {
          if (zcctx) ZSTD_freeCCtx(zcctx);
          if (zdctx) ZSTD_freeDCtx(zdctx);
          free(cbuf); free(dbuf);
          return -1;
      }
  }
  ```
- En cada iteración del bucle de bloques, se verifica el retorno de `ZSTD_CCtx_reset()`:
  ```c
  size_t rst = ZSTD_CCtx_reset(zcctx, ZSTD_reset_session_only);
  if (ZSTD_isError(rst)) { success = 0; break; }
  ```
- El mismo estándar se extendió a [`run_zstd_l1()`](file:///C:/Users/sgi.lm/Desktop/binpack-SDK/tests/benchmarks/benchmark_decisive_suite.c#L670-L745) y [`run_zstd_l19()`](file:///C:/Users/sgi.lm/Desktop/binpack-SDK/tests/benchmarks/benchmark_decisive_suite.c#L910-L985), verificando las asignaciones iniciales y comprobando `!ZSTD_isError(rst)` tanto en el warm-up como en las 20 iteraciones cronometradas.

### 2.2 Delimitación Metodológica de la Topología de CPU
En [`detect_and_set_pcore_affinity()`](file:///C:/Users/sgi.lm/Desktop/binpack-SDK/tests/benchmarks/benchmark_decisive_suite.c#L40-L125):
- Se eliminó la compilación y ejecución de la sonda externa `pcore_probe.exe`.
- El algoritmo opera exclusivamente dentro del espacio del kernel y la arquitectura x86:
  1. `sched_setaffinity` al Logical Core 2.
  2. Verificación estricta de la máscara con `sched_getaffinity`: exige `CPU_ISSET(2)` y `CPU_COUNT == 1`.
  3. Sincronización del planificador mediante `sched_yield()`.
  4. Consulta de topología nativa en sysfs (`core_id = 1`, par SMT 2-3).
  5. Consulta de `CPUID Leaf 0x1A`: si `core_type == 0x40`, se certifica como P-Core en silicio nativo (bare-metal).
  6. Si `core_type != 0x40`, se consulta la bandera de hipervisor en `CPUID.1:ECX[31]`. Si el entorno es virtualizado (Hyper-V / WSL2), se audita y declara con rigor científico la frontera de virtualización, reconociendo que Hyper-V enmascara las hojas superiores a `0x15` y gestiona la planificación de los procesadores virtuales. Si el entorno fuera bare-metal y no devolviera `0x40`, la suite aborta inmediatamente.

---

## 3. Resultados Consolidados de la Campaña BP-008C-R4

Resultados certificados obtenidos tras la ejecución aleatorizada bajo semilla registrada `20261106`:

### Dataset 1: `busybox_armv7l` (1.109.128 Bytes / 1.11 MB — Binario ARMv7-A)
| Motor / Configuración | Ratio (%) | Compresión (MB/s ± IQR) | Descompresión (MB/s ± IQR) | Lat 4K (C / D $\mu$s) | Pico RSS | Integridad |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **LZ4 v1.10 (RawBlock)** | 80.49% | 706.5 ± 13.3 | 5221.9 ± 158.4 | 6.8 / 1.0 | 77.456 KB | **PASS** |
| **BinPack (Fast Zero-CRC)** | 61.66% | 365.9 ± 4.2 | 1037.1 ± 48.8 | 41.6 / 7.1 | 79.460 KB | **PASS** |
| **Tier 3: Ruta C (BinPack Archival)** | 42.48% | 4.5 ± 0.4 | 61.2 ± 2.8 | — | 92.584 KB | **PASS** |
| **BinPack (Default L19)** | 48.49% | 5.3 ± 0.5 | 640.7 ± 11.5 | — | 112.580 KB | **PASS** |
| **BinPack (Convenience API)** | 61.66% | 389.0 ± 4.3 | 1073.1 ± 21.7 | — | 79.148 KB | **PASS** |
| *Tier 3: Ruta A (LZMA1 Puro)* | 44.80% | 4.7 ± 0.4 | 61.1 ± 1.1 | — | 90.196 KB | **PASS** |
| *Tier 3: Ruta B (BCJ + LZMA1)* | 42.46% | 4.6 ± 0.3 | 63.0 ± 2.5 | — | 91.312 KB | **PASS** |
| **BinPack (Strict CRC32)** | 61.66% | **— (N/A)** | 783.9 ± 14.8 | — | 79.140 KB | **PASS** |
| **Zstd v1.5.7 (L19 Frame)** | 51.04% | 5.5 ± 0.6 | 902.5 ± 28.8 | — | 111.104 KB | **PASS** |
| **Zstd v1.5.7 (L1 Frame)** | 64.26% | 500.9 ± 4.8 | 1390.7 ± 38.2 | 13.2 / 5.7 | 78.304 KB | **PASS** |

### Dataset 2: `nasa_turbofan_f32` (1.980.576 Bytes / 1.98 MB — Telemetría Float32)
#### A. Evaluación Multicanal Entrelazada (24 canales físicos multiplexados)
| Motor / Configuración | Ratio (%) | Compresión (MB/s ± IQR) | Descompresión (MB/s ± IQR) | Pico RSS | Integridad | Comportamiento |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| **Gorilla (24-ch Interleaved)** | 106.18% | 49.6 ± 2.9 | 220.7 ± 1.2 | 79.020 KB | **PASS** | Expansión (+6.18%) por ruptura de autocorrelación |
| **Chimp (24-ch Interleaved)** | 106.18% | 58.2 ± 0.6 | 210.7 ± 1.9 | 79.020 KB | **PASS** | Expansión (+6.18%) |
| **BinPack (Transpose-F32)** | **31.02%** | 4.1 ± 0.4 | **1059.6 ± 14.7** | 112.420 KB | **PASS** | Compresión 3.2x vía transposición en 4 carriles |

#### B. Evaluación Univariada Desentrelazada (Canal Individual: Sensor 0, 82.524 Bytes)
| Motor / Configuración | Ratio (%) | Compresión (MB/s ± IQR) | Descompresión (MB/s ± IQR) | Pico RSS | Integridad | Comportamiento |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| **Gorilla (Single-Ch 0)** | 104.96% | 44.0 ± 0.8 | 221.2 ± 5.9 | 75.392 KB | **PASS** | Expansión (+4.96%) por ruido en mantisas IEEE 754 |
| **Chimp (Single-Ch 0)** | 105.00% | 51.8 ± 1.1 | 218.3 ± 3.3 | 75.392 KB | **PASS** | Expansión (+5.00%) |
| **BinPack (Single-Ch 0)** | **29.67%** | 4.6 ± 0.0 | **849.0 ± 37.6** | 78.660 KB | **PASS** | Compresión 3.4x colapsando exponentes idénticos |

*Nota de Auditoría Tier 4B:* Sprintz I16 permanece estrictamente excluido y en cuarentena por Undefined Behavior aritmético.

### Dataset 3: `silesia_mozilla` (51.220.480 Bytes / 51.22 MB — Ejecutable Binario x86)
| Motor / Configuración | Ratio (%) | Compresión (MB/s ± IQR) | Descompresión (MB/s ± IQR) | Lat 4K (C / D $\mu$s) | Pico RSS | Integridad |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **BinPack (Strict CRC32)** | 39.08% | **— (N/A)** | 989.7 ± 10.4 | — | 200.000 KB | **PASS** |
| **LZ4 v1.10 (RawBlock)** | 51.49% | 816.7 ± 17.3 | 4071.2 ± 76.6 | 4.5 / 0.9 | 151.376 KB | **PASS** |
| **Zstd v1.5.7 (L1 Frame)** | 38.99% | 628.3 ± 9.5 | 1531.8 ± 32.8 | 10.8 / 4.8 | 146.144 KB | **PASS** |
| **BinPack (Fast Zero-CRC)** | 39.08% | 453.9 ± 10.0 | 1493.9 ± 8.6 | 30.0 / 6.1 | 205.600 KB | **PASS** |
| **BinPack (Default L19)** | 30.03% | 3.7 ± 0.1 | 813.7 ± 20.1 | — | 1.208.000 KB | **PASS** |
| **Zstd v1.5.7 (L19 Frame)** | 29.41% | 3.9 ± 0.4 | 1176.9 ± 24.3 | — | 223.424 KB | **PASS** |

### Dataset 4: `silesia_samba` (21.606.400 Bytes / 21.61 MB — Código Fuente C Estructurado)
| Motor / Configuración | Ratio (%) | Compresión (MB/s ± IQR) | Descompresión (MB/s ± IQR) | Lat 4K (C / D $\mu$s) | Pico RSS | Integridad |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **LZ4 v1.10 (RawBlock)** | 35.32% | 825.0 ± 17.2 | 5670.7 ± 62.9 | 6.1 / 1.0 | 104.176 KB | **PASS** |
| **BinPack (Fast Zero-CRC)** | 25.44% | 511.1 ± 3.4 | 2356.4 ± 39.9 | 14.9 / 5.6 | 128.000 KB | **PASS** |
| **BinPack (Default L19)** | 19.00% | 3.1 ± 0.1 | 1250.8 ± 37.3 | — | 1.041.600 KB | **PASS** |
| **Zstd v1.5.7 (L1 Frame)** | 25.40% | 702.3 ± 4.4 | 2360.3 ± 31.1 | 11.8 / 4.7 | 103.104 KB | **PASS** |
| **Zstd v1.5.7 (L19 Frame)** | 18.04% | 4.2 ± 0.2 | 2132.3 ± 79.0 | — | 183.744 KB | **PASS** |
| **BinPack (Strict CRC32)** | 25.44% | **— (N/A)** | 1293.5 ± 40.5 | — | 125.920 KB | **PASS** |

---

## 4. Trazabilidad y Archivos del Repositorio

- **`tests/benchmarks/benchmark_decisive_suite.c`:** Código fuente del harness con subsanaciones BP-008C-R4 (manejo seguro de contextos Zstandard, comprobación de retornos de `ZSTD_CCtx_reset`, orden estricto de afinidad, y delimitación transparente de la frontera de virtualización).
- **`docs/audit/decisive_benchmark_raw_r4.csv`:** Registro auditado de 29 líneas con los 40 vectores crudos de muestras y metadatos completos.
- **`docs/audit/decisive_benchmark_raw_r3.csv`:** Preservado intacto como registro histórico previo.
- **`docs/audit/decisive_benchmark_raw_r2.csv`:** Preservado intacto como registro histórico previo.
- **`docs/audit/decisive_benchmark_raw_r1.csv`:** Preservado intacto como registro histórico previo.
- **`docs/audit/decisive_benchmark_raw_pilot.csv`:** Preservado intacto como registro histórico previo.
- **`src/`, `include/`, `deps/`:** Estrictamente congelados y sin modificación alguna.
