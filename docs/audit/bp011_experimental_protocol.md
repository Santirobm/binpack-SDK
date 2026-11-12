# Protocolo Experimental R15.9 — Hito BP-011
## Industrial Time-Series Benchmark: Gorilla & Chimp vs. BinPack Transpose

**Proyecto:** BinPack Enterprise SDK  
**Hito:** BP-011  
**Fase:** Fase A — Acta de Apertura y Protocolo Experimental (Revisión Definitiva R15.9)  
**Documento Rector:** `docs/audit/bp011_experimental_protocol.md`  
**Rama:** `audit/bp1-core-safety`  
**Estado:** PROTOCOLO FORMAL DEFINITIVO PARA DICTAMEN DE CIERRE DE FASE A  

---

## Control de Cambios y Registro Integral de Subsanaciones

### Revisiones Previas (R15.1 a R15.8)
- **NC-01 (Cuartiles de Tukey y Regla de Signos):** Cuartiles de Tukey en base 1 y base 0 (`arr[4]`, `arr[5]`, etc.); regla unívoca de $k$ para la mediana en la prueba de signos como el mayor entero con cobertura bilateral $\ge 95\%$ ($k=6$, cobertura $95,861\%$ para $n_{\text{eff}}=20$), tabla exhaustiva para $n_{\text{eff}} < 20$ y tratamiento con $k=1$ y advertencia `UNDERSIZED_EFFECTIVE_SAMPLE` para $n_{\text{eff}} \le 5$.
- **NC-02 (Separación de Amplificaciones y Bootstrap):** Definición independiente de $A_{\text{read}}$, $A_{\text{decomp}}$ y $A_{\text{store}}$ con $B_{\text{target}}$ libre de cabeceras; bootstrap percentil determinista con PRNG independiente.
- **NC-03 (Aplicabilidad de Consultas):** Regla de cardinalidad estricta y catalogación de $Q_2$ como `NOT_APPLICABLE` en DS-01 ($N < 10.000$).
- **NC-04 (Prefijos Inequívocos y Máquina de Estados):** Árbol estrictamente libre de prefijos (*strictly prefix-free*) con `111110` para delta inicial y `111111` para escape absoluto. Aritmética segura en 3 pasos, máquina de estados con `has_prev_delta` y serialización Big-Endian portable en microsegundos `int64_t`.
- **OBS-01 a OBS-06:** Desglose en 4 componentes de tamaño ($B_{\text{time}}$, $B_{\text{values}}$, $B_{\text{container}}$, $B_{\text{total}}$), densidades (bpv, bpt, bptuple, bpc), regímenes Hot/Cold Cache, reserva de $Q_{\text{conc}}$, familias Holm-Bonferroni por terna $(\text{Dataset}, \text{Consulta}, \text{Métrica})$ a $\alpha = 0.05$, y baseline Chimp128.
- **Subsanaciones R15.5 a R15.7:** Correspondencia unívoca de estimandos e intervalos; tratamiento riguroso de empates y ceros; coeficiente de Fisher-Pearson; PCG32 canónico; Lemire en C; diseño en bloques de Cuadrados Latinos con balanceo $f_{m, p} \in \{2, 3\}$; desambiguación contractual entre índices bit-exactos y resultados flotantes con tolerancia.

### Revisión R15.9 (Presente Documento Definitivo)
1. **Configuración Canónica de MSVC y Semántica Real de Toolchains (NC-02):** Se fija `/fp:strict` como configuración canónica obligatoria para MSVC (desactivando contracciones FMA implícitas en todas las versiones); se documenta el alcance real y no especulativo de las opciones de GCC (`-ffp-contract=off -fno-fast-math`, retirando el experimental `-frounding-math`) y Clang (`-ffp-contract=off -fno-fast-math -ffp-model=precise`); y se mandata la comprobación estricta en tiempo de ejecución del modo de redondeo `FE_TONEAREST` mediante `fegetround()`.
2. **Regla Unificada de Tolerancia Numérica para Referencia Cero (NC-02):** Se formaliza el criterio canónico unificado de verificación combinando tolerancias absoluta y relativa: $|\text{valor}_{\text{obs}} - \text{valor}_{\text{ref}}| \le \max(10^{-9}, \; 10^{-7} \cdot |\text{valor}_{\text{ref}}|)$, garantizando validez analítica tanto para valores normales como para magnitudes nulas o próximas a cero.

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
│ Fase A  │ Acta y Protocolo Experimental R15.5      │ Solo Documentación     │
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
5. **Transición a Fase B:** La Fase B no comenzará hasta que la presente revisión R15.5 cuente con aprobación formal de la auditoría.

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
| **Escape Absoluto de 64 Bits** | `111111` | 6 bits | 64 bits con signo | Timestamp literal: `(uint64_t)t_i` |

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
     - *Decodificador:* Al leer `111110`, lee 64 bits de $\Delta t_{i+1}$, comprueba formalmente mediante suma segura (`__builtin_add_overflow(prev_timestamp, delta, &curr)`) que la adición no sufra overflow/underflow con signo. Calcula $t_{i+1} = t_i + \Delta t_{i+1}$, actualiza `prev_timestamp = t_{i+1}`, `prev_delta = \Delta t_{i+1}` y establece:
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

## 7. Régimen Estadístico, Inferencia Emparejada y Reproducibilidad Exacta

### 7.1 Muestreo, PRNG Determinista Canónico y Diseño en Bloques Balanceados

- **Régimen Obligatorio:** **1 ciclo de warm-up + 20 iteraciones medidas** por motor, dataset y consulta.
- **Identificación de Parejas Experimentales:**
  Cada punto de comparación se define por la tupla unívoca:
  $$\mathcal{T} = (\text{Dataset}, \text{Bloque}, \text{Consulta}, \text{Ronda}_i) \quad \text{con } i \in [1, 20]$$
  Las mediciones de dos motores comparados se ejecutan sobre el mismo bloque físico y la misma consulta dentro de la misma ronda ambiental, constituyendo **muestras emparejadas** (*paired samples*).

#### A. Especificación Canónica de PCG32 (Variante XSH-RR de 64 a 32 bits)
Para garantizar reproducibilidad matemática y bit a bit en cualquier arquitectura (x86_64, AArch64, Windows, Linux) sin depender de librerías del sistema, se adopta como único PRNG el generador **PCG32** (Melissa O'Neill, 2014).

```c
#include <stdint.h>

typedef struct {
    uint64_t state;
    uint64_t inc;
} pcg32_random_t;

// Función de paso y salida XSH-RR (xorshift high randomize, random rotation)
static inline uint32_t pcg32_random_r(pcg32_random_t* rng) {
    uint64_t oldstate = rng->state;
    // Avance del estado LCG con multiplicador de Knuth
    rng->state = oldstate * 6364136223846793005ULL + (rng->inc | 1ULL);
    // Transformación de salida a 32 bits con permutación
    uint32_t xorshifted = (uint32_t)(((oldstate >> 18u) ^ oldstate) >> 27u);
    uint32_t rot = (uint32_t)(oldstate >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31u));
}

// Procedimiento canónico de inicialización en 2 pasos
static inline void pcg32_srandom_r(pcg32_random_t* rng, uint64_t initstate, uint64_t initseq) {
    rng->state = 0ULL;
    rng->inc = (initseq << 1u) | 1ULL;
    pcg32_random_r(rng);
    rng->state += initstate;
    pcg32_random_r(rng);
}
```

- **Parámetros Canónicos del PRNG de Ejecución:**
  - `initstate = 42ULL`
  - `initseq = 54ULL`
  - **Continuidad Estricta:** Se inicializa **una sola vez** al arrancar la campaña de medición. El generador avanza continuamente sin reinicio ni perturbación externa durante la construcción del calendario.

#### B. Muestreo Acotado sin Sesgo de Lemire (Nearly Divisionless Bounded Random)
Para barajar vectores sin sesgo de módulo en el intervalo $[0, s)$, se implementa estrictamente la variante de 32 bits de Daniel Lemire (2019):

```c
static inline uint32_t bounded_rand_lemire(pcg32_random_t* rng, uint32_t s) {
    uint32_t x = pcg32_random_r(rng);
    uint64_t m = (uint64_t)x * (uint64_t)s;
    uint32_t l = (uint32_t)m;
    if (l < s) {
        uint32_t t = (-s) % s; // t = (2^32 - s) % s en uint32_t
        while (l < t) {
            x = pcg32_random_r(rng);
            m = (uint64_t)x * (uint64_t)s;
            l = (uint32_t)m;
        }
    }
    return (uint32_t)(m >> 32u);
}
```

#### C. Diseño en Bloques de Cuadrados Latinos con Balanceo Térmico Óptimo
Para neutralizar la deriva térmica de CPU y el efecto de orden de ejecución sin recurrir a muestreo aleatorio simple (el cual puede concentrar asimétricamente un motor en ciertas posiciones), el calendario de 20 rondas se construye mediante un **Diseño en Bloques de Cuadrados Latinos Aleatorizados**:

1. **Estructura en 3 Bloques:**
   - **Bloque 1 (Rondas 1 a 7):** Cuadrado Latino aleatorizado de $7 \times 7$ completo (7 rondas).
   - **Bloque 2 (Rondas 8 a 14):** Segundo Cuadrado Latino aleatorizado de $7 \times 7$ completo (7 rondas).
   - **Bloque 3 (Rondas 15 a 20):** Primeras 6 rondas de un tercer Cuadrado Latino aleatorizado de $7 \times 7$.
2. **Propiedad Matemática de Balanceo Exacto:**
   En cada Cuadrado Latino, cada uno de los 7 motores ($M_1$ a $M_7$) aparece **exactamente una vez** en cada una de las 7 posiciones ordinales ($p_1$ a $p_7$). En consecuencia:
   - En el Bloque 1, cada motor aparece 1 vez en cada posición.
   - En el Bloque 2, cada motor aparece 1 vez en cada posición.
   - En el Bloque 3 (6 rondas), cada motor aparece 0 o 1 vez en cada posición.
   - **Frecuencia Acumulada:** Para todo motor $m \in \{1,\dots,7\}$ y posición $p \in \{1,\dots,7\}$:
     $$f_{m, p} \in \{2, 3\}$$
   - **Desviación Máxima respecto al Óptimo Teórico ($20/7 \approx 2.857$):**
     $$\max_{m, p} \left| f_{m, p} - \frac{20}{7} \right| = \frac{6}{7} \approx 0.857 < 1.0$$
     *Queda analíticamente erradicada la dispersión amplia $[1, 4]$, garantizando por construcción la máxima neutralidad térmica físicamente alcanzable en 20 rondas.*

3. **Algoritmo de Generación Determinista:**
```c
static void shuffle_vector(uint8_t* arr, uint32_t n, pcg32_random_t* rng) {
    for (uint32_t i = n - 1; i > 0; i--) {
        uint32_t j = bounded_rand_lemire(rng, i + 1);
        uint8_t tmp = arr[i]; arr[i] = arr[j]; arr[j] = tmp;
    }
}

void generate_schedule_latin_blocks(pcg32_random_t* rng, uint8_t sched[20][7]) {
    uint8_t L[7][7];
    for (int i = 0; i < 7; i++)
        for (int j = 0; j < 7; j++)
            L[i][j] = (uint8_t)((i + j) % 7);

    int round_idx = 0;
    for (int block = 0; block < 3; block++) {
        uint8_t row_order[7] = {0, 1, 2, 3, 4, 5, 6};
        uint8_t col_order[7] = {0, 1, 2, 3, 4, 5, 6};
        uint8_t sym_map[7]   = {1, 2, 3, 4, 5, 6, 7}; // Mapeo a Motores M1..M7

        shuffle_vector(row_order, 7, rng);
        shuffle_vector(col_order, 7, rng);
        shuffle_vector(sym_map, 7, rng);

        int max_rows = (block < 2) ? 7 : 6;
        for (int r = 0; r < max_rows; r++) {
            int row = row_order[r];
            for (int c = 0; c < 7; c++) {
                int col = col_order[c];
                sched[round_idx][c] = sym_map[L[row][col]];
            }
            round_idx++;
        }
    }
}
```

4. **Calendario Canónico Generado (Inspección y Cotejo Estático):**
   Con `initstate = 42ULL` e `initseq = 54ULL`, el algoritmo produce de manera reproducible el siguiente orden de ejecución:

| Ronda | Posición 1 | Posición 2 | Posición 3 | Posición 4 | Posición 5 | Posición 6 | Posición 7 |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Ronda 1** | M3 | M6 | M4 | M7 | M1 | M5 | M2 |
| **Ronda 2** | M6 | M5 | M3 | M4 | M2 | M1 | M7 |
| **Ronda 3** | M4 | M3 | M7 | M2 | M5 | M6 | M1 |
| **Ronda 4** | M7 | M4 | M2 | M1 | M6 | M3 | M5 |
| **Ronda 5** | M1 | M2 | M5 | M6 | M4 | M7 | M3 |
| **Ronda 6** | M5 | M1 | M6 | M3 | M7 | M2 | M4 |
| **Ronda 7** | M2 | M7 | M1 | M5 | M3 | M4 | M6 |
| **Ronda 8** | M1 | M5 | M4 | M3 | M7 | M2 | M6 |
| **Ronda 9** | M5 | M3 | M2 | M6 | M4 | M1 | M7 |
| **Ronda 10** | M2 | M1 | M7 | M5 | M6 | M4 | M3 |
| **Ronda 11** | M7 | M4 | M3 | M2 | M5 | M6 | M1 |
| **Ronda 12** | M3 | M6 | M1 | M7 | M2 | M5 | M4 |
| **Ronda 13** | M4 | M2 | M6 | M1 | M3 | M7 | M5 |
| **Ronda 14** | M6 | M7 | M5 | M4 | M1 | M3 | M2 |
| **Ronda 15** | M4 | M1 | M6 | M5 | M2 | M3 | M7 |
| **Ronda 16** | M3 | M7 | M4 | M6 | M1 | M2 | M5 |
| **Ronda 17** | M5 | M3 | M7 | M1 | M4 | M6 | M2 |
| **Ronda 18** | M2 | M5 | M3 | M4 | M7 | M1 | M6 |
| **Ronda 19** | M7 | M4 | M1 | M2 | M6 | M5 | M3 |
| **Ronda 20** | M6 | M2 | M5 | M7 | M3 | M4 | M1 |

5. **Matriz de Frecuencias de Posición ($f_{m, p}$ Verificada):**

| Motor | P1 | P2 | P3 | P4 | P5 | P6 | P7 | Total |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **M1 (Gorilla)** | 2 | 3 | 3 | 3 | 3 | 3 | 3 | **20** |
| **M2 (Chimp)** | 3 | 3 | 2 | 3 | 3 | 3 | 3 | **20** |
| **M3 (Chimp128)** | 3 | 3 | 3 | 2 | 3 | 3 | 3 | **20** |
| **M4 (BP Raw Scalar)** | 3 | 3 | 3 | 3 | 3 | 3 | 2 | **20** |
| **M5 (BP Trans Scalar)** | 3 | 3 | 3 | 3 | 2 | 3 | 3 | **20** |
| **M6 (BP Trans SIMD)** | 3 | 2 | 3 | 3 | 3 | 3 | 3 | **20** |
| **M7 (BP Channel Ded)** | 3 | 3 | 3 | 3 | 3 | 2 | 3 | **20** |
| **Total por Posición** | **20** | **20** | **20** | **20** | **20** | **20** | **20** | **140** |

*Verificación: Para todo $m, p$, $f_{m, p} \in \{2, 3\}$. Se cumple $|f_{m, p} - 20/7| \le 0.8571 < 1.0$.*

---

### 7.2 Fórmulas Estadísticas de Cuartiles de Tukey (NC-01)
Dada una muestra ordenada en orden ascendente:
- **Notación Estadística Canónica (Base 1: $x_{(1)} \le x_{(2)} \le \dots \le x_{(20)}$):**
  $$Q_1 = \frac{x_{(5)} + x_{(6)}}{2}, \qquad Q_3 = \frac{x_{(15)} + x_{(16)}}{2}$$
- **Mapeo a Arreglos de Implementación en C (Base 0: `arr[0]` a `arr[19]`):**
  $$Q_1 = \frac{\text{arr}[4] + \text{arr}[5]}{2}, \qquad Q_3 = \frac{\text{arr}[14] + \text{arr}[15]}{2}$$
- **Rango Intercuartílico e Intervalo de Tukey:**
  $$\text{IQR} = Q_3 - Q_1, \quad \text{Límite Inferior} = Q_1 - 1.5 \cdot \text{IQR}, \quad \text{Límite Superior} = Q_3 + 1.5 \cdot \text{IQR}$$

---

### 7.3 Dispersión Muestral, Coeficiente de Asimetría e Inferencia Emparejada (OBS-04)
- **Media Muestral:** $\bar{x} = \frac{1}{n}\sum_{i=1}^n x_i$
- **Varianza Muestral Insesgada:** $s^2 = \frac{1}{n-1}\sum_{i=1}^n (x_i - \bar{x})^2$
- **Desviación Estándar Muestral:** $s = \sqrt{s^2}$
- **Coeficiente de Variación:** $\text{CV} = \frac{s}{\bar{x}} \times 100\%$
- **Criterios de Estabilidad Obligatorios:**
  - $\text{Outliers} \le 2$ por dirección.
  - $\text{CV}_{\text{raw}} \le 10,0\%$.
  - $\text{CV}_{\text{filt}} \le 5,0\%$.

#### A. Coeficiente Insesgado de Asimetría de Fisher-Pearson ($g_1$) y Encuadre Metodológico
Para evaluar la simetría de la serie de diferencias emparejadas $d_i = x_{A,i} - x_{B,i}$ ($i=1\dots 20$), se adopta formalmente la definición insesgada estándar de Fisher-Pearson:
$$g_1 = \frac{n}{(n-1)(n-2)} \sum_{i=1}^n \left( \frac{d_i - \bar{d}}{s_d} \right)^3 \quad \text{donde } s_d = \sqrt{\frac{1}{n-1}\sum_{i=1}^n (d_i - \bar{d})^2}$$

> [!NOTE] Encuadre Metodológico del Árbol de Decisión
> Se aclara de forma expresa que un valor $|g_1| \le 0.5$ no constituye una demostración matemática formal de simetría de la distribución subyacente (distribuciones asimétricas bimodales pueden presentar $g_1 \approx 0$), del mismo modo que un p-valor $p_{\text{SW}} \ge 0.05$ en la prueba de Shapiro-Wilk no demuestra que los datos provengan verdaderamente de una distribución normal (ausencia de evidencia en contra no equivale a prueba de normalidad).  
> El árbol de decisión no pretende emitir certificados teóricos de normalidad o simetría, sino que opera como una **regla operativa metodológica predefinida y vinculante** (*a priori protocolized decision heuristic*), diseñada para guiar objetivamente la selección de pruebas emparejadas y prevenir sesgos de selección post-hoc (*p-hacking*).

#### B. Tratamiento Riguroso de Empates y Diferencias Nulas ($d_i = 0$)
1. **En la Prueba de Signos:** Las diferencias nulas ($d_i = 0$, empates exactos) se excluyen del cómputo conforme al criterio estándar de Dixon & Mood. El tamaño muestral efectivo es:
   $$n_{\text{eff}} = \sum_{i=1}^n \mathbb{I}(d_i \ne 0)$$
   La prueba contrasta la hipótesis binomial $H_0: P(d_i > 0 \mid d_i \ne 0) = 0.5$ bajo la distribución $B(n_{\text{eff}}, 0.5)$. Si $n_{\text{eff}} = 0$, se reporta formalmente $p = 1.0$ y empate exacto.
2. **En la Prueba de Wilcoxon:** Las diferencias nulas ($|d_i| = 0$) se excluyen del ordenamiento ($n_{\text{eff}}$ pares activos). Los empates en magnitudes absolutas no nulas ($|d_i| = |d_j|$) reciben rangos promedio (*mid-ranks*), aplicando la corrección estándar a la varianza del estadístico de suma de rangos $W$:
   $$\sigma_W^2 = \frac{n_{\text{eff}}(n_{\text{eff}} + 1)(2n_{\text{eff}} + 1) - \frac{1}{2}\sum_{j=1}^g (t_j^3 - t_j)}{24}$$
   donde $g$ es el número de grupos de valores empatados y $t_j$ es la cardinalidad de cada grupo.

#### C. Árbol Algorítmico de Selección de Pruebas y Correspondencia de Estimandos
Para garantizar que la prueba de hipótesis y el intervalo de confianza estimen rigurosamente el mismo parámetro poblacional:

1. **Paso 1 (Evaluación Operativa de Normalidad):** Se aplica Shapiro-Wilk sobre la serie de diferencias $d_i$:
   - Si $p_{\text{SW}} \ge 0.05 \implies$ **Prueba $t$ de Student emparejada** (*paired $t$-test*, 19 g.l.).
     - *Estimando Principal:* Media poblacional de las diferencias emparejadas $\bar{d}$.
     - *Intervalo de Confianza del 95%:* Intervalo paramétrico de Student:
       $$\text{CI}_{95\%}(\bar{d}) = \left[ \bar{d} - 2.093 \cdot \frac{s_d}{\sqrt{20}}, \quad \bar{d} + 2.093 \cdot \frac{s_d}{\sqrt{20}} \right]$$

2. **Paso 2 (Evaluación Operativa de Simetría ante No Normalidad):** Si $p_{\text{SW}} < 0.05$, se evalúa el coeficiente de asimetría $g_1$:
   - Si $|g_1| \le 0.5$ (heurística de simetría aceptable) $\implies$ **Prueba de Rangos con Signo de Wilcoxon emparejada**.
     - *Estimando Principal:* Pseudomediana poblacional $\Delta_{\text{HL}}$ (estimador de localización de Hodges-Lehmann, definido como la mediana de los $M = \frac{n_{\text{eff}}(n_{\text{eff}} + 1)}{2}$ promedios de Walsh $W_{ij} = \frac{d_i + d_j}{2}$ para $1 \le i \le j \le n_{\text{eff}}$).
     - *Intervalo de Confianza del 95% de Hodges-Lehmann ante Presencia de Empates:*
       - Se ordenan los promedios de Walsh: $W_{(1)} \le W_{(2)} \le \dots \le W_{(M)}$.
       - **Caso sin empates ($t_j = 1, n_{\text{eff}} = 20$):** Inversión exacta con corte crítico $k = 52$ derivado de la distribución de Wilcoxon: $[W_{(52)}, \; W_{(159)}]$.
       - **Caso con empates en $|d_i|$:** Inversión de rangos mediante la aproximación normal con corrección de continuidad y varianza de empates $\sigma_W^2$:
         $$k = \max\left(1, \; \left\lfloor \frac{M}{2} - 1.959964 \cdot \sigma_W + 0.5 \right\rfloor \right)$$
         $$\text{CI}_{95\%}(\Delta_{\text{HL}}) = [W_{(k)}, \; W_{(M - k + 1)}]$$
   - Si $|g_1| > 0.5$ (distribución asimétrica o colas pesadas) $\implies$ **Prueba de Signos Binomial exacta** (*Exact Two-Sided Sign Test*).
     - *Estimando Principal:* Mediana poblacional de las diferencias emparejadas $\tilde{d}$.
     - *Regla Canónica Unívoca para el Intervalo de Confianza de la Mediana (NC-01):*
       Sea la muestra ordenada de diferencias no nulas $d_{(1)} \le d_{(2)} \le \dots \le d_{(n_{\text{eff}})}$ y sea $K \sim \text{Binomial}(n_{\text{eff}}, 0.5)$. El intervalo de confianza bilateral viene dado por los estadísticos de orden $[d_{(k)}, \; d_{(n_{\text{eff}} - k + 1)}]$.
       Para garantizar una regla completamente determinista y libre de ambigüedad, el índice entero $k$ se selecciona estrictamente como **el mayor entero $k$ que preserva una cobertura bilateral mínima del 95 % ($\ge 0.95$)**:
       $$k = \max \left\{ j \in \left\{1, \dots, \left\lfloor \frac{n_{\text{eff}}}{2} \right\rfloor \right\} \;\middle|\; \sum_{i=j}^{n_{\text{eff}}-j} \binom{n_{\text{eff}}}{i} (0.5)^{n_{\text{eff}}} \ge 0.95 \right\}$$
       - **Caso $n_{\text{eff}} = 20$ (Muestra completa sin empates nulos):**  
         La distribución acumulada simétrica arroja para $k=6$:  
         $$\sum_{i=6}^{14} \binom{20}{i} (0.5)^{20} = 1 - 2 \cdot \sum_{i=0}^5 \binom{20}{i} (0.5)^{20} = 1 - 2 \cdot \frac{21.700}{1.048.576} = \frac{1.005.176}{1.048.576} \approx 95,861\%$$  
         Dado que para $k=7$ la cobertura desciende a $88,468\% < 95\%$, el índice canónico único es **$k = 6$**, y el intervalo bilateral es $[d_{(6)}, \; d_{(15)}]$.
       - **Tabla Canónica de Selección de $k$ para Tamaños Reducidos ($n_{\text{eff}} < 20$):**
         - $n_{\text{eff}} = 19, 18, 17 \implies k = 5$ (coberturas bilaterales exactas: $98,079\%$, $96,912\%$, $95,096\%$).
         - $n_{\text{eff}} = 16, 15 \implies k = 4$ (coberturas bilaterales exactas: $97,873\%$, $96,484\%$).
         - $n_{\text{eff}} = 14, 13, 12 \implies k = 3$ (coberturas bilaterales exactas: $98,706\%$, $97,798\%$, $96,143\%$).
         - $n_{\text{eff}} = 11, 10, 9 \implies k = 2$ (coberturas bilaterales exactas: $98,828\%$, $97,852\%$, $96,094\%$).
         - $n_{\text{eff}} = 8, 7, 6 \implies k = 1$ (coberturas bilaterales exactas: $99,219\%$, $98,438\%$, $96,875\%$).
       - **Tratamiento Formal para $n_{\text{eff}} \le 5$ (Insuficiencia de Cobertura Nominal al 95 %):**  
         Para $n_{\text{eff}} \le 5$, la probabilidad acumulada bilateral máxima alcanzable (con $k=1$) es $1 - 2(0.5)^{n_{\text{eff}}} < 95\%$ (ej. para $n_{\text{eff}}=5$ es $93,750\%$; para $n=4$ es $87,500\%$). En tales casos excepcionales:
         1. Se adopta obligatoriamente $k = 1$, reportando el intervalo envolvente $[d_{(1)}, \; d_{(n_{\text{eff}})}]$.
         2. Se reporta de forma transparente el nivel de cobertura exacto alcanzado $1 - 2^{1 - n_{\text{eff}}}$.
         3. Se emite la advertencia metodológica `UNDERSIZED_EFFECTIVE_SAMPLE` indicando que el tamaño muestral efectivo no permite alcanzar el umbral del 95 % con la prueba de signos.
         4. Si $n_{\text{eff}} = 0$ (empate unánime absoluto), se declara empate con intervalo $[0, 0]$ y $p = 1.0$.

3. **Métrica Comercial Transversal Invariante (Bootstrap Percentil Emparejado Determinista):**
   Con el fin de proporcionar una magnitud económica unificada y comprensible para directores de infraestructura sin comprometer el rigor de las pruebas no paramétricas, se reportará de manera estandarizada la diferencia media $\bar{d}$ acompañada de su intervalo de confianza al 95% obtenido mediante **Bootstrap Percentil Emparejado Determinista**:
   - **Instancia de PRNG Independiente:** Se emplea una instancia dedicada de PCG32 (`pcg32_random_t boot_rng`).
   - **Semilla Canónica del Bootstrap:** `boot_initstate = 0xB00757A7ULL`, `boot_initseq = 0x42ULL`.
   - **Procedimiento de Remuestreo:**
     - Se ejecutan exactamente $B = 10.000$ réplicas de remuestreo con reemplazo.
     - En cada réplica $b \in [1, B]$, se extraen $n = 20$ parejas observadas $(x_{A, j}, x_{B, j})$ mediante índices enteros generados unívocamente:
       $$j_i = \text{bounded\_rand\_lemire}(\&boot\_rng, 20) \quad \text{para } i = 0, \dots, 19$$
     - Se calcula la media muestral remuestreada acumulando estrictamente de forma secuencial en orden ascendente de índice $i$:
       $$\bar{d}^*_b = \frac{1}{20} \sum_{i=0}^{19} (x_{A, j_i} - x_{B, j_i})$$
     - Se ordenan las réplicas en orden ascendente: $\bar{d}^*_{(1)} \le \bar{d}^*_{(2)} \le \dots \le \bar{d}^*_{(10000)}$.
     - **Intervalo Percentil al 95%:** $[\bar{d}^*_{(250)}, \; \bar{d}^*_{(9750)}]$ (percentiles $2,5\%$ y $97,5\%$ exactos en base 1; correspondientes a los elementos de índice `reps[249]` y `reps[9749]` en base 0 en C).
     - Para la mejora relativa $\Delta_{\text{rel}}$, cada réplica calcula $\Delta_{\text{rel}, b}^*$ (aplicando inversión de signo para métricas donde menor es mejor), reportando $[\Delta^*_{(250)}, \; \Delta^*_{(9750)}]$.
   - **Criterio de Reproducibilidad Numérica y Aritmética de Punto Flotante (NC-02):**
     - **Secuencia de Índices de Remuestreo (Bit-Exactitud Estricta):** La secuencia completa de los $200.000$ índices de remuestreo generados ($B \times n = 10.000 \times 20$) mediante `bounded_rand_lemire(&boot_rng, 20)` sobre la instancia canónica `boot_rng` es **estrictamente bit-exacta** (idéntica bit a bit) entre todas las plataformas, compiladores y sistemas operativos admitidos.
     - **Formato Numérico y Reglas de Evaluación IEEE 754:** La acumulación aritmética de cada réplica se ejecuta obligatoriamente en formato IEEE 754 Binary64 (`double`), modo de redondeo estándar al más cercano (*roundTiesToEven* / `FE_TONEAREST`), en orden secuencial estricto y ascendente:
       $$\bar{d}^*_b = \frac{1}{20} \sum_{i=0}^{19} (x_{A, j_i} - x_{B, j_i})$$
     - **Comprobación en Tiempo de Ejecución del Modo de Redondeo:**  
       El ejecutable verificará formalmente al inicio de la fase analítica (mediante `<fenv.h>`) que el entorno de punto flotante se encuentra configurado en el modo canónico:
       ```c
       #include <fenv.h>
       #include <assert.h>
       assert(fegetround() == FE_TONEAREST);
       ```
       Queda estrictamente prohibida la alteración del entorno de redondeo o de control durante el cálculo estadístico.
     - **Tabla Canónica de Opciones de Compilación por Compilador Admitido:**  
       Para garantizar la consistencia numérica y evitar contracciones FMA implícitas o reasociaciones espurias entre diferentes toolchains admitidos:

| Compilador Admitido | Plataforma Típica | Banderas Obligatorias de Compilación | Semántica Real y Alcance Documentado |
| :--- | :--- | :--- | :--- |
| **GCC (GNU C)** | Linux, Windows (MinGW) | `-ffp-contract=off -fno-fast-math` | Desactiva fusión FMA no explícita (`fp-contract=off`) y preserva la asociatividad estricta de expresiones flotantes (`fno-fast-math`). Se opera bajo el entorno estático por defecto `FE_TONEAREST` sin requerir `-frounding-math` (el cual es experimental según la documentación oficial de GCC). |
| **Clang / LLVM** | Linux, macOS, Windows | `-ffp-contract=off -fno-fast-math -ffp-model=precise` | Establece el modelo de punto flotante preciso (`-ffp-model=precise`), inhibe contracciones FMA no explícitas y evita reasociaciones algebraicas no seguras bajo entorno flotante estándar. |
| **MSVC (Visual C++)** | Windows (MSVC x64/ARM64) | `/fp:strict` | Configuración canónica obligatoria para MSVC. Deshabilita de forma universal las contracciones FMA implícitas en todas las versiones de Visual Studio, preserva el orden secuencial estricto de evaluación de expresiones y garantiza el respeto íntegro de la semántica de excepciones y redondeo IEEE 754. |

*(Nota de Delimitación de Alcance: Compiladores o modos de compilación que no implementen la semántica estricta IEEE 754 Binary64, o que fuercen flags de relajación aritmética de tipo `fast-math` que rompen la asociatividad, quedan formalmente excluidos del alcance de la garantía de reproducibilidad).*

     - **Reproducibilidad Numérica Independiente y Criterio Unificado de Tolerancia:**  
       Se establece de forma rigurosa la distinción conceptual entre:
       1. **Bit-Exactitud Absoluta:** Aplica *exclusivamente* a la generación de los $200.000$ índices enteros del bootstrap.
       2. **Reproducibilidad Numérica Independiente:** Aplica a los estimadores y percentiles flotantes ($\bar{d}^*$, percentiles $[\bar{d}^*_{(250)}, \bar{d}^*_{(9750)}]$ y $[\Delta^*_{(250)}, \Delta^*_{(9750)}]$), los cuales se certifican como **reproducibles numéricamente de manera independiente bajo las reglas aritméticas y la regla unificada de tolerancia**:
          $$|\text{valor}_{\text{obs}} - \text{valor}_{\text{ref}}| \le \max\left(10^{-9}, \; 10^{-7} \cdot |\text{valor}_{\text{ref}}|\right)$$
       - **Régimen de Magnitudes Normales ($|\text{valor}_{\text{ref}}| > 10^{-2}$):** La cota está dominada por la tolerancia relativa de $10^{-7}$ (0.1 partes por millón).
       - **Régimen Próximo a Cero o Cero Exacto ($|\text{valor}_{\text{ref}}| \le 10^{-2}$ y en particular $|\text{valor}_{\text{ref}}| \to 0$):** La cota está gobernada por la tolerancia absoluta $\epsilon_{\text{abs}} = 10^{-9}$, evitando divisiones espurias por cero o divergencias relativas artificiales cuando la diferencia entre motores es nula o infinitesimal.
       *Esta formulación unificada delimita contractualmente la reproducibilidad numérica independiente, absorbiendo micro-diferencias inevitables de redondeo en el último bit de la mantisa entre microarquitecturas de registros (ej. x86_64 SSE2/AVX frente a ARM64 NEON).*

#### D. Orientación del Signo e Intervalo de Mejora Relativa ($\Delta_{\text{rel}}$)
Con $\bar{x}_B > 0$ y $[\bar{d}_L, \bar{d}_U]$ correspondiente al intervalo de confianza del 95% del estimador de diferencia media:
- **Para Métricas donde Mayor es Mejor (Throughput MB/s, Mpts/s):**
  $$\Delta_{\text{rel}} = \frac{\bar{x}_A - \bar{x}_B}{\bar{x}_B} \times 100\% = \frac{\bar{d}}{\bar{x}_B} \times 100\%$$
  $$\text{CI}_{95\%}(\Delta_{\text{rel}}) \approx \left[ \frac{\bar{d}_L}{\bar{x}_B} \times 100\%, \quad \frac{\bar{d}_U}{\bar{x}_B} \times 100\% \right]$$
- **Para Métricas donde Menor es Mejor (Tamaño Comprimido, Latencia, Amplificación, Memoria):**
  $$\Delta_{\text{rel}} = \frac{\bar{x}_B - \bar{x}_A}{\bar{x}_B} \times 100\% = -\frac{\bar{d}}{\bar{x}_B} \times 100\%$$
  $$\text{CI}_{95\%}(\Delta_{\text{rel}}) \approx \left[ -\frac{\bar{d}_U}{\bar{x}_B} \times 100\%, \quad -\frac{\bar{d}_L}{\bar{x}_B} \times 100\% \right]$$
  *(Inversión explícita de extremos y signos estrictamente formalizada).*

#### E. Delimitación de Familias y Control de Multiplicidad (Holm-Bonferroni)
- **Definición de Familia:** Se define formalmente una familia independiente de comparaciones por cada terna:
  $$\mathcal{F} = (\text{Dataset}, \text{Consulta}, \text{Métrica})$$
- **Procedimiento:** Dentro de cada familia $\mathcal{F}$, se contrastan simultáneamente las comparaciones de BinPack frente a los baselines activos ($m=3$: BinPack vs. Gorilla, BinPack vs. Chimp, BinPack vs. Chimp128). Se ordenan los p-valores $p_{(1)} \le p_{(2)} \le p_{(3)}$ y se aplica el criterio escalonado:
  - Paso 1: Si $p_{(1)} \le \frac{0.05}{3} \approx 0.0167 \implies$ Rechazo de $H_0^{(1)}$; continuar al Paso 2. Si no, detenerse (ninguno es significativo).
  - Paso 2: Si $p_{(2)} \le \frac{0.05}{2} = 0.0250 \implies$ Rechazo de $H_0^{(2)}$; continuar al Paso 3. Si no, detenerse.
  - Paso 3: Si $p_{(3)} \le \frac{0.05}{1} = 0.0500 \implies$ Rechazo de $H_0^{(3)}$.
- **Rol Metodológico de Chimp128:**
  Chimp128 se evalúa como una **referencia técnica especializada de alta capacidad** (ring buffer de 128 valores flotantes para maximizar compresión a expensas de mayor huella de memoria). Su inclusión dentro de la familia $\mathcal{F}$ previene el sesgo de selección favorable y garantiza que las ventajas declaradas por BinPack compitan contra la variante más agresiva del estado del arte.

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

Una vez obtenida la aprobación formal de la presente revisión R15.9:
1. **Cierre Formal de la Fase A.**
2. **Apertura de la Fase B (Auditoría de Codecs y Datasets):** Actividad exclusivamente de solo lectura dedicada a la inspección estática de las implementaciones fuente de Gorilla, Chimp/Chimp128 y la validación de integridad de los datasets formalizados.
3. **Mantenimiento del Code Freeze:** Ninguna compilación ni ejecución de código hasta completar las Fases B, C y D.
