# Protocolo Experimental R15.4 — Hito BP-011
## Industrial Time-Series Benchmark: Gorilla & Chimp vs. BinPack Transpose

**Proyecto:** BinPack Enterprise SDK  
**Hito:** BP-011  
**Fase:** Fase A — Acta de Apertura y Protocolo Experimental (Revisión Definitiva R15.4)  
**Documento Rector:** `docs/audit/bp011_experimental_protocol.md`  
**Rama:** `audit/bp1-core-safety`  
**Estado:** PROTOCOLO FORMAL DEFINITIVO PARA DICTAMEN DE CIERRE DE FASE A  

---

## Control de Cambios y Registro Integral de Subsanaciones

### Revisiones Previas (R15.1 a R15.3)
- **NC-01 (Cuartiles de Tukey):** Desambiguación formal entre notación estadística en base 1 ($x_{(5)}, x_{(6)}$ y $x_{(15)}, x_{(16)}$) y mapeo en base 0 en C (`arr[4]`, `arr[5]` y `arr[14]`, `arr[15]`).
- **NC-02 (Separación de Amplificaciones):** Definición independiente de $A_{\text{read}}$ (lectura física), $A_{\text{decomp}}$ (descompresión en memoria) y $A_{\text{store}}$ (relación de almacenamiento).
- **NC-03 (Aplicabilidad de Consultas):** Regla de cardinalidad estricta y catalogación de $Q_2$ como `NOT_APPLICABLE` en DS-01 ($N < 10.000$).
- **OBS-01 a OBS-06:** Desglose en 4 componentes de tamaño ($B_{\text{time}}$, $B_{\text{values}}$, $B_{\text{container}}$, $B_{\text{total}}$), desglose cuádruple de densidades (bpv, bpt, bptuple, bpc), separación de regímenes Hot-Cache vs. Cold-Cache, definición de memoria operativa y reserva de $Q_{\text{conc}}$.

### Revisión R15.4 (Presente Documento Definitivo)
1. **NC-04 (Erradicación de Colisión de Prefijos en el Árbol de Códigos):** Se rediseña la tabla de prefijos para garantizar que sea estrictamente libre de prefijos (*prefix-free*). Se asigna `111110` (6 bits) al delta inicial de 64 bits y `111111` (6 bits) al escape absoluto de 64 bits, eliminando la colisión con `11110` (delta de 32 bits) y la ambigüedad en el decodificador.
2. **Contrato de Transición y Decodificación Segura:** Se define la máquina de estados completa de codificador y decodificador con la bandera `has_prev_delta`, incluyendo la comprobación segura de adición `prev_timestamp + delta` en decodificación.
3. **Comprobación Aritmética de 3 Pasos:** Verificación estricta de ausencia de desbordamiento en $\Delta t_i$, $\Delta t_{i-1}$ y $D = \Delta t_i - \Delta t_{i-1}$ antes de computar Delta-of-Delta.
4. **Serialización Big-Endian y Portabilidad de `int64_t`:** Representación física bit-exacta mediante reinterpretación de memoria (`memcpy` a `uint64_t`), preservando valores negativos y extremos $[-2^{63}, 2^{63}-1]\,\mu\text{s}$.
5. **OBS-04 (Árbol Formal de Decisión Estadística y Transformación de Intervalos):** Se establece la regla algorítmica de selección: Shapiro-Wilk $\implies$ prueba $t$ emparejada (si $p \ge 0.05$); si $p < 0.05$, evaluación de asimetría $g_1$: Wilcoxon emparejada si $|g_1| \le 0.5$, o Prueba de Signos Binomial exacta si $|g_1| > 0.5$. Se especifica la inversión estricta de extremos para el intervalo de confianza del 95% en métricas donde menor es mejor.
6. **Balanceo de Orden y PRNG Continuo:** Flujo PRNG continuo único (sin reinicio de semilla entre rondas) acoplado a un diseño en bloques balanceados para neutralizar sesgos de deriva térmica.
7. **Familias Holm-Bonferroni y Rol de Chimp128:** Delimitación de familias por terna $(\text{Dataset}, \text{Consulta}, \text{Métrica})$ a $\alpha = 0.05$, y fijación de Chimp128 como baseline especializado.

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
│ Fase A  │ Acta y Protocolo Experimental R15.4      │ Solo Documentación     │
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
5. **Transición a Fase B:** La Fase B no comenzará hasta que la presente revisión R15.4 cuente con aprobación formal de la auditoría.

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

### 2.3 Codificación Temporal Canónica: Aritmética Segura, Tabla Libre de Prefijos y Serialización (NC-04)
El dominio temporal se fija unívocamente como enteros con signo de 64 bits (`int64_t`), representando épocas Unix en **microsegundos** ($\mu\text{s}$) desde `1970-01-01T00:00:00Z` ($[-2^{63}, 2^{63}-1]\,\mu\text{s}$, equivalente a $\pm 292.000$ años).

#### A. Comprobación Segura de Operaciones Aritméticas (Three-Step Safe Check)
Para evitar desbordamientos en la evaluación de la doble diferencia $D = (t_i - t_{i-1}) - (t_{i-1} - t_{i-2})$, el codificador ejecuta estrictamente:
1. **Paso 1 (Delta Actual):** Comprobar si $\Delta t_i = t_i - t_{i-1}$ desborda `int64_t` (ej. vía `__builtin_sub_overflow(t_i, t_prev, &delta_curr)`).
2. **Paso 2 (Delta Anterior):** Comprobar si $\Delta t_{i-1} = t_{i-1} - t_{i-2}$ es válido y representable en `int64_t`.
3. **Paso 3 (Resta de Deltas):** Comprobar si $D = \Delta t_i - \Delta t_{i-1}$ desborda `int64_t` (ej. vía `__builtin_sub_overflow(delta_curr, delta_prev, &D)`).
4. **Paso 4 (Evaluación de Rango):** Solo si los tres pasos precedentes no sufren desbordamiento y $D \in [-2^{31}, 2^{31}-1]$, se evalúa el rango de prefijo estándar de Gorilla. Si cualquiera de los pasos desborda o si $|D| \ge 2^{31}$, se invoca inmediatamente el **Escape de Reinicio Absoluto**.

#### B. Tabla de Códigos de Prefijo Inequívoca (Strictly Prefix-Free Tree)
Para erradicar colisiones entre prefijos de longitud variable, todos los códigos constituyen nodos hoja en el árbol de decodificación:

```
               (Raíz)
              /      \
            [0]       [1]
          (D = 0)    /   \
                  [10]   [11]
               (7 bits)  /   \
                      [110]  [111]
                   (9 bits)  /   \
                          [1110] [1111]
                       (12 bits) /    \
                              [11110] [11111]
                             (32 bits)/      \
                                  [111110]  [111111]
                                 (Delta 64) (Escape Abs 64)
```

| Operación / Rango de $D$ | Prefijo Binario | Longitud | Campo de Datos Asociado | Codificación de Datos |
|---|:---:|:---:|---|---|
| **$D = 0$** | `0` | 1 bit | *Ninguno* | *Sin datos adicionales* |
| **$-63 \le D \le 64$** | `10` | 2 bits | 7 bits no signados | Sesgo (*bias*): $D_7 = (uint8\_t)(D + 63) \in [0, 127]$ |
| **$-255 \le D \le 256$** | `110` | 3 bits | 9 bits no signados | Sesgo (*bias*): $D_9 = (uint16\_t)(D + 255) \in [0, 511]$ |
| **$-2047 \le D \le 2048$** | `1110` | 4 bits | 12 bits no signados | Sesgo (*bias*): $D_{12} = (uint16\_t)(D + 2047) \in [0, 4095]$ |
| **$-2^{31} \le D < 2^{31}$** | `11110` | 5 bits | 32 bits con signo | Complemento a 2: $D_{32} = (uint32\_t)((int32\_t)D)$ |
| **Delta Inicial de 64 Bits** | `111110` | 6 bits | 64 bits con signo | Complemento a 2: `(uint64_t)delta_init` |
| **Escape Absoluto de 64 Bits** | `111111` | 6 bits | 64 bits con signo | Timestamp absoluto literal: `(uint64_t)t_i` |

**Demostración de No Colisión:** Ningún prefijo de la tabla es prefijo de ningún otro. El decodificador lee bit a bit sin requerir *lookahead* ni incurrir en ambigüedad.

#### C. Contrato Simétrico de Máquina de Estados para Codificador y Decodificador
1. **Emisión y Recepción de Escape Absoluto (Muestra $i$):**
   - *Codificador:* Emite el prefijo `111111` (6 bits) seguido de los 64 bits de $t_i$.
   - *Decodificador:* Al leer `111111`, lee 64 bits de $t_i$, actualiza `prev_timestamp = t_i` y establece:
     $$\text{has\_prev\_delta} = \text{false}$$
2. **Transición en la Muestra Subsiguiente (Muestra $i+1$):**
   - Al estar $\text{has\_prev\_delta} = \text{false}$, se evalúa la diferencia simple $\Delta t_{i+1} = t_{i+1} - t_i$.
   - *Caso A (Delta representable sin desbordamiento en `int64_t`):*
     - *Codificador:* Emite `111110` (6 bits) seguido de los 64 bits de $\Delta t_{i+1}$.
     - *Decodificador:* Al leer `111110`, lee 64 bits de $\Delta t_{i+1}$, comprueba mediante suma segura (`__builtin_add_overflow(prev_timestamp, delta, &curr)`) que no exista desbordamiento, calcula $t_{i+1} = t_i + \Delta t_{i+1}$, actualiza `prev_timestamp = t_{i+1}`, `prev_delta = \Delta t_{i+1}` y establece:
       $$\text{has\_prev\_delta} = \text{true}$$
   - *Caso B (Diferencia simple desborda `int64_t`):*
     - *Codificador:* Emite nuevamente el escape absoluto `111111` seguido de los 64 bits de $t_{i+1}$.
     - *Decodificador:* Lee `111111`, actualiza `prev_timestamp = t_{i+1}` y permanece en $\text{has\_prev\_delta} = \text{false}$.
3. **Régimen Delta-of-Delta Pleno (Muestra $i+2$ en adelante):**
   - Con $\text{has\_prev\_delta} = \text{true}$, el codificador evalúa $D = \Delta t_{i+2} - \Delta t_{i+1}$ bajo las comprobaciones aritméticas seguras de la Sección 2.3.A.
   - El decodificador lee el prefijo correspondiente, decodifica $D$, calcula con suma segura `new_delta = prev_delta + D` y `curr_timestamp = prev_timestamp + new_delta`, y actualiza su estado. Si lee el prefijo `111111`, transiciona de inmediato a $\text{has\_prev\_delta} = \text{false}$.

#### D. Serialización y Endianness Bit-Exacto
- **Orden de Bits:** Big-Endian estricto a nivel de bitstream (los bits se transmiten y leen desde el bit más significativo MSB al menos significativo LSB, bit 63 al bit 0).
- **Representación Entera:** Los timestamps `int64_t` y campos literales de 64 bits se convierten mediante reinterpretación de memoria (`memcpy` a `uint64_t`), preservando intacto el complemento a 2 y el bit de signo para timestamps negativos y valores en los extremos del dominio ($[-2^{63}, 2^{63}-1]\,\mu\text{s}$).

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

### 7.1 Muestreo, Diseño en Bloques Balanceados y PRNG Continuo
- **Régimen Obligatorio:** **1 ciclo de warm-up + 20 iteraciones medidas** por motor, dataset y consulta.
- **Identificación de Parejas Experimentales:**
  Cada punto de comparación se define por la tupla unívoca:
  $$\mathcal{T} = (\text{Dataset}, \text{Bloque}, \text{Consulta}, \text{Ronda}_i) \quad \text{con } i \in [1, 20]$$
  Las mediciones de dos motores comparados se ejecutan sobre el mismo bloque físico y la misma consulta dentro de la misma ronda ambiental, garantizando la naturaleza estrictamente emparejada (*paired samples*).
- **Flujo PRNG Continuo y Diseño en Bloques Balanceados:**
  Para evitar que la semilla repetida genere órdenes idénticos o que un motor resulte sistemáticamente beneficiado por calentamiento o deriva térmica:
  1. La semilla fija (`seed = 0`) inicializa un generador pseudoaleatorio de alta calidad (PCG32 / xoshiro256**) una única vez al comienzo de la campaña, avanzando continuamente sin re-sembrarse entre rondas.
  2. Se aplica una estrategia de aleatorización en bloques balanceados (*Balanced Block Randomization*), garantizando que cada motor ocupe cada posición ordinal (1º a 7º) con idéntica frecuencia esperada a lo largo de las 20 rondas.

### 7.2 Fórmulas Estadísticas de Cuartiles de Tukey (NC-01)
Dada una muestra ordenada en orden ascendente:
- **Notación Estadística Canónica (Base 1: $x_{(1)} \le x_{(2)} \le \dots \le x_{(20)}$):**
  $$Q_1 = \frac{x_{(5)} + x_{(6)}}{2}, \qquad Q_3 = \frac{x_{(15)} + x_{(16)}}{2}$$
- **Mapeo a Arreglos de Implementación en C (Base 0: `arr[0]` a `arr[19]`):**
  $$Q_1 = \frac{\text{arr}[4] + \text{arr}[5]}{2}, \qquad Q_3 = \frac{\text{arr}[14] + \text{arr}[15]}{2}$$
- **Rango Intercuartílico e Intervalo de Tukey:**
  $$\text{IQR} = Q_3 - Q_1, \quad \text{Límite Inferior} = Q_1 - 1.5 \cdot \text{IQR}, \quad \text{Límite Superior} = Q_3 + 1.5 \cdot \text{IQR}$$

### 7.3 Dispersión Muestral, Inferencia Emparejada y Regla de Decisión (OBS-04)
- **Media Muestral:** $\bar{x} = \frac{1}{n}\sum_{i=1}^n x_i$
- **Varianza Muestral Insesgada:** $s^2 = \frac{1}{n-1}\sum_{i=1}^n (x_i - \bar{x})^2$
- **Desviación Estándar Muestral:** $s = \sqrt{s^2}$
- **Coeficiente de Variación:** $\text{CV} = \frac{s}{\bar{x}} \times 100\%$
- **Criterios de Estabilidad Obligatorios:**
  - $\text{Outliers} \le 2$ por dirección.
  - $\text{CV}_{\text{raw}} \le 10,0\%$.
  - $\text{CV}_{\text{filt}} \le 5,0\%$.

#### A. Orientación Estricta del Signo para la Mejora Relativa ($\Delta_{\text{rel}}$)
Dada la serie de diferencias emparejadas $d_i = x_{A,i} - x_{B,i}$ ($i=1\dots 20$) entre el motor evaluado $A$ y el baseline $B$:
- **Para Métricas donde Menor es Mejor (Tamaño Comprimido, Latencia de Consulta, Amplificación, Memoria):**
  $$\Delta_{\text{rel}} = \frac{\bar{x}_B - \bar{x}_A}{\bar{x}_B} \times 100\% = -\frac{\bar{d}}{\bar{x}_B} \times 100\%$$
  *(Un valor positivo de $\Delta_{\text{rel}}$ indica formalmente una mejora: reducción porcentual de tamaño o tiempo).*
- **Para Métricas donde Mayor es Mejor (Throughput de Ingesta, Throughput de Descompresión en MB/s o Mpts/s):**
  $$\Delta_{\text{rel}} = \frac{\bar{x}_A - \bar{x}_B}{\bar{x}_B} \times 100\% = \frac{\bar{d}}{\bar{x}_B} \times 100\%$$
  *(Un valor positivo de $\Delta_{\text{rel}}$ indica formalmente una mejora: incremento porcentual de velocidad).*

#### B. Intervalo de Confianza del 95% para la Diferencia Media y Transformación de Extremos
Sea $\text{SE}_d = \frac{s_d}{\sqrt{20}}$ y $t_{\text{crit}} = t_{0.975, 19} = 2.093$. El intervalo de confianza del 95% para la diferencia media $\bar{d}$ es:
$$[\bar{d}_L, \bar{d}_U] = [\bar{d} - 2.093 \cdot \text{SE}_d, \; \bar{d} + 2.093 \cdot \text{SE}_d]$$

La estimación del intervalo de confianza para la mejora relativa $\Delta_{\text{rel}}$ se formula como una aproximación lineal de primer orden (Taylor plug-in conditioned on $\bar{x}_B > 0$), especificando rigurosamente la inversión de extremos según la orientación de la métrica:
- **Para Métricas donde Mayor es Mejor (Throughput):**
  $$\text{CI}_{95\%}(\Delta_{\text{rel}}) \approx \left[ \frac{\bar{d}_L}{\bar{x}_B} \times 100\%, \quad \frac{\bar{d}_U}{\bar{x}_B} \times 100\% \right]$$
- **Para Métricas donde Menor es Mejor (Tamaño, Latencia, Memoria):**
  $$\text{CI}_{95\%}(\Delta_{\text{rel}}) \approx \left[ -\frac{\bar{d}_U}{\bar{x}_B} \times 100\%, \quad -\frac{\bar{d}_L}{\bar{x}_B} \times 100\% \right]$$
  *(Nótese la inversión obligatoria de los extremos inferior y superior impuesta por el cambio de signo).*

#### C. Árbol Algorítmico de Selección de Pruebas de Hipótesis Emparejadas
Para garantizar la validez inferencial sin violar supuestos distributivos:
1. **Paso 1 (Evaluación de Normalidad):** Se aplica la prueba de Shapiro-Wilk sobre la serie de diferencias $d_i$:
   - Si $p_{\text{SW}} \ge 0.05$: Las diferencias son consistentes con la normalidad $\implies$ Se aplica la **Prueba $t$ de Student emparejada** (*paired $t$-test*) sobre $\bar{d}$ con 19 grados de libertad.
2. **Paso 2 (Evaluación de Simetría ante No Normalidad):** Si $p_{\text{SW}} < 0.05$, se calcula el coeficiente muestral de asimetría (*skewness*):
   $$g_1 = \frac{\frac{1}{n}\sum_{i=1}^n (d_i - \bar{d})^3}{s_d^3}$$
   - Si $|g_1| \le 0.5$ (distribución aproximadamente simétrica): Se satisface el supuesto de localización de Wilcoxon $\implies$ Se aplica la **Prueba de Rangos con Signo de Wilcoxon emparejada**.
   - Si $|g_1| > 0.5$ (distribución marcadamente asimétrica o colas pesadas): Se invalida el supuesto de simetría de Wilcoxon $\implies$ Se aplica la **Prueba de Signos Binomial exacta** (*Exact Two-Sided Sign Test* sobre $H_0: P(d_i > 0) = 0.5$), libre de cualquier supuesto de forma distributiva.

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
Chimp128 se evalúa como una **referencia técnica especializada de alta capacidad** gracias a su buffer circular de 128 valores previos. No se exige batir a Chimp128 para declarar una "Ventaja Concluyente" frente a los estándares de la industria (Gorilla y Chimp estándar), pero si Chimp128 supera a BinPack en ratio en series altamente oscilatorias, dicho hallazgo delimitará con precisión los casos donde BinPack debe incorporar un histórico de referencias para competir.

---

## 9. Próximos Pasos tras la Revisión de la Fase A

Una vez obtenida la aprobación formal de la presente revisión R15.4:
1. **Cierre Formal de la Fase A.**
2. **Apertura de la Fase B (Auditoría de Codecs y Datasets):** Actividad exclusivamente de solo lectura dedicada a la inspección estática de las implementaciones fuente de Gorilla, Chimp/Chimp128 y la validación de integridad de los datasets formalizados.
3. **Mantenimiento del Code Freeze:** Ninguna compilación ni ejecución de código hasta completar las Fases B, C y D.
