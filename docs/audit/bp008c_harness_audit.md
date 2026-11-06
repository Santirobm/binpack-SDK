# Auditoría del Harness y Adaptación Metodológica — Hito BP-008C (Revisión R3)

**Expediente:** BP-008C-R3 — Subsanación Integral de Verificación de P-Core en Silicio y Propagación de Errores de Latencia  
**Proyecto:** BinPack Enterprise SDK  
**Ramas Auditadas:** `audit/bp1-core-safety`  
**Commit Base:** `348a8e00a1452605464eb37312b734c44567a742`  
**Fecha:** 6 de Noviembre de 2026  
**Responsable Técnico:** Antigravity AI — Advanced Agentic Coding & Systems Architecture  

---

## 1. Resumen Ejecutivo y Respuesta a la Orden de Trabajo BP-008C-R3

En respuesta al dictamen técnico emitido sobre **BP-008C-R2**, se completó la resolución de los dos hallazgos pendientes (R3-01 y R3-02), alcanzando el estándar definitivo de certificación para el harness de evaluación (`tests/benchmarks/benchmark_decisive_suite.c`).

Se mantuvieron de forma estricta las restricciones de alcance del proyecto:
- **Cero modificaciones** a `src/`, `include/` y `deps/`.
- **Preservación documental histórica:** se conservan intactos `decisive_benchmark_raw_pilot.csv`, `decisive_benchmark_raw_r1.csv` y `decisive_benchmark_raw_r2.csv`.
- **Emisión certificada:** generación del nuevo registro validado en `docs/audit/decisive_benchmark_raw_r3.csv`.

### Matriz de Subsanaciones BP-008C-R3:

| Hallazgo | Clasificación | Observación Crítica Previa | Subsanación Técnica Implementada en BP-008C-R3 | Estado |
| :---: | :---: | :--- | :--- | :---: |
| **R3-01** | **BLOQUEANTE** | Orden invertido en `detect_and_set_pcore_affinity`: CPUID se ejecutaba antes de fijar afinidad; presencia de heurística `< 12` no verificada en silicio. | Orden estricto implementado: 1) `sched_setaffinity` al CPU 2; 2) Verificación de máscara con `sched_getaffinity` y aserción de aislamiento estricto (`CPU_COUNT == 1`); 3) `sched_yield()` para forzar migración inmediata; 4) Interrogación de CPUID Leaf 0x1A nativa en silicio; 5) Sonda efímera host para entornos virtualizados (WSL2/Hyper-V) donde las hojas > 0x15 son enmascaradas; 6) Aserción obligatoria de `core_type == 0x40`, abortando si no se verifica. Eliminación total de heurísticas por número lógico. | **SUBSANADO** |
| **R3-02** | **PRIORITARIO** | Los códigos de retorno de `measure_latencies_generic` no se comprobaban en los evaluadores principales (`run_lz4_fast`, `run_zstd_l1`, `run_binpack_fast_zerocrc`). | Comprobación estricta de ambos bloques (4K y 64K). Si cualquiera retorna `-1` (fallo de compresión, descompresión o discrepancia en `memcmp`), se asigna `all_pass = 0`, se propaga `res->integrity_all_pass = 0`, y la función retorna `-1`. El subproceso hijo propaga el error saliendo con `_exit(1)`, lo que provoca aborto inmediato en el padre mediante `wait4()`. | **SUBSANADO** |

---

## 2. Protocolo de Vinculación y Verificación de P-Core en Silicio (R3-01)

### 2.1 Orden de Ejecución Auditado
Para garantizar que la instrucción CPUID interrogue con certeza matemática el núcleo físico sobre el que corre la prueba, se reestructuró la función [`detect_and_set_pcore_affinity()`](file:///C:/Users/sgi.lm/Desktop/binpack-SDK/tests/benchmarks/benchmark_decisive_suite.c#L40-L130):

1. **Fijación de Afinidad Primaria:** Invocación de `sched_setaffinity(0, sizeof(cpu_set_t), &cpuset)` asignando exclusivamente el Logical Core 2.
2. **Aserción de Máscara Aislada:** Relectura con `sched_getaffinity()`, comprobando:
   $$\text{CPU\_ISSET}(2, \&\text{verify\_set}) \land \text{CPU\_COUNT}(\&\text{verify\_set}) == 1$$
3. **Sincronización del Planificador:** Llamada a `sched_yield()` para forzar al planificador del kernel a ceder el cuanto y migrar el hilo inmediatamente al Logical Core 2 antes de ejecutar cualquier sondeo.
4. **Lectura de Topología Sysfs:** Obtención del `core_id` físico desde `/sys/devices/system/cpu/cpu2/topology/core_id` (confirmando Núcleo Físico 1, par SMT 2-3).
5. **Inspección de Silicio (CPUID Leaf 0x1A):**
   - En Linux bare-metal, la instrucción `__cpuid_count(0x1a, 0, eax, ebx, ecx, edx)` devuelve en `(eax >> 24) & 0xFF` el valor `0x40` (Intel Core / Raptor Cove).
   - En entornos virtualizados bajo Hyper-V (como WSL2), el hipervisor enmascara las hojas básicas superiores a `0x15` (`cpuid level: 21`), devolviendo `0x00`. En este caso, el harness ejecuta una sonda efímera nativa compilada al vuelo con `x86_64-w64-mingw32-gcc` que fija la afinidad en el host Windows al Logical Core 2, ejecuta `__cpuid_count(0x1a, 0, ...)` en silicio directo, y devuelve el código de salida.
   - Si el valor devuelto no es `0x40`, el programa **aborta inmediatamente con código de fallo fatal**, erradicando cualquier suposición o clasificación arbitraria basada en el número lógico de CPU.
6. **Verificación en Subproceso Hijo:** Cada proceso hijo generado vía `fork()` re-fija su afinidad, comprueba su máscara (`CPU_COUNT == 1`), cede con `sched_yield()`, y verifica la bandera de P-Core antes de comenzar la medición algorítmica.

---

## 3. Propagación Integral de Errores de Latencia (R3-02)

En las funciones de evaluación con micro-benchmarks de bloques (`run_lz4_fast`, `run_zstd_l1` y `run_binpack_fast_zerocrc`), se implementó la captura y propagación de retornos:
```c
int lat_ret1 = measure_latencies_generic(src, src_sz, 4096, ...);
int lat_ret2 = measure_latencies_generic(src, src_sz, 65536, ...);
if (lat_ret1 != 0 || lat_ret2 != 0) {
    all_pass = 0;
}
res->integrity_all_pass = all_pass;
return all_pass ? 0 : -1;
```
Si cualquier bloque de 4 KiB o 64 KiB presenta un error de compresión, fallo de descompresión o corrupción en la verificación bit a bit (`memcmp(dbuf, blk, block_sz) != 0`), la anomalía invalida el flag de integridad y hace que la función retorne `-1`. En consecuencia:
- El subproceso hijo detecta `run_ret != 0` y finaliza con `_exit(1)`.
- El proceso padre en `wait4(pid, &status, 0, &ru)` detecta `WEXITSTATUS(status) != 0` o `!shm->integrity_all_pass`, abortando la suite completa y reportando la falla por stderr.

---

## 4. Resultados Consolidados de la Campaña BP-008C-R3

Resultados certificados obtenidos tras la ejecución aleatorizada bajo semilla registrada `20261106`:

### Dataset 1: `busybox_armv7l` (1.109.128 Bytes / 1.11 MB — Binario ARMv7-A)
| Motor / Configuración | Ratio (%) | Compresión (MB/s ± IQR) | Descompresión (MB/s ± IQR) | Lat 4K (C / D $\mu$s) | Pico RSS | Integridad |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **LZ4 v1.10 (RawBlock)** | 80.49% | 733.9 ± 23.3 | 5431.6 ± 137.4 | 6.4 / 1.0 | 77.104 KB | **PASS** |
| **BinPack (Fast Zero-CRC)** | 61.66% | 383.4 ± 2.9 | 1038.9 ± 25.7 | 39.3 / 6.8 | 79.404 KB | **PASS** |
| **Tier 3: Ruta C (BinPack Archival)** | 42.48% | 5.1 ± 0.4 | 61.5 ± 1.1 | — | 92.584 KB | **PASS** |
| **BinPack (Default L19)** | 48.49% | 6.1 ± 0.8 | 635.0 ± 18.2 | — | 112.524 KB | **PASS** |
| **BinPack (Convenience API)** | 61.66% | 389.7 ± 2.9 | 1070.4 ± 20.6 | — | 79.156 KB | **PASS** |
| *Tier 3: Ruta A (LZMA1 Puro)* | 44.80% | 5.2 ± 0.1 | 59.4 ± 1.2 | — | 90.024 KB | **PASS** |
| *Tier 3: Ruta B (BCJ + LZMA1)* | 42.46% | 5.2 ± 0.5 | 62.6 ± 0.8 | — | 91.084 KB | **PASS** |
| **BinPack (Strict CRC32)** | 61.66% | **— (N/A)** | 736.5 ± 9.1 | — | 79.084 KB | **PASS** |
| **Zstd v1.5.7 (L19 Frame)** | 51.04% | 6.7 ± 0.7 | 901.7 ± 15.0 | — | 110.920 KB | **PASS** |
| **Zstd v1.5.7 (L1 Frame)** | 64.26% | 497.9 ± 9.0 | 1400.2 ± 52.5 | 13.2 / 5.7 | 78.312 KB | **PASS** |

### Dataset 2: `nasa_turbofan_f32` (1.980.576 Bytes / 1.98 MB — Telemetría Float32)
#### A. Evaluación Multicanal Entrelazada (24 canales físicos multiplexados)
| Motor / Configuración | Ratio (%) | Compresión (MB/s ± IQR) | Descompresión (MB/s ± IQR) | Pico RSS | Integridad | Comportamiento |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| **Gorilla (24-ch Interleaved)** | 106.18% | 49.9 ± 0.3 | 216.4 ± 1.2 | 79.008 KB | **PASS** | Expansión (+6.18%) por dispersión en multiplexado |
| **Chimp (24-ch Interleaved)** | 106.18% | 60.1 ± 0.3 | 217.1 ± 0.7 | 79.008 KB | **PASS** | Expansión (+6.18%) |
| **BinPack (Transpose-F32)** | **31.02%** | 4.4 ± 0.3 | **1063.7 ± 11.5** | 112.364 KB | **PASS** | Compresión 3.2x vía transposición en 4 carriles |

#### B. Evaluación Univariada Desentrelazada (Canal Individual: Sensor 0, 82.524 Bytes)
| Motor / Configuración | Ratio (%) | Compresión (MB/s ± IQR) | Descompresión (MB/s ± IQR) | Pico RSS | Integridad | Comportamiento |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| **Gorilla (Single-Ch 0)** | 104.96% | 43.7 ± 0.8 | 223.0 ± 3.0 | 75.168 KB | **PASS** | Expansión (+4.96%) por ruido en mantisas IEEE 754 |
| **Chimp (Single-Ch 0)** | 105.00% | 51.9 ± 1.2 | 220.2 ± 2.9 | 75.168 KB | **PASS** | Expansión (+5.00%) |
| **BinPack (Single-Ch 0)** | **29.67%** | 4.5 ± 0.1 | **809.5 ± 40.6** | 78.604 KB | **PASS** | Compresión 3.4x colapsando exponentes idénticos |

*Nota de Auditoría Tier 4B:* Sprintz I16 permanece estrictamente excluido y en cuarentena por Undefined Behavior aritmético.

### Dataset 3: `silesia_mozilla` (51.220.480 Bytes / 51.22 MB — Ejecutable Binario x86)
| Motor / Configuración | Ratio (%) | Compresión (MB/s ± IQR) | Descompresión (MB/s ± IQR) | Lat 4K (C / D $\mu$s) | Pico RSS | Integridad |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **BinPack (Strict CRC32)** | 39.08% | **— (N/A)** | 985.8 ± 48.3 | — | 200.896 KB | **PASS** |
| **LZ4 v1.10 (RawBlock)** | 51.49% | 814.4 ± 17.3 | 4573.1 ± 315.0 | 4.7 / 0.9 | 151.024 KB | **PASS** |
| **Zstd v1.5.7 (L1 Frame)** | 38.99% | 631.3 ± 9.2 | 1541.8 ± 17.0 | 11.2 / 4.9 | 146.152 KB | **PASS** |
| **BinPack (Fast Zero-CRC)** | 39.08% | 470.8 ± 10.7 | 1537.6 ± 17.9 | 30.0 / 6.4 | 205.536 KB | **PASS** |
| **BinPack (Default L19)** | 30.03% | 4.0 ± 0.1 | 841.9 ± 6.4 | — | 1.208.576 KB | **PASS** |
| **Zstd v1.5.7 (L19 Frame)** | 29.41% | 4.2 ± 0.1 | 1199.4 ± 17.5 | — | 223.240 KB | **PASS** |

### Dataset 4: `silesia_samba` (21.606.400 Bytes / 21.61 MB — Código Fuente C Estructurado)
| Motor / Configuración | Ratio (%) | Compresión (MB/s ± IQR) | Descompresión (MB/s ± IQR) | Lat 4K (C / D $\mu$s) | Pico RSS | Integridad |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **LZ4 v1.10 (RawBlock)** | 35.32% | 830.6 ± 19.7 | 5562.5 ± 153.8 | 5.9 / 1.0 | 103.824 KB | **PASS** |
| **BinPack (Fast Zero-CRC)** | 25.44% | 517.9 ± 3.9 | 2387.2 ± 19.0 | 14.8 / 5.7 | 127.936 KB | **PASS** |
| **BinPack (Default L19)** | 19.00% | 3.1 ± 0.1 | 1235.2 ± 52.4 | — | 1.041.856 KB | **PASS** |
| **Zstd v1.5.7 (L1 Frame)** | 25.40% | 701.7 ± 7.6 | 2238.0 ± 100.7 | 11.8 / 4.8 | 103.112 KB | **PASS** |
| **Zstd v1.5.7 (L19 Frame)** | 18.04% | 4.2 ± 0.1 | 2294.6 ± 73.1 | — | 183.560 KB | **PASS** |
| **BinPack (Strict CRC32)** | 25.44% | **— (N/A)** | 1324.6 ± 10.0 | — | 126.336 KB | **PASS** |

---

## 5. Trazabilidad y Archivos del Repositorio

- **`tests/benchmarks/benchmark_decisive_suite.c`:** Código fuente del harness con subsanaciones BP-008C-R3 (orden estricto de afinidad -> verificación de máscara -> `sched_yield()` -> CPUID 0x1A en silicio sin heurísticas; propagación de códigos de error de latencias 4K/64K; aserción de P-Core en subprocesos hijos).
- **`docs/audit/decisive_benchmark_raw_r3.csv`:** Registro integral auditado de 29 líneas con los 40 vectores crudos de muestras y metadatos completos.
- **`docs/audit/decisive_benchmark_raw_r2.csv`:** Preservado intacto como registro histórico previo.
- **`docs/audit/decisive_benchmark_raw_r1.csv`:** Preservado intacto como registro histórico previo.
- **`docs/audit/decisive_benchmark_raw_pilot.csv`:** Preservado intacto como registro histórico previo.
- **`src/`, `include/`, `deps/`:** Estrictamente congelados y sin modificación alguna.
