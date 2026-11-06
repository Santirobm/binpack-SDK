# Auditoría del Harness y Adaptación Metodológica — Hito BP-008C (Revisión R2)

**Expediente:** BP-008C-R2 — Subsanación Rigurosa de Hallazgos Metodológicos, Semántica Strict CRC32 y Control de Errores  
**Proyecto:** BinPack Enterprise SDK  
**Ramas Auditadas:** `audit/bp1-core-safety`  
**Commit Base:** `f4db92f55611bdf8b81150d4610d456caa412bd1`  
**Fecha:** 6 de Noviembre de 2026  
**Responsable Técnico:** Antigravity AI — Advanced Agentic Coding & Systems Architecture  

---

## 1. Resumen Ejecutivo y Respuesta a la Orden de Trabajo BP-008C-R2

En respuesta al dictamen técnico sobre la revisión R1 de **BP-008C**, se efectuó una reingeniería exhaustiva en el harness de evaluación (`tests/benchmarks/benchmark_decisive_suite.c`), subsanando la totalidad de los hallazgos categorizados como bloqueantes (P0), prioritarios (P1) y de control de afinidad (P2).

Se mantuvieron de forma estricta las restricciones de alcance: **cero modificaciones a `src/`, `include/` ni `deps/`**, preservación histórica de `decisive_benchmark_raw_pilot.csv` y `decisive_benchmark_raw_r1.csv`, y generación del nuevo registro validado en `docs/audit/decisive_benchmark_raw_r2.csv`.

### Matriz de Subsanaciones BP-008C-R2:

| Prioridad | Área Observada | Hallazgo Crítico Previo | Subsanación Implementada en BP-008C-R2 | Estado |
| :---: | :--- | :--- | :--- | :---: |
| **P0-1** | **Semántica Strict CRC32** | Compresión única externa generaba muestra centinela artificial de 0.001s, publicando 51.220 MB/s de compresión ficticia. | Eliminación de toda medición ficticia. Flag explícito `has_comp_measurement = 0`. El CSV exporta `N/A,N/A,N/A,0,` y la salida por consola muestra `—`. Descompresión con CRC32 medida de forma rigurosa. | **SUBSANADO** |
| **P0-2** | **Propagación de Errores Hijo** | El proceso padre ignoraba el retorno de `cfg->run_fn()` y el estado de terminación del hijo en `wait4()`. | Verificación exhaustiva: `wait4 < 0`, `!WIFEXITED(status)`, `WEXITSTATUS(status) != 0` y `!shm->integrity_all_pass`. Aborto inmediato si el hijo falla. | **SUBSANADO** |
| **P0-3** | **Overflow Timer Windows QPC** | Multiplicación `QuadPart * 10^9` desborda `uint64_t` tras ~30.7 minutos de uptime del sistema. | Descomposición analítica entera por cociente y resto: `whole_sec * 1e9 + (remainder * 1e9) / freq`. Inicialización protegida atómicamente con `InterlockedCompareExchange`. | **SUBSANADO** |
| **P1-1** | **Delimitación de Memoria RSS** | Nomenclatura ambigua sobre si `ru_maxrss` representaba el "consumo exclusivo del motor". | Redefinición formal: "Pico RSS del proceso de ensayo aislado", explicitando que Linux `ru_maxrss` engloba páginas heredadas vía CoW de `fork()` más las asignaciones dinámicas del motor. Documentada la frontera frente a Windows `GetProcessMemoryInfo`. | **SUBSANADO** |
| **P1-2** | **Micro-benchmarks de Latencia** | En `measure_latencies_generic`, no se validaba el código de retorno ni la integridad del bloque individual. | Validación obligatoria en cada iteración de bloque: retorno `!ZSTD_isError`, `LZ4_decompress_safe == block_sz`, `BINPACK_OK` y verificación bit a bit `memcmp(dbuf, blk, block_sz) == 0`. Aborto ante discrepancia. | **SUBSANADO** |
| **P1-3** | **Cálculo Matemático de IQR** | Aproximación proporcional $IQR_{MB/s} \approx \frac{\text{size}}{t_{med}^2} \times IQR_{sec}$. | Vector de rendimiento directo: $X_{\text{tp}, i} = \frac{\text{orig\_sz}}{10^6 \times t_i}$ para cada muestra válida. Se ordenan los $X_{\text{tp}}$ y se computan mediana e IQR directamente del vector de throughput. | **SUBSANADO** |
| **P2-1** | **Afinidad en Proceso Hijo** | El hijo heredaba la máscara pero no re-verificaba su propia afinidad activa tras el `fork()`. | Invocación explícita de `sched_setaffinity` y `sched_getaffinity` dentro del proceso hijo con salida de código de error si la asignación no coincide con el Core P auditado. | **SUBSANADO** |

---

## 2. Demostración Matemática: Protección contra Desbordamiento de QPC en Windows

En sistemas Windows x64, `QueryPerformanceFrequency` devuelve típicamente frecuencias entre 10 MHz ($10^7$ ticks/s) y 24 MHz ($2.4 \times 10^7$ ticks/s). El valor máximo representable por un entero sin signo de 64 bits es:
$$2^{64} - 1 \approx 18.446.744.073.709.551.615 \approx 1.844 \times 10^{19}$$

Bajo la implementación naive previa:
```c
return (counter.QuadPart * 1000000000ULL) / freq.QuadPart;
```
Para una frecuencia $F = 10^7$ Hz, el producto `counter.QuadPart * 10^9` desborda $2^{64}-1$ tan pronto como `counter.QuadPart` supera:
$$\text{Ticks}_{\text{max}} = \frac{2^{64}-1}{10^9} \approx 18.446.744.073$$
Dividiendo entre la frecuencia de $10^7$ ticks/s, el tiempo de desbordamiento ocurre a:
$$T_{\text{overflow}} = \frac{18.446.744.073}{10.000.000} \approx 1.844 \text{ segundos} \approx 30.7 \text{ minutos}$$

Cualquier estación de trabajo con más de media hora de encendido continuo sufría un desbordamiento aritmético de 64 bits, corrompiendo silenciosamente todas las marcas temporales.

### Implementación Robusta por Descomposición de Cociente y Resto (BP-008C-R2):
Dado que cualquier número de ticks $C$ se puede descomponer mediante la división euclídea respecto a la frecuencia $F$:
$$C = q \cdot F + r, \quad \text{donde } q = \lfloor C / F \rfloor \text{ (segundos enteros)}, \quad r = C \pmod F < F$$
El tiempo en nanosegundos es exactamente:
$$T_{\text{ns}} = q \times 10^9 + \frac{r \times 10^9}{F}$$

Dado que $r < F \le 2.4 \times 10^7$, el término $r \times 10^9 < 2.4 \times 10^{16} \ll 1.844 \times 10^{19}$. Por tanto, **el producto $r \times 10^9$ jamás desborda 64 bits**, y el cociente $q \times 10^9$ permanece seguro por más de 584 años de tiempo de ejecución continuo.

Código auditado implementado:
```c
uint64_t whole_sec = (uint64_t)counter.QuadPart / (uint64_t)freq.QuadPart;
uint64_t remainder = (uint64_t)counter.QuadPart % (uint64_t)freq.QuadPart;
return whole_sec * 1000000000ULL + (remainder * 1000000000ULL) / (uint64_t)freq.QuadPart;
```

---

## 3. Delimitación de Fronteras de Memoria y Protocolo de Aislamiento

### 3.1 Nomenclatura Formal: "Pico RSS del Proceso de Ensayo Aislado"
Se delimita formalmente el significado físico de la métrica de memoria:
- En **Linux**, cada motor corre en un proceso independiente derivado mediante `fork()`.
- El proceso hijo hereda por Copy-On-Write las páginas virtuales de código binario y los búferes que contienen el corpus precargado en memoria RAM.
- El kernel registra en `wait4(..., &ru)` el valor `ru.ru_maxrss`. Este valor refleja el **Pico RSS total del proceso de ensayo aislado**: incluye la base fija de páginas mapeadas del harness y dataset precargado (~75-78 MB en BusyBox y NASA, ~100 MB en Samba, ~140 MB en Mozilla) más las asignaciones dinámicas, búferes de trabajo, diccionarios y tablas hash creadas por el motor durante su ejecución.
- En **Windows**, la función `GetProcessMemoryInfo` sobre el proceso unificado es acumulativa a lo largo de los motores si no se reinicia el proceso. La métrica en el informe queda formalmente catalogada como dependiente de plataforma, certificándose los valores de Linux como pico del proceso aislado.

---

## 4. Resultados Consolidados de la Campaña BP-008C-R2

A continuación se presentan los resultados certificados de la ejecución del harness refactorizado bajo orden aleatorizado con semilla `20261106`:

### Dataset 1: `busybox_armv7l` (1.109.128 Bytes / 1.11 MB — Binario ARMv7-A)
| Motor / Configuración | Ratio (%) | Compresión (MB/s ± IQR) | Descompresión (MB/s ± IQR) | Lat 4K (C / D $\mu$s) | Pico RSS | Integridad |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **LZ4 v1.10 (RawBlock)** | 80.49% | 739.3 ± 24.6 | 5469.1 ± 156.3 | 6.4 / 1.0 | 77.420 KB | **PASS** |
| **BinPack (Fast Zero-CRC)** | 61.66% | 384.1 ± 2.5 | 1086.2 ± 13.9 | 38.9 / 6.6 | 79.488 KB | **PASS** |
| **BinPack (Convenience API)** | 61.66% | 389.3 ± 7.1 | 1064.3 ± 18.3 | — | 79.184 KB | **PASS** |
| **BinPack (Strict CRC32)** | 61.66% | **— (N/A)** | 776.0 ± 12.8 | — | 79.168 KB | **PASS** |
| **Zstd v1.5.7 (L1 Frame)** | 64.26% | 511.1 ± 17.5 | 1432.6 ± 17.6 | 13.0 / 5.6 | 78.288 KB | **PASS** |
| **Zstd v1.5.7 (L19 Frame)** | 51.04% | 6.5 ± 1.4 | 911.7 ± 21.2 | — | 111.092 KB | **PASS** |
| **BinPack (Default L19)** | 48.49% | 7.3 ± 0.2 | 635.2 ± 7.0 | — | 112.608 KB | **PASS** |
| *Tier 3: Ruta A (LZMA1 Puro)* | 44.80% | 5.9 ± 0.5 | 60.7 ± 1.5 | — | 90.156 KB | **PASS** |
| *Tier 3: Ruta B (BCJ + LZMA1)* | 42.46% | 5.2 ± 1.0 | 62.5 ± 14.2 | — | 91.212 KB | **PASS** |
| *Tier 3: Ruta C (BinPack Archival)* | 42.48% | 5.8 ± 0.1 | 62.5 ± 0.6 | — | 92.604 KB | **PASS** |

### Dataset 2: `nasa_turbofan_f32` (1.980.576 Bytes / 1.98 MB — Telemetría Float32)
#### A. Evaluación Multicanal Entrelazada (24 canales físicos multiplexados)
| Motor / Configuración | Ratio (%) | Compresión (MB/s ± IQR) | Descompresión (MB/s ± IQR) | Pico RSS | Integridad | Comportamiento |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| **Gorilla (24-ch Interleaved)** | 106.18% | 50.9 ± 0.3 | 221.7 ± 0.4 | 79.168 KB | **PASS** | Expansión (+6.18%) por ruptura de autocorrelación 1D |
| **Chimp (24-ch Interleaved)** | 106.18% | 60.2 ± 0.3 | 216.7 ± 1.1 | 79.168 KB | **PASS** | Expansión (+6.18%) por dispersión de ceros |
| **BinPack (Transpose-F32)** | **31.02%** | 5.2 ± 0.2 | **1057.5 ± 27.8** | 112.448 KB | **PASS** | Compresión 3.2x vía transposición en 4 carriles |

#### B. Evaluación Univariada Desentrelazada (Canal Individual: Sensor 0, 82.524 Bytes)
| Motor / Configuración | Ratio (%) | Compresión (MB/s ± IQR) | Descompresión (MB/s ± IQR) | Pico RSS | Integridad | Comportamiento |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| **Gorilla (Single-Ch 0)** | 104.96% | 43.9 ± 0.5 | 223.0 ± 2.8 | 75.328 KB | **PASS** | Ligera expansión (+4.96%) por ruido en mantisas |
| **Chimp (Single-Ch 0)** | 105.00% | 52.6 ± 0.9 | 220.7 ± 3.0 | 75.328 KB | **PASS** | Ligera expansión (+5.00%) |
| **BinPack (Single-Ch 0)** | **29.67%** | 4.6 ± 0.0 | **845.5 ± 30.4** | 78.688 KB | **PASS** | Compresión 3.4x colapsando exponentes idénticos |

*Nota de Auditoría Tier 4B:* Sprintz I16 permanece estrictamente excluido y en cuarentena por Undefined Behavior aritmético.

### Dataset 3: `silesia_mozilla` (51.220.480 Bytes / 51.22 MB — Ejecutable Binario x86)
| Motor / Configuración | Ratio (%) | Compresión (MB/s ± IQR) | Descompresión (MB/s ± IQR) | Lat 4K (C / D $\mu$s) | Pico RSS | Integridad |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **LZ4 v1.10 (RawBlock)** | 51.49% | 830.6 ± 10.9 | 4743.1 ± 50.4 | 4.6 / 0.9 | 151.340 KB | **PASS** |
| **Zstd v1.5.7 (L1 Frame)** | 38.99% | 626.5 ± 31.2 | 1507.2 ± 8.0 | 11.0 / 4.8 | 146.128 KB | **PASS** |
| **BinPack (Fast Zero-CRC)** | 39.08% | 457.9 ± 7.1 | 1537.5 ± 16.7 | 31.5 / 6.1 | 203.692 KB | **PASS** |
| **BinPack (Strict CRC32)** | 39.08% | **— (N/A)** | 1025.8 ± 5.8 | — | 200.972 KB | **PASS** |
| **Zstd v1.5.7 (L19 Frame)** | 29.41% | 4.1 ± 0.1 | 1181.4 ± 54.0 | — | 223.412 KB | **PASS** |
| **BinPack (Default L19)** | 30.03% | 3.8 ± 0.2 | 813.9 ± 13.8 | — | 1.208.976 KB | **PASS** |

### Dataset 4: `silesia_samba` (21.606.400 Bytes / 21.61 MB — Código Fuente C Estructurado)
| Motor / Configuración | Ratio (%) | Compresión (MB/s ± IQR) | Descompresión (MB/s ± IQR) | Lat 4K (C / D $\mu$s) | Pico RSS | Integridad |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **LZ4 v1.10 (RawBlock)** | 35.32% | 812.1 ± 7.1 | 5346.5 ± 264.1 | 6.0 / 1.0 | 104.140 KB | **PASS** |
| **BinPack (Fast Zero-CRC)** | 25.44% | 511.5 ± 14.5 | 2332.8 ± 51.1 | 14.7 / 5.7 | 128.012 KB | **PASS** |
| **Zstd v1.5.7 (L1 Frame)** | 25.40% | 703.0 ± 22.3 | 2370.3 ± 66.8 | 11.8 / 4.9 | 103.088 KB | **PASS** |
| **BinPack (Strict CRC32)** | 25.44% | **— (N/A)** | 1305.5 ± 27.7 | — | 126.252 KB | **PASS** |
| **Zstd v1.5.7 (L19 Frame)** | 18.04% | 4.3 ± 0.1 | 2178.3 ± 112.8 | — | 183.732 KB | **PASS** |
| **BinPack (Default L19)** | 19.00% | 3.1 ± 0.1 | 1247.3 ± 26.3 | — | 1.042.096 KB | **PASS** |

---

## 5. Trazabilidad y Archivos del Repositorio

- **`tests/benchmarks/benchmark_decisive_suite.c`:** Código fuente del harness con subsanaciones BP-008C-R2 (semántica Strict CRC32 `N/A`, propagación exhaustiva de estados de terminación del hijo, temporizador QPC inmune a desbordamiento, micro-benchmarks con verificación `memcmp` bit a bit por bloque, IQR directo sobre arrays de throughput).
- **`docs/audit/decisive_benchmark_raw_r2.csv`:** Registro auditado de 29 líneas que documenta las 40 muestras crudas por fila, marcando `N/A` explícito en compresión de Strict CRC32.
- **`docs/audit/decisive_benchmark_raw_r1.csv`:** Preservado intacto como registro histórico previo del hito BP-008C-R1.
- **`docs/audit/decisive_benchmark_raw_pilot.csv`:** Preservado intacto como registro histórico previo del hito BP-008C inicial.
- **`src/`, `include/`, `deps/`:** Estrictamente congelados y sin modificación alguna.
