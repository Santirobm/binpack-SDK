# Informe Técnico Oficial: Campaña Decisiva de Benchmarking — BP-010 (Revisión de Auditoría)

**Proyecto:** BinPack Enterprise SDK  
**Hito:** BP-010 — Campaña Decisiva de Benchmarking  
**Instrumento:** `BP-010 Instrument Revision 1 (R14-Rev1)`  
**Rama:** `audit/bp1-core-safety`  
**Commit del Instrumento Habilitado:** [`140841f58312d6674238579dc39bd0aba8c3591c`](https://github.com/Santirobm/binpack-SDK/commit/140841f58312d6674238579dc39bd0aba8c3591c)  
**Git Blob SHA-1 del Arnés (`benchmark_decisive_suite.c`):** `45b53e288f57db5ebbe5567e3612ae8337ce6ad2`  
**Fecha de Ejecución:** `2026-11-11T12:35:40-03:00`  
**Duración Total:** `765,18 s` (12m 45s) | **Código de Salida:** `0`  
**Régimen de Muestreo:** R14 (1 Warm-up + 20 Iteraciones Cronometradas por Configuración)

---

## 1. Cadena de Custodia Criptográfica Dual (Archivos Físicos vs Blobs GitHub)

En atención a la observación de auditoría sobre la normalización de saltos de línea (CRLF en Windows vs LF en objetos Git) y el alcance de verificación de binarios, se formaliza la distinción explícita entre las identidades físicas locales y las identidades de almacenamiento en GitHub:

### 1.1 Artefactos Documentales en Repositorio (`docs/audit/`)

| Artefacto Oficial | Ruta en Repositorio | Archivo Físico Local (CRLF)<br>Tamaño / SHA-256 | Objeto Blob GitHub (LF)<br>Blob SHA-1 / Tamaño / SHA-256 |
|---|---|---|---|
| **CSV Crudo Oficial (R14)** | `docs/audit/decisive_benchmark_raw_r5.csv` | **15,334 B**<br>`8777bed56c721ea6da56752fcdab6471fa9abf064028239c2a089b89bc32ef47` | **SHA-1:** `4022a56c99d217b74ffbfe81c144a01ca4d46a73`<br>**Tamaño:** 15,305 B<br>**SHA-256:** `fbad16f331c52c67d06a4e1eab6c75056ddf470b30543f5a58ea742135bde953` |
| **Sidecar JSON de Metadatos** | `docs/audit/decisive_benchmark_raw_r5.csv.meta.json` | **7,312 B**<br>`c4f48673b738a4542f3daccd477747e24de974c164e84c5c86c5801268ece3cd` | **SHA-1:** `b76107fcd17fc016ef68ae8684d0053595e0183b`<br>**Tamaño:** 7,116 B<br>**SHA-256:** `827f0dc68d730751b1f3cf54c0b5dd26baa936cffc8c90bf2c186935400beec4` |
| **Registro de Salida STDOUT** | `docs/audit/decisive_campaign_stdout.log` | **10,506 B**<br>`0c18a1d9c719610761a7935f2516861a6441e87047bb6fbfbda14a864ae74b64` | **SHA-1:** `4d8938301fbb015833dc9511aa20df989d2f9ebc`<br>**Tamaño:** 10,407 B<br>**SHA-256:** `79420a1963e65aa38550242131fd999cc20741fa4281138a2c0d2d201e17dae2` |
| **Registro de Salida STDERR** | `docs/audit/decisive_campaign_stderr.log` | **534 B**<br>`4bf9aced2cbebab6c79aff11dec80d9362f6889a768667bb579e9cb4acb95ca1` | **SHA-1:** `3c4d58f81043ffc2689008cf27b6c7bd1dfb79fb`<br>**Tamaño:** 530 B<br>**SHA-256:** `63677dddd2c47161b49f3ef89521b34c38445fb2807264f9156c103629ca1d02` |

### 1.2 Declaración de Identidad y Reproducibilidad del Binario Ejecutable

- **Archivo Binario:** `bench_suite_decisive.exe`
- **Tamaño Físico:** **1.089.024 Bytes**
- **Hash SHA-256 Declarado:** `1518deab223a0a7f513720ce8d54f66fe288b26f8e2e9766bc63e7ce7d9c1527`
- **Estatus de Trazabilidad:** **Valor declarado de ejecución local**. Dado que el archivo `.gitignore` excluye deliberadamente ejecutables binarios (`*.exe`), su hash y contenido no son auditables como un objeto blob en GitHub.
- **Receta de Compilación Determinista:** El ejecutable fue compilado localmente a partir del commit habilitado [`140841f58312d6674238579dc39bd0aba8c3591c`](https://github.com/Santirobm/binpack-SDK/commit/140841f58312d6674238579dc39bd0aba8c3591c) mediante el comando:
  ```bash
  zig cc -O3 -march=x86_64 -mno-avx -mno-avx2 tests/benchmarks/benchmark_decisive_suite.c src/binpack.c deps/src/lz4.c deps/src/zstd/zstd.c deps/src/lzma/LzmaEnc.c deps/src/lzma/LzmaDec.c deps/src/lzma/Bra86.c -Iinclude -Ideps/include -Ideps/lzma -o bench_suite_decisive.exe -lpsapi -ladvapi32
  ```

---

## 2. Aislamiento en Windows y Desestimación de la Métrica de Memoria

En respuesta a la No Conformidad **NC-02**:
1. **Comportamiento en Plataforma Windows:** En Windows, la rutina `run_engine_isolated()` ejecuta la función del motor (`cfg->run_fn()`) directamente dentro del proceso anfitrión (*in-process*). El aislamiento por subprocesos basado en `fork()` y `wait4()` es exclusivo de la rama de compilación POSIX/Linux y no estuvo activo durante esta campaña.
2. **Leyenda de STDOUT:** La cadena textual `"Isolated Memory per Subprocess (fork/wait4)"` presente en la cabecera del registro STDOUT es un banner estático heredado del diseño Linux y **no describe el mecanismo operativo bajo Windows**.
3. **Desestimación de `PeakRSS_KB`:** El valor reportado en la columna `PeakRSS_KB` proviene de `GetProcessMemoryInfo(GetCurrentProcess(), &pmc, ...)` consultando `PeakWorkingSetSize`. En Windows, este valor representa el pico máximo acumulativo de working set alcanzado por el proceso completo desde su inicio, no el consumo puntual aislado de cada configuración.
4. **Disolución de Conclusiones de Memoria:** **Se retira formalmente toda interpretación de `PeakRSS_KB` como métrica de consumo aislado por motor.** Las conclusiones del expediente se basan exclusivamente en métricas de throughput (MB/s), ratio de compresión (%) y consistencia de datos, desestimando comparativas de huella de memoria en Windows.

---

## 3. Alcance Real de la Verificación Bit-Exact (28/28 PASS)

En respuesta a la No Conformidad **NC-03**, se precisa con exactitud el ciclo operativo del arnés:
1. **Fase Warm-up:** Se ejecuta 1 ciclo completo de compresión y descompresión, validando con `memcmp(decomp_buf, src, src_sz)` que el resultado sea idéntico al corpus original.
2. **Fase de Compresión Cronometrada (20 Muestras):** Se cronometran 20 iteraciones secuenciales de compresión que escriben consecutivamente en el búfer de compresión (`cmp_buf`). En cada iteración se verifica que la API devuelva éxito (`ZSTD_isError() == 0`, tamaños positivos válidos).
3. **Fase de Descompresión Cronometrada (20 Muestras):** Se cronometran 20 iteraciones de descompresión que leen el contenido del búfer comprimido producido en la última operación de la fase precedente. En cada una de las 20 descompresiones se valida bit a bit mediante `memcmp(decomp_buf, src, src_sz)`.
4. **Validación por Bloques (NASA Canal 0 y Latencias):** En las pruebas de latencia por bloques (4 KB y 64 KB), cada bloque individual es cotejado bit a bit con `memcmp(dbuf, blk, block_sz)`.
5. **Alcance Técnico Acreditado:** El registro de `Integrity = PASS` en las 28 filas acredita que el búfer comprimido generado por cada motor decodificó íntegra y perfectamente el archivo de origen en todas y cada una de las 20 iteraciones cronometradas; **no acredita la conservación ni verificación independiente de 20 búferes comprimidos separados físicamente**.

---

## 4. Matriz Oficial con Triple Criterio de Estabilidad Original (13 Estables / 15 Inestables)

En estricta subsanación de la No Conformidad **NC-01**, se restituye de forma vinculante el **triple criterio estadístico aprobado** en el protocolo R14:
- **Outliers:** `<= 2` outliers por dirección bajo la regla de Tukey (`Q1 = (x_(4) + x_(5))/2`, `Q3 = (x_(14) + x_(15))/2`).
- **CV Bruto:** `CV_raw <= 10.0%`.
- **CV Filtrado:** `CV_filt <= 5.0%`.

Bajo este criterio estricto, las dos configuraciones señaladas:
- **Fila 08 (`busybox_armv7l` / `BinPack_Default_L19`):** Presenta `CV_filt = 5.90% > 5.00%` en compresión. Se clasifica formalmente como **INESTABLE**.
- **Fila 13 (`nasa_turbofan_f32` / `BinPack_Transpose_F32`):** Presenta `CV_filt = 5.46% > 5.00%` en compresión. Se clasifica formalmente como **INESTABLE**.

| # | Dataset | Motor / Configuración | Ratio (%) | Comp Raw (MB/s ± IQR) | Decomp Raw (MB/s ± IQR) | Outl (C/D) | CV Comp (Bruto / Filt) | CV Decomp (Bruto / Filt) | Clasificación de Estabilidad Vinculante |
|:---:|---|---|:---:|:---:|:---:|:---:|:---:|:---:|---|
| 01 | `busybox_armv7l` | `LZ4_RawBlock` | 80.49% | 824.3 ± 9.4 | 5579.1 ± 150.5 | 1 / 1 | 1.25% / 0.79% | 2.35% / 2.07% | **ESTABLE** |
| 02 | `busybox_armv7l` | `Zstd_Frame_L19` | 51.04% | 6.7 ± 0.6 | 877.2 ± 5.4 | 2 / 2 | 6.82% / 4.26% | 0.99% / 0.60% | **ESTABLE** |
| 03 | `busybox_armv7l` | `RutaA_LZMA1_Puro` | 44.80% | 5.8 ± 0.2 | 61.3 ± 0.8 | 1 / 0 | 3.46% / 2.42% | 0.85% / 0.85% | **ESTABLE** |
| 04 | `busybox_armv7l` | `Zstd_Frame_L1` | 64.26% | 483.6 ± 6.6 | 1277.0 ± 8.0 | 1 / 3 | 1.23% / 0.97% | 1.93% / 0.51% | **INESTABLE** (Decomp Outl=3>2) |
| 05 | `busybox_armv7l` | `BinPack_Strict_CRC32` | 61.66% | — | 730.1 ± 8.4 | 0 / 0 | — | 0.74% / 0.74% | **ESTABLE** |
| 06 | `busybox_armv7l` | `RutaB_PavlovBCJ_LZMA1` | 42.46% | 5.7 ± 0.1 | 62.6 ± 0.6 | 4 / 0 | 6.74% / 2.97% | 0.77% / 0.77% | **INESTABLE** (Comp Outl=4>2) |
| 07 | `busybox_armv7l` | `BinPack_Convenience_API` | 61.66% | 356.3 ± 6.1 | 1053.3 ± 6.6 | 0 / 5 | 1.29% / 1.29% | 2.00% / 0.52% | **INESTABLE** (Decomp Outl=5>2) |
| 08 | `busybox_armv7l` | `BinPack_Default_L19` | 48.49% | 6.6 ± 0.6 | 602.4 ± 4.4 | 1 / 2 | 7.08% / 5.90% | 0.98% / 0.55% | **INESTABLE** (Comp CV_filt=5.90%>5%) |
| 09 | `busybox_armv7l` | `RutaC_BinPack_Archival` | 42.48% | 5.5 ± 0.1 | 62.3 ± 1.3 | 2 / 0 | 3.06% / 2.35% | 1.28% / 1.28% | **ESTABLE** |
| 10 | `busybox_armv7l` | `BinPack_Fast_ZeroCRC` | 61.66% | 367.6 ± 3.9 | 1050.6 ± 3.8 | 0 / 3 | 0.68% / 0.68% | 0.80% / 0.33% | **INESTABLE** (Decomp Outl=3>2) |
| 11 | `nasa_turbofan_f32` | `Gorilla_F32_Interleaved` | 106.18% | 45.1 ± 0.9 | 235.5 ± 2.0 | 3 / 0 | 6.26% / 1.33% | 0.54% / 0.54% | **INESTABLE** (Comp Outl=3>2) |
| 12 | `nasa_turbofan_f32` | `Chimp_F32_Interleaved` | 106.18% | 51.6 ± 0.1 | 244.5 ± 2.0 | 3 / 0 | 0.78% / 0.18% | 0.67% / 0.67% | **INESTABLE** (Comp Outl=3>2) |
| 13 | `nasa_turbofan_f32` | `BinPack_Transpose_F32` | 31.02% | 4.4 ± 0.3 | 967.3 ± 6.3 | 0 / 1 | 5.46% / 5.46% | 1.03% / 0.67% | **INESTABLE** (Comp CV_filt=5.46%>5%) |
| 14 | `nasa_turbofan_ch0` | `Gorilla_F32_SingleCh` | 104.96% | 38.8 ± 0.2 | 241.4 ± 2.7 | 2 / 0 | 0.67% / 0.50% | 0.83% / 0.83% | **ESTABLE** |
| 15 | `nasa_turbofan_ch0` | `Chimp_F32_SingleCh` | 105.00% | 43.1 ± 0.4 | 249.8 ± 1.7 | 0 / 0 | 0.72% / 0.72% | 0.37% / 0.37% | **ESTABLE** |
| 16 | `nasa_turbofan_ch0` | `BinPack_F32_SingleCh` | 29.67% | 4.5 ± 0.0 | 768.7 ± 8.6 | 1 / 1 | 2.74% / 0.19% | 3.01% / 1.21% | **ESTABLE** |
| 17 | `silesia_mozilla` | `LZ4_RawBlock` | 51.49% | 891.6 ± 6.5 | 4556.0 ± 321.2 | 1 / 2 | 0.52% / 0.36% | 11.20% / 3.71% | **INESTABLE** (Decomp CV_raw=11.2%>10%) |
| 18 | `silesia_mozilla` | `Zstd_Frame_L19` | 29.27% | 4.1 ± 0.1 | 1064.3 ± 7.3 | 0 / 3 | 1.71% / 1.71% | 1.03% / 0.51% | **INESTABLE** (Decomp Outl=3>2) |
| 19 | `silesia_mozilla` | `Zstd_Frame_L1` | 38.98% | 571.4 ± 12.5 | 1418.9 ± 3.1 | 1 / 3 | 1.81% / 1.12% | 2.40% / 0.24% | **INESTABLE** (Decomp Outl=3>2) |
| 20 | `silesia_mozilla` | `BinPack_Fast_ZeroCRC` | 38.99% | 478.2 ± 5.7 | 1515.6 ± 32.2 | 1 / 2 | 5.13% / 0.76% | 13.72% / 1.09% | **INESTABLE** (Decomp CV_raw=13.7%>10%) |
| 21 | `silesia_mozilla` | `BinPack_Default_L19` | 29.41% | 4.4 ± 0.0 | 763.4 ± 3.8 | 3 / 5 | 1.57% / 0.68% | 1.26% / 0.38% | **INESTABLE** (Comp Outl=3>2, Decomp Outl=5>2) |
| 22 | `silesia_mozilla` | `BinPack_Strict_CRC32` | 38.99% | — | 929.6 ± 2.6 | 0 / 3 | — | 0.84% / 0.25% | **INESTABLE** (Decomp Outl=3>2) |
| 23 | `silesia_samba` | `Zstd_Frame_L1` | 25.40% | 627.2 ± 1.9 | 2052.8 ± 20.4 | 4 / 0 | 1.25% / 0.25% | 0.67% / 0.67% | **INESTABLE** (Comp Outl=4>2) |
| 24 | `silesia_samba` | `BinPack_Strict_CRC32` | 25.40% | — | 1162.5 ± 6.2 | 0 / 0 | — | 0.36% / 0.36% | **ESTABLE** |
| 25 | `silesia_samba` | `BinPack_Default_L19` | 18.04% | 4.6 ± 0.0 | 1130.9 ± 22.1 | 2 / 1 | 1.09% / 0.72% | 1.60% / 1.07% | **ESTABLE** |
| 26 | `silesia_samba` | `Zstd_Frame_L19` | 17.96% | 4.5 ± 0.3 | 1967.1 ± 44.7 | 0 / 1 | 4.39% / 4.39% | 1.68% / 1.31% | **ESTABLE** |
| 27 | `silesia_samba` | `BinPack_Fast_ZeroCRC` | 25.40% | 511.2 ± 5.2 | 2307.3 ± 42.7 | 0 / 0 | 1.01% / 1.01% | 1.17% / 1.17% | **ESTABLE** |
| 28 | `silesia_samba` | `LZ4_RawBlock` | 35.32% | 854.5 ± 17.5 | 5334.5 ± 316.1 | 0 / 0 | 1.07% / 1.07% | 3.55% / 3.55% | **ESTABLE** |

### Resumen Estadístico Oficial
- **Configuraciones Plenamente Estables:** **13 de 28 (46.4%)**
- **Configuraciones Inestables / Sensibles a Outliers:** **15 de 28 (53.6%)**
- **Preservación Total de Datos:** La totalidad de las 15 configuraciones inestables se preserva en los archivos y análisis comparativos; no se aplica ningún descarte selectivo.

---

## 5. Síntesis Comparativa de Rendimiento Operativo

1. **Binarios ARMv7 (`busybox_armv7l`, 1,11 MB):**
   - **Tier 2 (Máxima Compresión):** `BinPack_Default_L19` alcanza **48,49%** superando a `Zstd_Frame_L19` (**51,04%**). En descompresión rinde **602,4 MB/s** vs **877,2 MB/s** de Zstd L19.
   - **Tier 3 (Archivo Extremo):** `PavlovBCJ` (**42,46%**) y `BinPack Archival` (**42,48%**) superan ampliamente a `LZMA1 Puro` (**44,80%**).
   - **Tier 1 (Velocidad):** `LZ4` descomprime a **5.579,1 MB/s** (ratio **80,49%**). `BinPack Fast Zero-CRC` comprime a **367,6 MB/s** (ratio **61,66%**) y descomprime a **1.050,6 MB/s**.
2. **Telemetría NASA (`nasa_turbofan_f32`, 1,98 MB):**
   - **Entrelazado:** `Gorilla` (**106,18%**) y `Chimp` (**106,18%**) presentan expansión. `BinPack Transpose-F32` compacta a **31,02%** con descompresión a **967,3 MB/s**.
   - **Canal Único (Canal 0, 82,5 KB):** `Gorilla` (**104,96%**) y `Chimp` (**105,00%**) continúan expandiendo. `BinPack Single-Channel` alcanza **29,67%** con descompresión a **768,7 MB/s**.
3. **Ejecutable x86 (`silesia_mozilla`, 51,22 MB):**
   - **Tier 2:** `Zstd L19` alcanza **29,27%** (decomp: 1.064,3 MB/s); `BinPack Default L19` alcanza **29,41%** (decomp: 763,4 MB/s).
   - **Tier 1:** `BinPack Fast Zero-CRC` y `Zstd L1` igualan exactamente en ratio (**38,99%**), con BinPack descomprimiendo a **1.515,6 MB/s** frente a **1.418,9 MB/s** de Zstd L1.
4. **Código C Estructurado (`silesia_samba`, 21,61 MB):**
   - **Tier 2:** `Zstd L19` (**17,96%**, decomp: 1.967,1 MB/s) y `BinPack Default L19` (**18,04%**, decomp: 1.130,9 MB/s).
   - **Tier 1:** `BinPack Fast Zero-CRC` descomprime a **2.307,3 MB/s** (ratio **25,40%**), superando a `Zstd L1` (**2.052,8 MB/s**).
