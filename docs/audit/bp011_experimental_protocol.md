# Protocolo Experimental R15.3 — Hito BP-011
## Industrial Time-Series Benchmark: Gorilla & Chimp vs. BinPack Transpose

**Proyecto:** BinPack Enterprise SDK  
**Hito:** BP-011  
**Fase:** Fase A — Acta de Apertura y Protocolo Experimental (Revisión R15.3)  
**Documento Rector:** `docs/audit/bp011_experimental_protocol.md`  
**Rama:** `audit/bp1-core-safety`  
**Estado:** PROTOCOLO FORMAL DEFINITIVO PARA DICTAMEN DE CIERRE DE FASE A  

---

## Control de Cambios y Registro de Subsanaciones

### Revisión R15.1
- **NC-01 (Cuartiles de Tukey):** Desambiguación de base 1 ($x_{(5)}, x_{(6)}$ y $x_{(15)}, x_{(16)}$) y base 0 (`arr[4]`, `arr[5]` y `arr[14]`, `arr[15]`).
- **NC-02 (Separación de Amplificaciones):** Definición de $A_{\text{read}}$, $A_{\text{decomp}}$ y $A_{\text{store}}$.
- **NC-03 (Aplicabilidad de Consultas):** Regla de cardinalidad y designación de $Q_2$ como `NOT_APPLICABLE` en DS-01 ($N < 10.000$).
- **OBS-01 a OBS-06:** Desglose en 4 componentes de tamaño, densidades (bpv, bpt, bptuple, bpc), Hot/Cold Cache, dispersión muestral y concurrencia ($Q_{\text{conc}}$).

### Revisión R15.2
- **NC-04:** Adopción del escape absoluto de 64 bits para deltas superiores a 32 bits.
- **Unidades:** Unificación canónica a microsegundos (`int64_t`).
- **Signo:** Especificación del mapeo offset-binary para rangos de Gorilla.
- **Inferencia:** Incorporación de muestras emparejadas y corrección de Holm-Bonferroni.

### Revisión R15.3 (Presente Documento Definitivo)
1. **NC-04 (Máquina de Estados de Reinicio Temporal y Aritmética Segura):** Se define formalmente el contrato de transición simétrico del decodificador/codificador tras un escape absoluto, introduciendo el flag `has_prev_delta` y la codificación de transición de $\Delta t_{i+1}$. Se explicita la comprobación en 3 pasos previos de desbordamiento de `int64_t` antes de efectuar la doble diferencia.
2. **Serialización y Endianness Bit-Exacto:** Se fija la serialización Big-Endian a nivel de bitstream y la reinterpretación estricta de bits para `int64_t` en complemento a 2, garantizando la preservación de timestamps negativos y extremos del dominio.
3. **OBS-04 (Diseño Estadístico Emparejado, Selección de Pruebas y Orientación de Mejora):** Se define la tupla cuádruple de emparejamiento `(Dataset, Bloque, Consulta, Ronda_i)`, la convención de signo para mejora relativa $\Delta_{\text{rel}}$ según el sentido de la métrica (menor es mejor vs. mayor es mejor), la regla de selección entre prueba $t$ emparejada, Wilcoxon y Sign Test, y la fórmula del intervalo de confianza del 95% para $\Delta_{\text{rel}}$.
4. **Delimitación de Familias Holm-Bonferroni:** Se fija la familia de hipótesis a nivel de cada terna `(Dataset, Consulta, Métrica)` para controlar la tasa de error familiar en $\alpha = 0.05$.
5. **Rol de Chimp128 en la Clasificación Comercial:** Se establece que la "Ventaja Concluyente" requiere superar de forma obligatoria a los dos codecs canónicos de referencia primaria (Gorilla y Chimp estándar). Chimp128 se categoriza como baseline especializado con ring-buffer, cuya ventaja o paridad se cataloga como "Ventaja Especializada".

---

## 1. Mandato, Gobernanza y Pregunta de Negocio

### 1.1 Pregunta Estratégica Central
El hito previo (BP-010) acreditó que BinPack es un compresor generalista robusto, pero constató la ausencia de ventajas decisivas frente a herramientas consolidadas (Zstd, LZ4). El hito BP-011 abandona la pretensión de universalidad para investigar una hipótesis específica de alto valor técnico y comercial:

> **¿Ofrece BinPack una ventaja competitiva medible y reproducible frente a codecs especializados en series temporales (Gorilla, Chimp/Chimp128) y estándares de la industria (Zstandard), en escenarios de telemetría industrial donde conviven la eficiencia de almacenamiento y la agilidad de consulta selectiva?**

El propósito no es forzar una conclusión favorable para BinPack, sino identificar de forma objetiva su verdadero perfil de aptitud:
- Si BinPack demuestra superioridad en almacenamiento o en consultas selectivas, se fundamenta una propuesta de valor comercial sólida para bases de datos de series temporales (TSDB) e historiadores industriales.
- Si Gorilla o Chimp demuestran superioridad integral en almacenamiento y latencia, se obtiene una delimitación técnica clara que evitará desviar recursos comerciales hacia casos de uso donde el motor no es competitivo.

### 1.2 Estructura de Gobernanza por Fases
Para salvaguardar la integridad metodológica, BP-011 se rige por seis fases secuenciales estrictas:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                       GOBERNANZA POR FASES — BP-011                         │
├─────────┬──────────────────────────────────────────┬────────────────────────┤
│ Fase A  │ Acta y Protocolo Experimental R15.3      │ Solo Documentación     │
│ Fase B  │ Auditoría de Codecs, Papers y Datasets   │ Solo Lectura           │
│ Fase C  │ Diseño e Implementación del Arnés TSDB   │ Sin Ejecución          │
│ Fase D  │ Auditoría Estática Pre-Flight del Arnés  │ Sin Compilación        │
│ Fase E  │ Campaña Experimental Oficial             │ Solo con Autorización  │
│ Fase F  │ Análisis Estadístico y Dictamen Final    │ Evidencia Reproducible │
└─────────┴──────────────────────────────────────────┴────────────────────────┘
```

#### Barreras de Contención Vigentes en Fase A:
1. **Ámbito Exclusivo:** Toda modificación está confinada a `docs/audit/bp011_experimental_protocol.md`.
2. **Code Freeze Estricto:** Cero modificaciones a `src/`, `include/`, `deps/` o arneses previos en `tests/`.
3. **Prohibición de Ejecución:** No se permite compilar, ejecutar benchmarks, cronometrar ni descargar archivos externos.
4. **Independencia Probatoria:** Queda expresamente prohibido reutilizar resultados de BP-010 como evidencia de superioridad en series temporales.
5. **Transición a Fase B:** La Fase B no comenzará hasta que la presente revisión R15.3 cuente con aprobación formal de la auditoría.

---

## 2. Taxonomía de Motores, Composiciones Arquitectónicas y Desglose Contable

Para garantizar paridad estricta, cada motor evaluado cuenta con una definición algorítmica explícita que incluye el manejo integral de marcas de tiempo (*timestamps*), valores y sobrecarga de contenedor (*overhead*).

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                        TAXONOMÍA DE MOTORES DE EVALUACIÓN                               │
├──────────────────────────┬──────────────────────────┬───────────────────────────────────┤
│ Motor                    │ Algoritmo de Tiempo      │ Algoritmo de Valores Flotantes    │
├──────────────────────────┼──────────────────────────┼───────────────────────────────────┤
│ Gorilla_Canonical        │ Delta-of-Delta con Escape│ XOR flotante con ventana de ceros │
│ Chimp_Composed           │ Delta-of-Delta canónico  │ Chimp flotante (VLDB22)           │
│ Chimp128_Composed        │ Delta-of-Delta canónico  │ Chimp128 con Ring-Buffer (VLDB22) │
│ BinPack_Transpose_Multi  │ Transposición columnar   │ Transposición de bytes + BinPack  │
│ BinPack_Chunked_Channel  │ Segmentación monocanal   │ Transposición monocanal + BinPack │
│ Zstd_Structured_L1       │ Estructura binaria fija  │ Zstandard Nivel 1                 │
│ Zstd_Structured_L3       │ Estructura binaria fija  │ Zstandard Nivel 3 (Baseline TSDB) │
└──────────────────────────┴──────────────────────────┴───────────────────────────────────┘
```

### 2.1 Especificación de los Motores Evaluados

1. **`Gorilla_Canonical` (Facebook / VLDB 2015):**
   - **Timestamps:** Algoritmo *Delta-of-Delta* extendido con escape de reinicio absoluto para 64 bits (ver Sección 2.3).
   - **Valores:** XOR bitwise con seguimiento de ceros a la izquierda (*leading zeros*) y a la derecha (*trailing zeros*).
2. **`Chimp_Composed` (VLDB 2022):**
   - **Valores:** Implementación canónica de Chimp con codificación optimizada de ceros a la izquierda (3 bits de longitud) y detección de ceros a la derecha.
   - **Timestamps:** Dado que Chimp es un codec de coma flotante puro y no define un estándar de timestamps, se acopla a la rutina canónica *Delta-of-Delta* de Gorilla. Se identifica formalmente como arquitectura integrada (`Chimp + DoD_Time`).
3. **`Chimp128_Composed` (VLDB 2022):**
   - **Valores:** Chimp con buffer circular (*ring-buffer*) de 128 valores previos para maximizar la probabilidad de coincidencias XOR en series con oscilaciones periódicas.
   - **Timestamps:** Acoplado idénticamente a *Delta-of-Delta*.
4. **`BinPack_Transpose_Multichannel`:**
   - **Estructura:** Disposición columnar del bloque multicanal: vector temporal $T$ seguido de matrices de canales $C_0, C_1, \dots, C_{K-1}$.
   - **Transformación:** Pre-filtro de transposición estructural de bytes aplicado por canal, agrupando bytes de signo/exponente y mantisa para optimizar la compresión entrópica mediante BinPack.
5. **`BinPack_Chunked_PerChannel`:**
   - **Estructura:** Bloques independientes por canal sensor, conteniendo cada uno su copia o referencia de la base temporal correspondiente.
   - **Propósito:** Evaluar el coste de compresión y la ventaja en lectura cuando las consultas TSDB solicitan proyecciones de una sola columna.
6. **`Zstandard_Structured` (Niveles 1 y 3):**
   - **Línea Base Generalista:** Compresión Zstd aplicada sobre el mismo layout binario estructurado que alimenta a los motores especializados, representando el estándar actual de motores como InfluxDB v3 o TimescaleDB.

### 2.2 Desglose Contable de Componentes (OBS-01)
Para evitar que se atribuya injustamente a Chimp el coste del codec temporal o que se oculten costes de contenedor, se publicará obligatoriamente para cada motor la siguiente contabilidad cuadrupartita:

| Componente | Definición y Contenido |
|---|---|
| **$B_{\text{time}}$** | Bytes consumidos estrictamente por la codificación de la serie de marcas de tiempo. |
| **$B_{\text{values}}$** | Bytes consumidos estrictamente por la codificación de las matrices de valores de sensores. |
| **$B_{\text{container}}$** | Bytes de cabeceras (*headers*), tablas de saltos (*jump tables*), índices de canales y metadatos de sincronización. |
| **$B_{\text{total}}$** | Suma total innegociable: $B_{\text{total}} = B_{\text{time}} + B_{\text{values}} + B_{\text{container}}$. |

### 2.3 Codificación Temporal Canónica: Aritmética Segura, Máquina de Estados y Serialización (NC-04)
El dominio temporal se fija unívocamente como enteros con signo de 64 bits (`int64_t`), representando épocas Unix en **microsegundos** ($\mu\text{s}$) desde `1970-01-01T00:00:00Z` ($[-2^{63}, 2^{63}-1]\,\mu\text{s}$, equivalente a $\pm 292.000$ años).

#### A. Comprobación Segura de Operaciones Aritméticas (Three-Step Safe Arithmetic)
Para evitar desbordamientos en la evaluación de la doble diferencia $D = (t_i - t_{i-1}) - (t_{i-1} - t_{i-2})$, el codificador ejecuta estrictamente:
1. **Paso 1 (Delta Actual):** Comprobar si $\Delta t_i = t_i - t_{i-1}$ desborda `int64_t` (ej. vía `__builtin_sub_overflow`).
2. **Paso 2 (Delta Anterior):** Comprobar si $\Delta t_{i-1} = t_{i-1} - t_{i-2}$ es válido y representable en `int64_t`.
3. **Paso 3 (Resta de Deltas):** Comprobar si $D = \Delta t_i - \Delta t_{i-1}$ desborda `int64_t`.
4. **Paso 4 (Evaluación de Rango):** Solo si los tres pasos precedentes no sufren desbordamiento y $D \in [-2^{31}, 2^{31}-1]$, se evalúa el rango de prefijo estándar de Gorilla. Si cualquiera de los pasos desborda o si $|D| \ge 2^{31}$, se invoca inmediatamente el **Escape de Reinicio Absoluto**.

#### B. Tabla de Codificación y Mapeo de Sesgo (Offset-Binary)
- **Caso $D = 0$:** Prefijo `0` (1 bit).
- **Caso $-63 \le D \le 64$ ($128$ valores representables):**
  - Prefijo: `10` (2 bits).
  - Campo de datos: 7 bits no signados con sesgo (*bias*): $D_7 = (uint8\_t)(D + 63) \in [0, 127]$.
- **Caso $-255 \le D \le 256$ ($512$ valores representables):**
  - Prefijo: `110` (3 bits).
  - Campo de datos: 9 bits no signados con sesgo: $D_9 = (uint16\_t)(D + 255) \in [0, 511]$.
- **Caso $-2047 \le D \le 2048$ ($4096$ valores representables):**
  - Prefijo: `1110` (4 bits).
  - Campo de datos: 12 bits no signados con sesgo: $D_{12} = (uint16\_t)(D + 2047) \in [0, 4095]$.
- **Caso $-2^{31} \le D < 2^{31}$ (Rango de 32 bits):**
  - Prefijo: `11110` (5 bits).
  - Campo de datos: 32 bits en complemento a 2 estándar: $D_{32} = (uint32\_t)((int32\_t)D)$.
- **Escape de Reinicio Absoluto de 64 Bits:**
  - Prefijo: `11111` (5 bits).
  - Campo de datos: 64 bits literales de $t_i$.

#### C. Contrato de Transición de Estados tras el Escape Absoluto
Para garantizar que codificador y decodificador mantengan un estado idéntico y determinista:
1. **Emisión de Escape (Muestra $i$):**
   - El codificador emite el prefijo `11111` seguido de los 64 bits literales de $t_i$.
   - El decodificador lee $t_i$, actualiza `prev_timestamp = t_i` y fija la variable de control:
     $$\text{has\_prev\_delta} = \text{false}$$
2. **Codificación de la Muestra Siguiente (Muestra $i+1$):**
   - Como no existe un delta previo válido, no se puede calcular Delta-of-Delta.
   - Se evalúa la diferencia simple $\Delta t_{i+1} = t_{i+1} - t_i$.
   - Si $\Delta t_{i+1}$ no desborda `int64_t`:
     - Se emite el prefijo de delta inicial `111110` (6 bits) seguido de los 64 bits de $\Delta t_{i+1}$.
     - El decodificador calcula $t_{i+1} = t_i + \Delta t_{i+1}$, fija `prev_delta = \Delta t_{i+1}` y actualiza:
       $$\text{has\_prev\_delta} = \text{true}$$
   - Si $\Delta t_{i+1}$ desborda `int64_t`:
     - La muestra $i+1$ se emite nuevamente como escape absoluto (`11111` + $t_{i+1}$), permaneciendo en $\text{has\_prev\_delta} = \text{false}$.
3. **Reanudación del Régimen Delta-of-Delta (Muestra $i+2$):**
   - Habiéndose establecido `prev_delta` y $\text{has\_prev\_delta} = \text{true}$, la muestra $i+2$ reanuda el cálculo normal de la doble diferencia $D$.

#### D. Serialización y Endianness Bit-Exacto
- **Orden de Bits:** Big-Endian estricto a nivel de bitstream (los bits se transmiten y leen desde el bit más significativo MSB al menos significativo LSB).
- **Representación Entera:** Los timestamps `int64_t` y campos literales de 64 bits se convierten mediante reinterpretación de memoria (`memcpy` a `uint64_t`), preservando intacto el complemento a 2 y el bit de signo para timestamps negativos y valores en los extremos del dominio.

---

## 3. Especificación de Formatos Binarios y Tratamiento de Anomalías

### 3.1 Tipos de Datos y Representación Canónica
- **Marcas de Tiempo ($t_i$):** `int64_t` unificado (Unix epoch en microsegundos, $[-2^{63}, 2^{63}-1]\,\mu\text{s}$).
- **Valores de Sensores ($v_{i,k}$):** 
  - Punto flotante IEEE-754 de precisión simple (32 bits, `float`).
  - Punto flotante IEEE-754 de precisión doble (64 bits, `double`), según el dataset.
- **Orden de Canales:** Determinista e inmutable, indexado $k \in [0, K-1]$.

### 3.2 Manejo Riguroso de Casos Borde y Datos Especiales
Todo motor evaluado en BP-011 deberá procesar y reconstruir idénticamente los siguientes casos borde:

1. **Valores Especiales IEEE-754 (`NaN`, `+Inf`, `-Inf`):**
   - El payload binario exacto de los bits de `NaN` (signo, exponente y mantisa) debe preservarse bit a bit tras la descompresión. Prohibido sustituir `NaN` por ceros o valores por defecto.
2. **Ceros con Signo (`+0.0f` y `-0.0f`):**
   - Deben conservar su bit de signo tras la reconstrucción.
3. **Jitter e Irregularidades Temporales:**
   - Timestamps no perfectamente equiespaciados ($\Delta t_i \ne \text{constante}$). Intervalos con ráfagas (*bursts*, $\Delta t = 0$) y huecos prolongados (*gaps*).
4. **Muestras Duplicadas y Desordenadas:**
   - La secuencia física de entrada debe reproducirse de forma idéntica; el arnés no reordenará muestras sin indicación explícita de protocolo.
5. **Cierre de Bloques Parciales:**
   - Tratamiento de bloques finales que no alcancen la cardinalidad de corte predefinida (ej. bloque de 342 muestras en chunks de 1.000).

---

## 4. Inventario Canónico del Corpus y Reglas de Aplicabilidad

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                        INVENTARIO DEL CORPUS — BP-011                                  │
├─────────┬──────────────────────────────┬────────────┬─────────────┬────────────────────┤
│ ID      │ Denominación                 │ Canales    │ Muestras    │ Estado Actual      │
├─────────┼──────────────────────────────┼────────────┼─────────────┼────────────────────┤
│ DS-01   │ NASA IMS Statistical Features│ 48 Float   │ 2.156 snaps │ DISPONIBLE (CSV)   │
│ DS-02   │ NASA C-MAPSS Turbofan        │ 24 Float   │ 20.631 snaps│ DISPONIBLE (Bin)   │
│ DS-03   │ NASA IMS Raw Vibration 20kHz │ 4/8 Canales│ Alta Frec.  │ PENDIENTE (Fase B) │
│ DS-04   │ Smart Grid / IoT Telemetry   │ Multicanal │ Irregular   │ PENDIENTE (Fase B) │
└─────────┴──────────────────────────────┴────────────┴─────────────┴────────────────────┘
```

### 4.1 Regla Estricta de Aplicabilidad de Consultas por Cardinalidad (NC-03)
Para preservar la integridad del dato real sin distorsiones artificiales:
- **Consulta $Q_1$ (1.000 puntos):** Aplicable exclusivamente si $N \ge 1.000$.
- **Consulta $Q_2$ (10.000 puntos):** Aplicable exclusivamente si $N \ge 10.000$.
- **Consulta $Q_3$ (4 canales):** Aplicable exclusivamente si $K \ge 4$.
- **Tratamiento de Incompatibilidad:** Si un dataset posee cardinalidad insuficiente (como `DS-01` con $N=2.156$), la consulta correspondiente se catalogará formalmente como **`NOT_APPLICABLE` (N/A)**. Queda expresamente prohibido inventar, duplicar, repetir o ciclar registros para alcanzar una dimensión requerida.

---

## 5. Definición Física de Consultas, Caché y Separación de Amplificaciones

### 5.1 Escenarios de Consulta Estandarizados
1. **Consulta $Q_1$ (Ventana Reciente Monocanal - 1.000 puntos):** Recuperación de los últimos 1.000 valores para un canal sensor crítico $C_k$ ($N \ge 1.000$).
2. **Consulta $Q_2$ (Ventana Histórica Monocanal - 10.000 puntos):** Recuperación de un rango temporal continuo de 10.000 puntos en un canal sensor ($N \ge 10.000$).
3. **Consulta $Q_3$ (Proyección Multicanal Selectiva - 4 canales):** Recuperación de 4 sensores simultáneos sobre un intervalo temporal determinado ($K \ge 4$).
4. **Extensión $Q_{\text{conc}}$ (Concurrencia Analítica):** Prueba de estrés con $M$ hilos de consulta concurrentes sobre el mismo chunk comprimido (reservada para Fase posterior; no se certificarán ventajas de concurrencia analítica hasta ejecutar esta prueba).

### 5.2 Regímenes de Memoria y Caché (OBS-03)
Las consultas se evaluarán obligatoriamente bajo dos regímenes operativos disjuntos:
- **Régimen Hot-Cache:** Bloque comprimido pre-cargado en memoria RAM. Aísla y mide el coste algorítmico puro de decodificación, direccionamiento de columnas y transformaciones.
- **Régimen Cold-Cache:** Bloque recuperado desde almacenamiento masivo (I/O controlada, bypass de buffer cache). Mide el coste combinado de transferencia y decodificación.
*Los resultados de ambos regímenes se reportarán en tablas separadas, sin promediar ni mezclar sus mediciones.*

### 5.3 Separación Rigurosa de Métricas de Amplificación (NC-02)
Se establecen tres magnitudes físicas diferenciadas:

1. **Amplificación Física de Lectura ($A_{\text{read}}$):**
   $$A_{\text{read}} = \frac{B_{\text{read}}}{B_{\text{target}}}$$
   *Mide cuántos bytes físicos comprimidos deben leerse del medio o memoria para resolver la consulta.*
2. **Amplificación de Descompresión en Memoria ($A_{\text{decomp}}$):**
   $$A_{\text{decomp}} = \frac{B_{\text{decomp}}}{B_{\text{target}}}$$
   *Mide cuántos bytes de datos descomprime efectivamente el motor hacia buffers de trabajo para entregar el resultado.*
3. **Relación de Almacenamiento ($A_{\text{store}}$):**
   $$A_{\text{store}} = \frac{B_{\text{total}}}{B_{\text{logical}}}$$
   *Representa la densidad de compactación global del bloque.*

**Definición Canónica de $B_{\text{target}}$:**
$$B_{\text{target}} = B_{\text{timestamps\_solicitados}} + B_{\text{values\_solicitados}} + B_{\text{metadatos\_solicitados}}$$
donde:
- $B_{\text{timestamps\_solicitados}} = N_{\text{puntos}} \times \text{sizeof}(t_i)$ (cuando la consulta incluye marcas temporales).
- $B_{\text{values\_solicitados}} = N_{\text{puntos}} \times \sum_{k \in \text{canales\_solicitados}} \text{sizeof}(v_k)$.
- $B_{\text{metadatos\_solicitados}}$ representa la información descriptiva explícitamente requerida por el cliente (ej. identificadores de sensor o flags de calidad).
- **Regla Estricta de Exclusión:** Las cabeceras de bloque, tablas de saltos, índices de compresión y metadatos internos del contenedor que no formen parte de los datos entregados al cliente **quedan formalmente excluidos de $B_{\text{target}}$**, garantizando que las amplificaciones $A_{\text{read}}$ y $A_{\text{decomp}}$ reflejen fielmente el sobrecoste físico respecto a la carga útil entregada.

### 5.4 Huella de Memoria Operativa (OBS-05)
Se medirán con instrumentación explícita:
- **$M_{\text{peak\_alloc}}$:** Pico máximo de asignación dinámica simultánea en heap durante la consulta (high-water mark del allocator).
- **$M_{\text{alloc\_count}}$:** Conteo total de operaciones `malloc`/`calloc` ejecutadas por consulta.
- **$M_{\text{working\_buf}}$:** Tamaño del buffer estático de trabajo requerido.

---

## 6. Métricas Multidimensionales y Definición de Densidad

### 6.1 Desglose Canónico de Densidad de Almacenamiento (OBS-02)
Para erradicar ambigüedades respecto a la sigla `bpc`, se reportarán formalmente cuatro métricas de densidad:

- **$\text{bpv}$ (Bits por Valor):** $\text{bpv} = \frac{B_{\text{values}} \times 8}{N \times K}$ (eficiencia del codec flotante).
- **$\text{bpt}$ (Bits por Timestamp):** $\text{bpt} = \frac{B_{\text{time}} \times 8}{N}$ (eficiencia del codec temporal).
- **$\text{bptuple}$ (Bits por Tupla Completa):** $\text{bptuple} = \frac{B_{\text{total}} \times 8}{N}$ (métrica principal de comparación comercial).
- **$\text{bpc}$ (Bits por Muestra-Canal Integral):** $\text{bpc} = \frac{B_{\text{total}} \times 8}{N \times K}$ (densidad normalizada por celda).

### 6.2 Velocidad y Latencia
- **Throughput de Ingesta/Compresión:** Megabytes por segundo ($\text{MB/s}$) y Millones de puntos por segundo ($\text{Mpts/s}$).
- **Throughput de Descompresión Masiva:** $\text{MB/s}$ y $\text{Mpts/s}$.
- **Latencia de Consulta Selectiva:** Tiempo wall-clock end-to-end en microsegundos ($\mu\text{s}$) o milisegundos ($\text{ms}$) para $Q_1$, $Q_2$ y $Q_3$.

---

## 7. Régimen Estadístico y Fórmulas Rigurosas (Protocolo R15)

### 7.1 Muestreo, Rondas Emparejadas y Aleatorización
- **Régimen Obligatorio:** **1 ciclo de warm-up + 20 iteraciones medidas** por motor, dataset y consulta.
- **Identificación de Parejas Experimentales:**
  Cada punto de comparación se define por la tupla unívoca:
  $$\mathcal{T} = (\text{Dataset}, \text{Bloque}, \text{Consulta}, \text{Ronda}_i) \quad \text{con } i \in [1, 20]$$
  Las mediciones de dos motores comparados se ejecutan sobre el mismo bloque físico y la misma consulta dentro de la misma ronda ambiental, garantizando la naturaleza estrictamente emparejada (*paired samples*).
- **Aleatorización Determinista:** Barajado pseudoaleatorio del orden de ejecución de los motores dentro de cada ronda mediante Fisher-Yates (`seed = 0`), mitigando sesgos de orden o calentamiento térmico.

### 7.2 Fórmulas Estadísticas de Cuartiles de Tukey (NC-01)
Dada una muestra ordenada en orden ascendente:
- **Notación Estadística Canónica (Base 1: $x_{(1)} \le x_{(2)} \le \dots \le x_{(20)}$):**
  $$Q_1 = \frac{x_{(5)} + x_{(6)}}{2}, \qquad Q_3 = \frac{x_{(15)} + x_{(16)}}{2}$$
- **Mapeo a Arreglos de Implementación en C (Base 0: `arr[0]` a `arr[19]`):**
  $$Q_1 = \frac{\text{arr}[4] + \text{arr}[5]}{2}, \qquad Q_3 = \frac{\text{arr}[14] + \text{arr}[15]}{2}$$
- **Rango Intercuartílico e Intervalo de Tukey:**
  $$\text{IQR} = Q_3 - Q_1, \quad \text{Límite Inferior} = Q_1 - 1.5 \cdot \text{IQR}, \quad \text{Límite Superior} = Q_3 + 1.5 \cdot \text{IQR}$$

### 7.3 Dispersión Muestral, Inferencia Emparejada y Orientación del Signo (OBS-04)
- **Media Muestral:** $\bar{x} = \frac{1}{n}\sum_{i=1}^n x_i$
- **Varianza Muestral Insesgada:** $s^2 = \frac{1}{n-1}\sum_{i=1}^n (x_i - \bar{x})^2$
- **Desviación Estándar Muestral:** $s = \sqrt{s^2}$
- **Coeficiente de Variación:** $\text{CV} = \frac{s}{\bar{x}} \times 100\%$
- **Criterios de Estabilidad Obligatorios:**
  - $\text{Outliers} \le 2$ por dirección.
  - $\text{CV}_{\text{raw}} \le 10,0\%$.
  - $\text{CV}_{\text{filt}} \le 5,0\%$.

#### A. Orientación del Signo para la Mejora Relativa ($\Delta_{\text{rel}}$)
Dada la serie de diferencias emparejadas $d_i = x_{A,i} - x_{B,i}$ ($i=1\dots 20$) entre el motor evaluado $A$ y el baseline $B$:
- **Para Métricas donde Menor es Mejor (Tamaño Comprimido, Latencia de Consulta, Amplificación, Memoria):**
  $$\Delta_{\text{rel}} = \frac{\bar{x}_B - \bar{x}_A}{\bar{x}_B} \times 100\% = -\frac{\bar{d}}{\bar{x}_B} \times 100\%$$
  *(Un valor positivo de $\Delta_{\text{rel}}$ indica formalmente una mejora: reducción de tamaño o tiempo).*
- **Para Métricas donde Mayor es Mejor (Throughput de Ingesta, Throughput de Descompresión en MB/s o Mpts/s):**
  $$\Delta_{\text{rel}} = \frac{\bar{x}_A - \bar{x}_B}{\bar{x}_B} \times 100\% = \frac{\bar{d}}{\bar{x}_B} \times 100\%$$
  *(Un valor positivo de $\Delta_{\text{rel}}$ indica formalmente una mejora: incremento de velocidad).*

#### B. Intervalo de Confianza del 95% para la Diferencia Media y la Mejora Relativa
$$\text{SE}_d = \frac{s_d}{\sqrt{20}}, \quad \text{CI}_{95\%}(\bar{d}) = \bar{d} \pm t_{0.975, 19} \cdot \text{SE}_d \quad (t_{0.975, 19} = 2.093)$$
$$\text{CI}_{95\%}(\Delta_{\text{rel}}) = \left[ \frac{\bar{d} - 2.093 \cdot \text{SE}_d}{\bar{x}_B} \times 100\%, \quad \frac{\bar{d} + 2.093 \cdot \text{SE}_d}{\bar{x}_B} \times 100\% \right] \quad \text{(ajustando el signo según la orientación de la métrica)}$$

#### C. Árbol de Selección de Pruebas de Hipótesis Emparejadas
1. **Verificación de Simetría / Normalidad de $d_i$:** Se evalúa la normalidad de la serie de diferencias mediante la prueba de Shapiro-Wilk (o asimetría $|skew| \le 1.0$).
2. **Caso Principal (Distribución Simétrica / Normal):** Se aplica la **Prueba $t$ de Student emparejada** (*paired $t$-test*) sobre $\bar{d}$ con 19 grados de libertad.
3. **Caso con Asimetría Moderada:** Se aplica la **Prueba de Rangos con Signo de Wilcoxon** emparejada.
4. **Caso con Asimetría Severa / Outliers Extremos:** Se aplica la **Prueba de Signos Binomial** (*Sign Test*), inmune a violaciones de simetría distributiva.

#### D. Delimitación de Familias y Control de Multiplicidad (Holm-Bonferroni)
- **Definición de Familia:** Se define formalmente una familia independiente de comparaciones por cada terna:
  $$\mathcal{F} = (\text{Dataset}, \text{Consulta}, \text{Métrica})$$
- **Procedimiento:** Dentro de cada familia $\mathcal{F}$, se contrastan simultáneamente las comparaciones de BinPack frente a los baselines activos ($m=3$: BinPack vs. Gorilla, BinPack vs. Chimp, BinPack vs. Chimp128). Se ordenan los p-valores $p_{(1)} \le p_{(2)} \le p_{(3)}$ y se aplica el criterio escalonado:
  $$p_{(k)} \le \frac{\alpha}{m - k + 1} \quad \text{con } \alpha = 0.05$$
  Diferencias que no superen este umbral ajustado serán catalogadas formalmente como no concluyentes o dentro del ruido experimental.

---

## 8. Criterios Comerciales, Perfiles de Carga y Clasificación Tripartita

### 8.1 Perfiles de Carga Industrial Estandarizados (OBS-05)

1. **Perfil 1 — Ingesta Edge / RTU (Recursos Restringidos):**
   - *Objetivo Operativo Base:* Throughput sostenido $\ge 10\text{ Mpts/s}$, memoria dinámica $\le 4\text{ MB}$, streaming continuo.
   - *Meta Aspiracional:* $50\text{ Mpts/s}$ en hardware de servidor.
2. **Perfil 2 — Historiador SCADA Central (Densidad de Almacenamiento):**
   - *Prioridad:* Mínimo $\text{bptuple}$ en bloques consolidados de tiempo. Descompresión masiva veloz.
3. **Perfil 3 — Motor Analítico TSDB (Consultas Concurrentes y Proyección):**
   - *Prioridad:* Mínima latencia en $Q_1, Q_2, Q_3$ y $A_{\text{decomp}} \approx 1.0$ (proyección columnar directa sin descomprimir canales espurios).

### 8.2 Matriz de Decisión Comercial Tripartita y Rol de Chimp128

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                        CATEGORÍAS DE CLASIFICACIÓN COMERCIAL                           │
├─────────────────────────┬──────────────────────────────────────────────────────────────┤
│ Categoría               │ Criterio de Cumplimiento Objetivo                            │
├─────────────────────────┼──────────────────────────────────────────────────────────────┤
│ 1. Ventaja Concluyente  │ Supera de forma estadísticamente significativa (p < 0.05     │
│                         │ corregido) a los dos baselines canónicos primarios (Gorilla  │
│                         │ y Chimp estándar) en al menos una dimensión clave (ej. >=15% │
│                         │ mejor ratio O >=2x más rápido en consulta) sin penalización  │
│                         │ inaceptable en las demás (>3x más lento o memoria desbordada)│
├─────────────────────────┼──────────────────────────────────────────────────────────────┤
│ 2. Ventaja Especializada│ No domina a todos los motores de forma general, pero exhibe  │
│                         │ superioridad contundente en un perfil de uso concreto        │
│                         │ (ej. acceso columnar selectivo frente a todos los motores,   │
│                         │ o superación de Gorilla/Chimp cediendo solo ante Chimp128    │
│                         │ en series de alta periodicidad por su ring-buffer).          │
├─────────────────────────┼──────────────────────────────────────────────────────────────┤
│ 3. Sin Ventaja Demostrada│ Las diferencias frente a Gorilla/Chimp son estadísticamente   │
│                         │ insignificantes tras corrección de Holm-Bonferroni, o las    │
│                         │ penalizaciones anulan por completo la utilidad comercial.    │
└─────────────────────────┴──────────────────────────────────────────────────────────────┘
```

#### Rol Específico de Chimp128:
Chimp128 se evalúa como una **referencia técnica especializada de alta capacidad** gracias a su buffer circular de 128 valores previos. No se exige batir a Chimp128 para declarar una "Ventaja Concluyente" frente a los estándares de la industria (Gorilla y Chimp), pero si Chimp128 supera a BinPack en ratio en series altamente oscilatorias, dicho hallazgo delimitará con precisión los casos donde BinPack debe incorporar un histórico de referencias para competir.

---

## 9. Próximos Pasos tras la Revisión de la Fase A

Una vez obtenida la aprobación formal de la presente revisión R15.3:
1. **Cierre Formal de la Fase A.**
2. **Apertura de la Fase B (Auditoría de Codecs y Datasets):** Actividad exclusivamente de solo lectura dedicada a la inspección estática de las implementaciones fuente de Gorilla, Chimp/Chimp128 y la validación de integridad de los datasets formalizados.
3. **Mantenimiento del Code Freeze:** Ninguna compilación ni ejecución de código hasta completar las Fases B, C y D.
