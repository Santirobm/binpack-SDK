# Protocolo Experimental R15.1 — Hito BP-011
## Industrial Time-Series Benchmark: Gorilla & Chimp vs. BinPack Transpose

**Proyecto:** BinPack Enterprise SDK  
**Hito:** BP-011  
**Fase:** Fase A — Acta de Apertura y Protocolo Experimental (Revisión R15.1)  
**Documento Rector:** `docs/audit/bp011_experimental_protocol.md`  
**Rama:** `audit/bp1-core-safety`  
**Estado:** PROTOCOLO FORMAL SUBSANADO PARA DICTAMEN DE CIERRE DE FASE A  

---

## Control de Cambios y Subsanaciones (Revisión R15.1)

En atención al Dictamen de Auditoría sobre la versión R15, la presente revisión R15.1 incorpora las siguientes subsanaciones formales:

1. **NC-01 (Fórmulas de Cuartiles de Tukey):** Se desambigua formalmente la notación matemática en base 1 ($x_{(5)}$ y $x_{(6)}$ para $Q_1$; $x_{(15)}$ y $x_{(16)}$ para $Q_3$) y se documenta su mapeo exacto a arreglos en base 0 de C (`arr[4]`/`arr[5]` y `arr[14]`/`arr[15]`).
2. **NC-02 (Separación Rigurosa de Amplificaciones):** Se diferencian conceptual y matemáticamente tres magnitudes: amplificación física de lectura ($A_{\text{read}} = B_{\text{read}} / B_{\text{target}}$), amplificación de descompresión en memoria ($A_{\text{decomp}} = B_{\text{decomp}} / B_{\text{target}}$) y relación de almacenamiento ($A_{\text{store}} = B_{\text{stored}} / B_{\text{logical}}$). Se define $B_{\text{target}}$ incluyendo todos los datos lógicos requeridos.
3. **NC-03 (Regla de Aplicabilidad de Consultas):** Se establece la regla estricta de cardinalidad: $Q_1$ requiere $N \ge 1.000$, $Q_2$ requiere $N \ge 10.000$ y $Q_3$ requiere $K \ge 4$. Datasets con $N < 10.000$ (como DS-01, $N=2.156$) registran formalmente $Q_2$ como `NOT_APPLICABLE` (N/A), prohibiendo la invención, repetición o ciclado de muestras.
4. **NC-04 (Codificación Temporal Canónica de 64 Bits):** Se documenta la estructura completa de escape para timestamps `int64_t`, extendiendo la secuencia prefijo de Gorilla con un fallback explícito de 64 bits para deltas superiores a $2^{31}-1$, garantizando preservación bit-exacta sin truncamiento.
5. **OBS-01 a OBS-06:** Se integra el desglose contable de 4 componentes de tamaño, la diferenciación estricta de densidades (bpv, bpt, bptuple, bpc), la separación de regímenes de caché (Hot-Cache vs. Cold-Cache), las fórmulas de estadística muestral y reglas de inferencia comparativa, la definición de memoria operativa y la delimitación de pruebas concurrentes ($Q_{\text{conc}}$).

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
│ Fase A  │ Acta y Protocolo Experimental R15.1      │ Solo Documentación     │
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
5. **Transición a Fase B:** La Fase B no comenzará hasta que la presente revisión R15.1 cuente con aprobación formal de la auditoría.

---

## 2. Taxonomía de Motores, Composiciones Arquitectónicas y Desglose Contable

Para garantizar paridad estricta, cada motor evaluado cuenta con una definición algorítmica explícita que incluye el manejo integral de marcas de tiempo (*timestamps*), valores y sobrecarga de contenedor (*overhead*).

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                        TAXONOMÍA DE MOTORES DE EVALUACIÓN                               │
├──────────────────────────┬──────────────────────────┬───────────────────────────────────┤
│ Motor                    │ Algoritmo de Tiempo      │ Algoritmo de Valores Flotantes    │
├──────────────────────────┼──────────────────────────┼───────────────────────────────────┤
│ Gorilla_Canonical        │ Delta-of-Delta (VLDB15)  │ XOR flotante con ventana de ceros │
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
   - **Timestamps:** Algoritmo *Delta-of-Delta* extendido para 64 bits (ver Sección 2.3).
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

### 2.3 Codificación Temporal Canónica de 64 Bits y Límites de Rango (NC-04)
El dominio temporal admisible se fija como enteros con signo de 64 bits (`int64_t`), representando épocas Unix en microsegundos o nanosegundos.

Para resolver el límite del campo de 32 bits del paper original de Gorilla ante huecos prolongados ($\Delta t \gg 1\text{ día}$) o desincronizaciones de reloj, se formaliza la siguiente codificación *Delta-of-Delta* extendida sobre $D = (t_i - t_{i-1}) - (t_{i-1} - t_{i-2})$:

- Si $D = 0$: bit `0` (1 bit).
- Si $-63 \le D \le 64$: bits `10` + 7 bits de $D$ (9 bits totales).
- Si $-255 \le D \le 256$: bits `110` + 9 bits de $D$ (12 bits totales).
- Si $-2047 \le D \le 2048$: bits `1110` + 12 bits de $D$ (16 bits totales).
- Si $-2^{31} \le D < 2^{31}$: bits `11110` + 32 bits de $D$ (37 bits totales).
- **Escape de Rango Completo 64-bit:** bits `11111` + 64 bits de $D$ (69 bits totales).

**Garantía de Preservación:** Este mecanismo garantiza que cualquier salto temporal arbitrario de 64 bits se codifique y reconstruya de forma 100% bit-exacta, sin desbordamiento ni truncamiento silencioso.

---

## 3. Especificación de Formatos Binarios y Tratamiento de Anomalías

### 3.1 Tipos de Datos y Representación Canónica
- **Marcas de Tiempo ($t_i$):** `int64_t` (Unix epoch en microsegundos).
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
$$B_{\text{target}} = N_{\text{puntos}} \times \left( \sum_{k \in \text{canales\_solicitados}} \text{sizeof}(v_k) \right) + N_{\text{puntos}} \times \text{sizeof}(t_i)$$
$B_{\text{target}}$ engloba la totalidad de los datos lógicos requeridos para responder íntegramente la consulta.

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

### 7.1 Muestreo y Aleatorización
- **Régimen Obligatorio:** **1 ciclo de warm-up + 20 iteraciones medidas** por motor, dataset y consulta.
- **Aleatorización Determinista:** Barajado pseudoaleatorio de motores por bloque mediante Fisher-Yates con semilla compartida (`seed = 0`).

### 7.2 Fórmulas Estadísticas de Cuartiles de Tukey (NC-01)
Dada una muestra ordenada en orden ascendente:
- **Notación Estadística Canónica (Base 1: $x_{(1)} \le x_{(2)} \le \dots \le x_{(20)}$):**
  $$Q_1 = \frac{x_{(5)} + x_{(6)}}{2}, \qquad Q_3 = \frac{x_{(15)} + x_{(16)}}{2}$$
- **Mapeo a Arreglos de Implementación en C (Base 0: `arr[0]` a `arr[19]`):**
  $$Q_1 = \frac{\text{arr}[4] + \text{arr}[5]}{2}, \qquad Q_3 = \frac{\text{arr}[14] + \text{arr}[15]}{2}$$
- **Rango Intercuartílico e Intervalo de Tukey:**
  $$\text{IQR} = Q_3 - Q_1, \quad \text{Límite Inferior} = Q_1 - 1.5 \cdot \text{IQR}, \quad \text{Límite Superior} = Q_3 + 1.5 \cdot \text{IQR}$$

### 7.3 Dispersión Muestral e Inferencia Comparativa (OBS-04)
- **Media Muestral:** $\bar{x} = \frac{1}{n}\sum_{i=1}^n x_i$
- **Varianza Muestral Insesgada:** $s^2 = \frac{1}{n-1}\sum_{i=1}^n (x_i - \bar{x})^2$
- **Desviación Estándar Muestral:** $s = \sqrt{s^2}$
- **Coeficiente de Variación:** $\text{CV} = \frac{s}{\bar{x}} \times 100\%$
- **Criterios de Estabilidad Obligatorios:**
  - $\text{Outliers} \le 2$ por dirección.
  - $\text{CV}_{\text{raw}} \le 10,0\%$.
  - $\text{CV}_{\text{filt}} \le 5,0\%$.
- **Regla de Inferencia para Declarar Ventaja:**
  Para declarar formalmente una ventaja entre el motor $A$ y el motor $B$ ($\Delta = \bar{x}_A - \bar{x}_B$), se exigirá que los intervalos de confianza del 95% ($t$-Student con 19 grados de libertad, $t_{0.975} = 2.093$) no se solapen, o que la prueba $t$ de Welch arroje $p < 0.01$. Diferencias solapadas serán clasificadas como no reproducibles o dentro del ruido.

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

### 8.2 Matriz de Decisión Comercial Tripartita

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                        CATEGORÍAS DE CLASIFICACIÓN COMERCIAL                           │
├─────────────────────────┬──────────────────────────────────────────────────────────────┤
│ Categoría               │ Criterio de Cumplimiento Objetivo                            │
├─────────────────────────┼──────────────────────────────────────────────────────────────┤
│ 1. Ventaja Concluyente  │ Supera a Gorilla y Chimp en al menos una dimensión clave     │
│                         │ (ej. >=15% mejor ratio O >=2x más rápido en consulta) sin     │
│                         │ penalizaciones inaceptables en las demás (>3x más lento      │
│                         │ o memoria desbordada).                                       │
├─────────────────────────┼──────────────────────────────────────────────────────────────┤
│ 2. Ventaja Especializada│ No domina de forma general, pero exhibe superioridad         │
│                         │ contundente y reproducible en un perfil de uso concreto      │
│                         │ (ej. acceso columnar selectivo en TSDB analítica).           │
├─────────────────────────┼──────────────────────────────────────────────────────────────┤
│ 3. Sin Ventaja Demostrada│ Las diferencias frente a Gorilla/Chimp son estadísticamente   │
│                         │ insignificantes (dentro del ruido experimental), o las      │
│                         │ penalizaciones anulan por completo la utilidad comercial.    │
└─────────────────────────┴──────────────────────────────────────────────────────────────┘
```

---

## 9. Próximos Pasos tras la Revisión de la Fase A

Una vez obtenida la aprobación formal de la presente revisión R15.1:
1. **Cierre Formal de la Fase A.**
2. **Apertura de la Fase B (Auditoría de Codecs y Datasets):** Actividad exclusivamente de solo lectura dedicada a la inspección estática de las implementaciones fuente de Gorilla, Chimp/Chimp128 y la validación de integridad de los datasets formalizados.
3. **Mantenimiento del Code Freeze:** Ninguna compilación ni ejecución de código hasta completar las Fases B, C y D.
