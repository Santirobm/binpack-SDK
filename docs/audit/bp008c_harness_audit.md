# Auditoría del Harness y Adaptación Metodológica — Hito BP-008C (Revisión R5)

**Expediente:** BP-008C-R5 — Cierre Semántico y Documental Definitivo del Harness de Benchmark  
**Proyecto:** BinPack Enterprise SDK  
**Ramas Auditadas:** `audit/bp1-core-safety`  
**Commit Base:** `18ea0912fc5a10ac9a3f51efe22fa5ff4d92205b`  
**Fecha:** 6 de Noviembre de 2026  
**Responsable Técnico:** Antigravity AI — Advanced Agentic Coding & Systems Architecture  

---

## 1. Resumen Ejecutivo y Respuesta a la Orden de Trabajo BP-008C-R5

En respuesta al dictamen técnico emitido sobre **BP-008C-R4**, se procedió al ajuste semántico y documental estricto de los tres puntos de rigor topológico solicitados por la auditoría:

1. **Disociación Semántica de Banderas de Topología:**
   Se sustituyó la bandera unificada `is_pcore` en la estructura `cpu_affinity_info_t` por dos indicadores ortogonales:
   - `physical_core_verified`: Se establece en `1` única y exclusivamente cuando se verifica en silicio nativo (bare-metal) mediante `CPUID Leaf 0x1A` que el tipo de núcleo es `0x40` (Intel Raptor Cove P-Core). En entornos virtualizados donde el hipervisor oculta o abstrae dicha hoja, permanece estrictamente en `0`.
   - `topology_gate_passed`: Se establece en `1` cuando la afinidad de CPU ha sido fijada con éxito a un CPU lógico determinado (vCPU 2), la máscara devuelta por `sched_getaffinity` confirma aislamiento estricto (`CPU_COUNT == 1`), y se ha emitido `sched_yield()`. El proceso hijo en aislamiento (`run_engine_isolated`) comprueba `topology_gate_passed` para autorizar la ejecución del ensayo sin atribuir falsamente certificación de silicio físico.

2. **Eliminación de Presunciones de `core_id`:**
   El campo `core_id` se inicializa estrictamente en `-1` (desconocido). Únicamente se actualiza si el subsistema `sysfs` (`/sys/devices/system/cpu/cpu%d/topology/core_id`) existe y devuelve un entero válido. Si la lectura falla o no está disponible, se conserva `-1` y se reporta explícitamente como `Unknown`, eliminando cualquier asignación arbitraria por defecto.

3. **Identificación Rigurosa del Hipervisor:**
   Tras detectar la presencia de un entorno virtualizado vía `CPUID.1:ECX[31]`, el harness interroga la hoja estándar `0x40000000` para leer la firma ASCII de 12 bytes del proveedor (registros `EBX`, `ECX`, `EDX`):
   - `"Microsoft Hv"` $\rightarrow$ `Microsoft Hyper-V Virtual Guest (WSL2/Hyper-V)`
   - `"KVMKVMKVM"` $\rightarrow$ `KVM Virtual Guest`
   - `"VMwareVMware"` $\rightarrow$ `VMware Virtual Guest`
   - Firma desconocida $\rightarrow$ `Generic Virtual Guest / Hypervisor`
   Se erradica cualquier atribución genérica o no comprobada del entorno de virtualización.

Conforme a las directrices de la orden de trabajo BP-008C-R5:
- **Cero modificaciones** a `src/`, `include/` ni `deps/`.
- **Cero repetición de la campaña:** no se re-ejecutó el benchmark ni se alteró ningún archivo de resultados históricos.
- **Preservación histórica íntegra:** se conservan intactos `decisive_benchmark_raw_pilot.csv`, `decisive_benchmark_raw_r1.csv`, `decisive_benchmark_raw_r2.csv`, `decisive_benchmark_raw_r3.csv` y `decisive_benchmark_raw_r4.csv`.

---

## 2. Detalle de las Subsanaciones Técnicas Implementadas (R4 y R5)

### 2.1 Modelo Semántico de Topología y Frontera de Virtualización (R5)
En [`tests/benchmarks/benchmark_decisive_suite.c`](file:///C:/Users/sgi.lm/Desktop/binpack-SDK/tests/benchmarks/benchmark_decisive_suite.c):
- Estructura `cpu_affinity_info_t`:
  ```c
  typedef struct {
      int logical_id;
      int core_id;                   /* -1 si es desconocido o no disponible */
      int physical_core_verified;    /* 1 solo si CPUID nativo 0x1A devuelve 0x40 */
      int topology_gate_passed;      /* 1 si la afinidad y barrera fueron verificadas */
      char description[128];
  } cpu_affinity_info_t;
  ```
- Inicialización en `detect_and_set_pcore_affinity()`:
  `info->logical_id = 2; info->core_id = -1; info->physical_core_verified = 0; info->topology_gate_passed = 0;`
- Interrogación de firma de hipervisor en `0x40000000`:
  ```c
  char hv_sig[13] = {0};
  __cpuid(0x40000000, h_eax, h_ebx, h_ecx, h_edx);
  memcpy(hv_sig + 0, &h_ebx, 4);
  memcpy(hv_sig + 4, &h_ecx, 4);
  memcpy(hv_sig + 8, &h_edx, 4);
  ```
- Verificación en subproceso hijo (`run_engine_isolated`):
  ```c
  if (!aff->topology_gate_passed) {
      fprintf(stderr, "[CHILD ERROR] Child running without verified topology gate\n");
      _exit(4);
  }
  ```

### 2.2 Robustez en la Gestión de Contextos Zstandard (R4)
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

- **`tests/benchmarks/benchmark_decisive_suite.c`:** Código fuente del harness con subsanaciones BP-008C-R5 (disociación semántica de `physical_core_verified` y `topology_gate_passed`, inicialización y control estricto de `core_id = -1`, identificación rigurosa de hipervisor vía CPUID Leaf `0x40000000`, manejo seguro de contextos Zstandard y retornos de `ZSTD_CCtx_reset`).
- **`docs/audit/decisive_benchmark_raw_r4.csv`:** Registro auditado de 29 líneas con los 40 vectores crudos de muestras y metadatos completos.
- **`docs/audit/decisive_benchmark_raw_r3.csv`:** Preservado intacto como registro histórico previo.
- **`docs/audit/decisive_benchmark_raw_r2.csv`:** Preservado intacto como registro histórico previo.
- **`docs/audit/decisive_benchmark_raw_r1.csv`:** Preservado intacto como registro histórico previo.
- **`docs/audit/decisive_benchmark_raw_pilot.csv`:** Preservado intacto como registro histórico previo.
- **`src/`, `include/`, `deps/`:** Estrictamente congelados y sin modificación alguna.
