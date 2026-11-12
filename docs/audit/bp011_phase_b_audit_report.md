# Informe de Auditoría Estática — Hito BP-011 / Fase B
## Inspección Forense de Implementaciones de Codecs e Integridad de Datasets

**Proyecto:** BinPack Enterprise SDK  
**Hito:** BP-011  
**Fase:** Fase B — Auditoría Estática de Codecs y Datasets  
**Rama:** `audit/bp1-core-safety`  
**Documento Rector:** `docs/audit/bp011_phase_b_audit_report.md`  
**Fecha:** 12 de noviembre de 2026  
**Estado:** EXPEDIENTE FORMAL DE AUDITORÍA — FASE B SUBSANADA  
**Modo Operativo:** Exclusivamente de Solo Lectura (Code Freeze Estricto Preservado)

---

## 1. Alcance, Mandato y Metodología de la Fase B

Conforme a la autorización emitida en el Dictamen Definitivo de Fase A (Protocolo R15.10), se procedió a la ejecución de la **Fase B** bajo estricto modo de solo lectura. En ningún momento se levantó el *code freeze*:
- Cero modificaciones a código en `src/`, `include/`, `deps/` o `tests/`.
- Cero compilaciones, enlaces o ejecuciones de código binario.
- Cero descargas o mutaciones sobre los directorios de corpus y manifiestos.

La inspección se dividió en dos ejes complementarios:
1. **Eje B.1 (Auditoría de Codecs):** Inspección estática de las implementaciones fuente de Gorilla, Chimp y BinPack disponibles en `deps/timeseries/timeseries_codecs.h` y `src/`, evaluando su correspondencia con la literatura técnica y con el protocolo R15.10.
2. **Eje B.2 (Auditoría de Datasets):** Inspección forense de los archivos de corpus inventariados, verificación criptográfica SHA-256 frente a `tests/experiments/datasets_checksums.json`, tipificación de datos (crudos vs. derivados) y detección de datasets ausentes.

---

## 2. Eje B.1 — Auditoría Forense de Implementaciones de Codecs

La inspección del archivo [`deps/timeseries/timeseries_codecs.h`](file:///C:/Users/sgi.lm/Desktop/binpack-SDK/deps/timeseries/timeseries_codecs.h) y de los módulos de filtrado en [`src/binpack_core.c`](file:///C:/Users/sgi.lm/Desktop/binpack-SDK/src/binpack_core.c) arrojó los siguientes hallazgos estructurales:

### 2.1 Codec Facebook Gorilla (VLDB 2015)
- **Implementación Actual:** Funciones `gorilla_compress_f32`, `gorilla_decompress_f32`, `gorilla_compress_f64`, `gorilla_decompress_f64`.
- **Hallazgo Crítico 1 (Ausencia Total de Compresión Temporal):**  
  La implementación disponible en `timeseries_codecs.h` codifica **únicamente streams de valores flotantes**. No existe ninguna rutina de compresión de marcas de tiempo (*timestamps*). En las campañas previas (BP-010), Gorilla se ejecutó exclusivamente sobre matrices planas de valores flotantes mediante reinterpretación de memoria (`(const float*)src`).
- **Hallazgo Crítico 2 (Mecanismo de Valores XOR):**  
  La compresión de valores flotantes sigue la heurística de Pelkonen et al. (2015):
  - Primer valor: 32 bits literales.
  - Sucesivos: $x_i = v_i \oplus v_{i-1}$. Si $x_i = 0 \implies$ bit `0`. Si $x_i \ne 0 \implies$ bit `1`. Si los ceros iniciales (`lz`) y finales (`tz`) están contenidos en la ventana previa, emite bit de control `0` y los bits significativos. Si no, emite bit `1`, longitud de `lz` (5 bits), cantidad de bits significativos (6 bits) y el payload.
- **Hallazgo Crítico 3 (Inexistencia de Metadatos de Longitud en Contenedor):**  
  El bitstream generado por `gorilla_compress_f32` no incluye cabecera, número mágico ni el conteo de elementos $N$. El decodificador `gorilla_decompress_f32` requiere imperativamente que el llamador le suministre externamente el parámetro `size_t count`.

### 2.2 Codec Chimp y Variante Chimp128 (VLDB 2022)
- **Implementación Actual:** Funciones `chimp_compress_f32` y `chimp_decompress_f32`.
- **Hallazgo Crítico 4 (Ausencia de Chimp128):**  
  En el repositorio **no existe ninguna implementación de Chimp128**. El código actual corresponde exclusivamente a la variante elemental de Chimp para Float32 con un único valor de referencia previo ($v_{i-1}$).
- **Hallazgo Crítico 5 (Ausencia de Ring-Buffer):**  
  La variante canónica Chimp128 (Liakos et al., VLDB 2022) exige un buffer circular (*ring-buffer*) de 128 valores previos flotantes, evaluando en cada paso el índice $j \in [0, 127]$ que maximice los ceros iniciales en $v_i \oplus v_{\text{buf}[j]}$ y emitiendo 7 bits de índice. Dicha estructura de datos y lógica de búsqueda no existen en `timeseries_codecs.h`.
- **Hallazgo Crítico 6 (Ausencia de Float64 y Timestamps en Chimp):**  
  No existe implementación de Chimp para doble precisión (`float64`), ni codificación para marcas temporales.

### 2.3 Seguridad y Contención del Bitstream (Observación Obligatoria)
- **Vulnerabilidad de Desbordamiento de Buffer:**  
  La inspección estática de las rutinas de lectura y escritura en `timeseries_codecs.h` revela:
  - `bw_write_bit()` incrementa `bw->bit_pos` y escribe en `bw->buf[byte_idx]` sin verificar si `byte_idx < bw->cap`.
  - `br_read_bit()` incrementa `br->bit_pos` y lee de `br->buf[byte_idx]` sin verificar si `br->bit_pos < br->bit_len`.
- **Mitigación Mandatada para la Fase C:**  
  Los codecs de `timeseries_codecs.h` no deben alimentarse directamente con buffers arbitrarios sin contención. Para la Fase C, se exige implementar en el espacio experimental (`tests/experiments/bp011/`) estructuras seguras con comprobación estricta de límites (`bit_writer_safe_t` y `bit_reader_safe_t`), sin modificar el archivo productivo congelado `deps/timeseries/timeseries_codecs.h`.

### 2.4 Implementación BinPack Transpose (Motores M4, M5, M6, M7)
- **Implementación Actual:** `binpack_transpose_delta_filter_into` y `binpack_transpose_delta_unfilter` en `src/binpack_core.c` (escalar portable) y `src/binpack_core_avx2.c` (optimización SIMD AVX2).
- **Mecanismo:**  
  1. *De-interleave:* Desentrelaza el bloque de datos por un paso (*stride*) de tamaño $S$ (ej. $S = 4$ para Float32, o $S = 4 \times K$ para $K$ canales).
  2. *Byte-Delta:* Aplica acumulación de diferencias finitas en cada plano de bytes de forma continua.
  3. *Compresión:* Alimenta el buffer filtrado hacia el motor L19 de BinPack o Zstandard.
- **Capacidad de Acceso Selectivo:**  
  La transposición actual opera a nivel de bloque en memoria. No cuenta con un índice o tabla de saltos interna que permita extraer un único canal sin desentrelazar el bloque completo, lo cual impacta directamente en las métricas de amplificación $A_{\text{read}}$ y $A_{\text{decomp}}$ bajo consultas selectivas monocanal ($Q_1$ y $Q_2$).

---

## 3. Eje B.2 — Auditoría Forense de Datasets e Integridad Criptográfica

Se realizó el cotejo físico, inventario criptográfico y clasificación de todos los datasets ubicados en `tests/corpora/nasa_telemetry/` frente al manifiesto oficial `tests/experiments/datasets_checksums.json`:

### 3.1 Registro Exhaustivo de Verificación Física SHA-256

| Dataset ID | Ruta Local en Repositorio | Tipificación del Dato | Tamaño (Bytes) | Hash SHA-256 Esperado (Manifiesto) | Hash SHA-256 Físico Calculado | Estado de Coincidencia |
| :--- | :--- | :--- | :---: | :--- | :--- | :---: |
| **DS-01** | `tests/corpora/nasa_telemetry/nasa_bearing_ims_test1.csv` | **Resumen Estadístico Precalculado** | 1.905.892 | `92df06fc3e54be5925c1bab9570741d6218354d193a1f27d5a26fd9f3415273c` | `92df06fc3e54be5925c1bab9570741d6218354d193a1f27d5a26fd9f3415273c` | **100% EXACTO** (Línea 76) |
| **DS-02 (Bin)** | `tests/corpora/nasa_telemetry/nasa_turbofan_float32.bin` | **Matriz Binaria Derivada** | 1.980.576 | `ee06f50d129b163884753299939c7e150d785e63928de860777b9f7869ce7c59` | `ee06f50d129b163884753299939c7e150d785e63928de860777b9f7869ce7c59` | **100% EXACTO** (Línea 60) |
| **DS-02 (Src)** | `tests/corpora/nasa_telemetry/nasa_turbofan_ascii.txt` | **Fuente Primaria Simulada (ASCII)** | 3.515.356 | `963b5e22825b34d8b21c69e1aeb4af3e647050eb672ee8834ba4b5d91d2de0f8` | `963b5e22825b34d8b21c69e1aeb4af3e647050eb672ee8834ba4b5d91d2de0f8` | **100% EXACTO** (Línea 44) |
| **IMS Test 2** | `tests/corpora/nasa_telemetry/nasa_bearing_ims_test2.csv` | **Resumen Estadístico Precalculado** | 462.350 | `d1889c18df68b68a759794dd9a4eb58c92fa55c0f2ef7ff719e5b0a33a49eefc` | `d1889c18df68b68a759794dd9a4eb58c92fa55c0f2ef7ff719e5b0a33a49eefc` | **100% EXACTO** (Línea 94) |
| **IMS Test 3** | `tests/corpora/nasa_telemetry/nasa_bearing_ims_test3.csv` | **Resumen Estadístico Precalculado** | 2.974.328 | `56f58a957ecbed1321cc67f5283f7d356c8259ca2b4b08c02f050a8a2076e0e6` | `56f58a957ecbed1321cc67f5283f7d356c8259ca2b4b08c02f050a8a2076e0e6` | **100% EXACTO** (Línea 112) |

### 3.2 Tipificación y Limitaciones Estructurales de los Datasets

1. **DS-01 (`nasa_bearing_ims_test1.csv`):**
   - **Tipificación:** Resumen estadístico precalculado (6 métricas $\times$ 8 canales = 48 variables continuas).
   - **Muestras:** 2.156 registros.
   - **Timestamps:** Marcas de tiempo reales (`YYYY-MM-DD HH:MM:SS`) espaciadas irregularmente entre 5 y 10 minutos.
   - **Aplicabilidad:** $Q_1$ (1.000 pts) y $Q_3$ (4 canales) son aplicables; $Q_2$ (10.000 pts) es **`NOT_APPLICABLE`**.
2. **DS-02 (`nasa_turbofan_float32.bin` y `nasa_turbofan_ascii.txt`):**
   - **Tipificación:** Matriz de telemetría derivada de simulación de motores a reacción (C-MAPSS).
   - **Muestras y Canales:** 20.631 registros $\times$ 24 canales numéricos.
   - **Estructura Temporal y Regla de No-Continuidad:**  
     El archivo ASCII contiene `unit_id` (identificador del motor, 1 a 100) y `cycle` (ciclo operativo discreto del motor). Al cambiar de `unit_id`, la secuencia temporal se reinicia. Por tanto, en la Fase C el arnés debe tratar cada motor como una **serie independiente**, forzando el reinicio del estado del codificador temporal (`has_prev_delta = false`) en cada cambio de motor, prohibiendo expresamente calcular deltas temporales espurios entre motores distintos.
3. **DS-03 y DS-04:**
   - **Estado:** **Materialmente ausentes del repositorio**. En el protocolo R15.10 fueron catalogados como `PENDIENTE (Fase B)`. Queda asentado que no forman parte del conjunto de evaluación inmediata.

---

## 4. Conclusiones y Transición a la Fase C

1. **Evidencia Forense Completa:**  
   La verificación física de hashes demuestra que los datasets presentes en disco son exactamente idénticos a los comprometidos en el manifiesto oficial de integridad criptográfica.
2. **Subsanación de la Reserva de Fase B:**  
   El presente documento se integra formalmente en la rama `audit/bp1-core-safety` bajo `docs/audit/bp011_phase_b_audit_report.md`, aportando la trazabilidad física completa requerida por el Auditor Líder.
3. **Mandato Metodológico para la Fase C:**  
   Se asumen formalmente los requerimientos C.1 a C.5 autorizados para el diseño del nuevo arnés experimental aislado en `tests/experiments/bp011/`.
