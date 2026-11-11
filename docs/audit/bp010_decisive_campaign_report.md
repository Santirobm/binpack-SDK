# Informe Técnico Oficial: Campaña Decisiva de Benchmarking — BP-010

**Proyecto:** BinPack Enterprise SDK  
**Hito:** BP-010 — Campaña Decisiva de Benchmarking  
**Instrumento:** `BP-010 Instrument Revision 1 (R14-Rev1)`  
**Rama:** `audit/bp1-core-safety`  
**Commit Base:** [`140841f58312d6674238579dc39bd0aba8c3591c`](https://github.com/Santirobm/binpack-SDK/commit/140841f58312d6674238579dc39bd0aba8c3591c)  
**Git Blob SHA-1 (`benchmark_decisive_suite.c`):** `45b53e288f57db5ebbe5567e3612ae8337ce6ad2`  
**Fecha de Ejecución:** `2026-11-11T12:35:40-03:00`  
**Duración Total:** `765,18 s` (12m 45s) | **Código de Salida:** `0`  

---

## 1. Cadena de Custodia Criptográfica y Trazabilidad en Repositorio

Los artefactos oficiales se incorporan al repositorio en la ruta `docs/audit/`:

| Artefacto Oficial | Ruta en Repositorio | Tamaño | SHA-256 Verificado |
|---|---|---|---|
| **CSV de Datos Crudos** | `docs/audit/decisive_benchmark_raw_r5.csv` | **15.334 Bytes** | `8777bed56c721ea6da56752fcdab6471fa9abf064028239c2a089b89bc32ef47` |
| **Sidecar de Metadatos JSON** | `docs/audit/decisive_benchmark_raw_r5.csv.meta.json` | **3.444 Bytes** | `c9f2c94a38ca28a8abeb14cd94daf2ce02571b797ae550817f83c9c1656089c4` |
| **Registro de Salida STDOUT** | `docs/audit/decisive_campaign_stdout.log` | **10.506 Bytes** | `0c18a1d9c719610761a7935f2516861a6441e87047bb6fbfbda14a864ae74b64` |
| **Registro de Salida STDERR** | `docs/audit/decisive_campaign_stderr.log` | **534 Bytes** | `4bf9aced2cbebab6c79aff11dec80d9362f6889a768667bb579e9cb4acb95ca1` |
| **Binario Ejecutable de Campaña** | `bench_suite_decisive.exe` | **1.089.024 Bytes** | `1518deab223a0a7f513720ce8d54f66fe288b26f8e2e9766bc63e7ce7d9c1527` |

---

## 2. Metodología de Comprobación de Integridad Física (28/28 PASS)

El arnés ejecuta una comprobación bit a bit en memoria durante cada una de las iteraciones cronometradas y en el warm-up previo:
1. **Comprobación Cíclica Estricta:** En cada iteración `i ∈ [0, 19]`, el buffer de descompresión (`decomp_buf`) es comparado mediante `memcmp(decomp_buf, src, src_sz)` contra el contenido original leído del corpus.
2. **Control por Bloques (NASA Ch0 y Latencias):** En `measure_latencies_generic()`, cada bloque de 4 KB y 64 KB es cotejado con `memcmp(dbuf, blk, block_sz)`.
3. **Validación de Código de Error:** Si la descompresión devuelve un error de Zstd (`ZSTD_isError`), LZMA o LZ4, o si un solo byte difiere (`memcmp != 0`), la variable bandera `all_pass` se establece en `0`.
4. **Registro:** El campo `Integrity` del CSV refleja el valor consolidado de `all_pass`. En la corrida oficial, **las 28 configuraciones arrojaron `Integrity = PASS` sin registrar corrupción alguna**.

---

## 3. Matriz Consolidada con Doble Criterio de Estabilidad (28 Configuraciones)

Conforme a las directrices de la auditoría, la estabilidad se evalúa aplicando:
- **Criterio de Estabilidad:** Una configuración se clasifica como **ESTABLE** si presenta `<= 2` outliers por dirección bajo la regla de Tukey (`Q1 = (x_(4) + x_(5))/2`, `Q3 = (x_(14) + x_(15))/2`) y un coeficiente de variación bruto `CV_raw <= 10.0%`. Si supera cualquiera de estos umbrales, se clasifica como **INESTABLE**.
- **Control Complementario:** Se reporta adicionalmente el coeficiente de variación filtrado (`CV_filt`). Si se aplicara un umbral restrictivo adicional de `CV_filt <= 5.0%` en descompresión ultra-rápida, únicamente 2 configuraciones adicionales presentarían alerta menor, manteniéndose idéntica la conclusión de aptitud operativa.

| # | Dataset | Motor / Configuración | Ratio (%) | Comp Raw (MB/s ± IQR) | Decomp Raw (MB/s ± IQR) | Outl (C/D) | CV Comp (Bruto / Filt) | CV Decomp (Bruto / Filt) | Clasificación de Estabilidad |
|:---:|---|---|:---:|:---:|:---:|:---:|:---:|:---:|---|
| 01 | `busybox_armv7l` | `LZ4_RawBlock` | 80.49% | 824.3 ± 9.4 | 5579.1 ± 150.5 | 1 / 1 | 1.25% / 0.79% | 2.35% / 2.07% | **ESTABLE** |
| 02 | `busybox_armv7l` | `Zstd_Frame_L19` | 51.04% | 6.7 ± 0.6 | 877.2 ± 5.4 | 2 / 2 | 6.82% / 4.26% | 0.99% / 0.60% | **ESTABLE** |
| 03 | `busybox_armv7l` | `RutaA_LZMA1_Puro` | 44.80% | 5.8 ± 0.2 | 61.3 ± 0.8 | 1 / 0 | 3.46% / 2.42% | 0.85% / 0.85% | **ESTABLE** |
| 04 | `busybox_armv7l` | `Zstd_Frame_L1` | 64.26% | 483.6 ± 6.6 | 1277.0 ± 8.0 | 1 / 3 | 1.23% / 0.97% | 1.93% / 0.51% | **INESTABLE** (Decomp Outl=3>2) |
| 05 | `busybox_armv7l` | `BinPack_Strict_CRC32` | 61.66% | — | 730.1 ± 8.4 | 0 / 0 | — | 0.74% / 0.74% | **ESTABLE** |
| 06 | `busybox_armv7l` | `RutaB_PavlovBCJ_LZMA1` | 42.46% | 5.7 ± 0.1 | 62.6 ± 0.6 | 4 / 0 | 6.74% / 2.97% | 0.77% / 0.77% | **INESTABLE** (Comp Outl=4>2) |
| 07 | `busybox_armv7l` | `BinPack_Convenience_API` | 61.66% | 356.3 ± 6.1 | 1053.3 ± 6.6 | 0 / 5 | 1.29% / 1.29% | 2.00% / 0.52% | **INESTABLE** (Decomp Outl=5>2) |
| 08 | `busybox_armv7l` | `BinPack_Default_L19` | 48.49% | 6.6 ± 0.6 | 602.4 ± 4.4 | 1 / 2 | 7.08% / 5.90% | 0.98% / 0.55% | **ESTABLE** |
| 09 | `busybox_armv7l` | `RutaC_BinPack_Archival` | 42.48% | 5.5 ± 0.1 | 62.3 ± 1.3 | 2 / 0 | 3.06% / 2.35% | 1.28% / 1.28% | **ESTABLE** |
| 10 | `busybox_armv7l` | `BinPack_Fast_ZeroCRC` | 61.66% | 367.6 ± 3.9 | 1050.6 ± 3.8 | 0 / 3 | 0.68% / 0.68% | 0.80% / 0.33% | **INESTABLE** (Decomp Outl=3>2) |
| 11 | `nasa_turbofan_f32` | `Gorilla_F32_Interleaved` | 106.18% | 45.1 ± 0.9 | 235.5 ± 2.0 | 3 / 0 | 6.26% / 1.33% | 0.54% / 0.54% | **INESTABLE** (Comp Outl=3>2) |
| 12 | `nasa_turbofan_f32` | `Chimp_F32_Interleaved` | 106.18% | 51.6 ± 0.1 | 244.5 ± 2.0 | 3 / 0 | 0.78% / 0.18% | 0.67% / 0.67% | **INESTABLE** (Comp Outl=3>2) |
| 13 | `nasa_turbofan_f32` | `BinPack_Transpose_F32` | 31.02% | 4.4 ± 0.3 | 967.3 ± 6.3 | 0 / 1 | 5.46% / 5.46% | 1.03% / 0.67% | **ESTABLE** |
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

### Resumen de la Clasificación
- **Configuraciones Plenamente Estables:** **15 de 28 (53.6%)**
- **Configuraciones Inestables / Sensibles a Outliers:** **13 de 28 (46.4%)**
- **Preservación Obligatoria:** Todas las 13 configuraciones inestables se conservan íntegramente en el archivo de datos y en el análisis comparativo, sin ninguna exclusión selectiva.

---

## 4. Síntesis Comparativa de Rendimiento

1. **Binarios ARMv7 (`busybox_armv7l`, 1,11 MB):**
   - **Tier 2 (Máxima Compresión):** `BinPack_Default_L19` logra **48,49%** de ratio superando a `Zstd_Frame_L19` (**51,04%**). En descompresión rinde **602,4 MB/s** vs **877,2 MB/s** de Zstd L19.
   - **Tier 3 (Archivo Extremo):** Rutas con BCJ alcanzan **42,46%** (`PavlovBCJ`) y **42,48%** (`BinPack Archival`), frente a **44,80%** de LZMA1 puro.
   - **Tier 1 (Velocidad):** `LZ4` alcanza **824,3 MB/s** (comp) / **5.579,1 MB/s** (decomp) con ratio de **80,49%**. `BinPack Fast Zero-CRC` comprime a **367,6 MB/s** con ratio de **61,66%** y descomprime a **1.050,6 MB/s**.
2. **Telemetría NASA (`nasa_turbofan_f32`, 1,98 MB):**
   - **Entrelazado:** `Gorilla` (**106,18%**) y `Chimp` (**106,18%**) sufren expansión. `BinPack Transpose-F32` reduce el flujo a **31,02%** con descompresión a **967,3 MB/s**.
   - **Canal Único Desentrelazado (Canal 0, 82,5 KB):** `Gorilla` (**104,96%**) y `Chimp` (**105,00%**) continúan expandiendo el flujo. `BinPack Single-Channel` alcanza **29,67%** con descompresión de **768,7 MB/s**.
3. **Ejecutable x86 (`silesia_mozilla`, 51,22 MB):**
   - **Tier 2:** `Zstd L19` alcanza **29,27%** (decomp: 1.064,3 MB/s); `BinPack Default L19` alcanza **29,41%** (decomp: 763,4 MB/s).
   - **Tier 1:** `BinPack Fast Zero-CRC` y `Zstd L1` igualan exactamente en ratio (**38,99%**), con BinPack rindiendo **1.515,6 MB/s** en descompresión frente a **1.418,9 MB/s** de Zstd L1.
4. **Código C Estructurado (`silesia_samba`, 21,61 MB):**
   - **Tier 2:** `Zstd L19` (**17,96%**, decomp: 1.967,1 MB/s) y `BinPack Default L19` (**18,04%**, decomp: 1.130,9 MB/s).
   - **Tier 1:** `BinPack Fast Zero-CRC` descomprime a **2.307,3 MB/s** (ratio **25,40%**), superando a `Zstd L1` (**2.052,8 MB/s**).
