# Protocolo Experimental R15 — Hito BP-011
## Industrial Time-Series Benchmark: Gorilla & Chimp vs. BinPack Transpose

**Proyecto:** BinPack Enterprise SDK  
**Hito:** BP-011  
**Fase:** Fase A — Acta de Apertura y Protocolo Experimental  
**Documento Rector:** `docs/audit/bp011_experimental_protocol.md`  
**Rama:** `audit/bp1-core-safety`  
**Estado:** PROPUESTA DE PROTOCOLO FORMAL PARA REVISIÓN DE AUDITORÍA  

---

## 1. Mandato, Gobernanza y Pregunta de Negocio

### 1.1 Pregunta Estratégica Central
El hito previo (BP-010) acreditó que BinPack es un compresor generalista robusto, pero no demostró ventajas decisivas frente a herramientas consolidadas (Zstd, LZ4). El hito BP-011 abandona la pretensión de universalidad para investigar una hipótesis específica de alto valor técnico y comercial:

> **¿Ofrece BinPack una ventaja competitiva medible y reproducible frente a codecs especializados en series temporales (Gorilla, Chimp/Chimp128) y estándares de la industria (Zstandard), en escenarios de telemetría industrial donde conviven la eficiencia de almacenamiento y la agilidad de consulta selectiva?**

El propósito no es forzar una conclusión favorable para BinPack, sino identificar de forma objetiva su verdadero perfil de aptitud:
- Si BinPack supera a los competidores en almacenamiento o consulta, se fundamenta una propuesta de valor comercial sólida para bases de datos de series temporales (TSDB) e historiadores industriales.
- Si Gorilla o Chimp demuestran superioridad integral en almacenamiento y latencia, se obtiene una delimitación técnica clara que evitará desviar recursos comerciales hacia casos de uso donde el motor no es competitivo.

### 1.2 Estructura de Gobernanza por Fases
Para salvaguardar la integridad metodológica, BP-011 se rige por seis fases secuenciales estrictas:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                       GOBERNANZA POR FASES — BP-011                         │
├─────────┬──────────────────────────────────────────┬────────────────────────┤
│ Fase A  │ Acta y Protocolo Experimental R15        │ Solo Documentación     │
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

---

## 2. Taxonomía de Motores y Composiciones Arquitectónicas

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
   - **Timestamps:** Algoritmo *Delta-of-Delta* con codificación de longitud variable sobre $D = (t_i - t_{i-1}) - (t_{i-1} - t_{i-2})$:
     - Si $D = 0$: `0` (1 bit).
     - Si $-63 \le D \le 64$: `10` + 7 bits.
     - Si $-255 \le D \le 256$: `110` + 9 bits.
     - Si $-2047 \le D \le 2048$: `1110` + 12 bits.
     - En otro caso: `1111` + 32 bits de $D$.
   - **Valores:** XOR bitwise con seguimiento de ceros a la izquierda (*leading zeros*) y a la derecha (*trailing zeros*).

2. **`Chimp_Composed` (VLDB 2022):**
   - **Valores:** Implementación canónica de Chimp con codificación optimizada de ceros a la izquierda (3 bits de longitud) y detección de ceros a la derecha.
   - **Timestamps:** Dado que Chimp es un codec de coma flotante puro y no define un estándar de timestamps, se acopla a la rutina canónica *Delta-of-Delta* de Gorilla. Se identifica formalmente como arquitectura compuesta para evitar distorsiones de autoría.

3. **`Chimp128_Composed` (VLDB 2022):**
   - **Valores:** Chimp con buffer circular (*ring-buffer*) de 128 valores previos para maximizar la probabilidad de coincidencias XOR en series con oscilaciones periódicas.
   - **Timestamps:** Acoplado idénticamente a *Delta-of-Delta*.

4. **`BinPack_Transpose_Multichannel`:**
   - **Estructura:** Disposición columnar del bloque multicanal: vector temporal $T$ seguido de matrices de canales $C_0, C_1, \dots, C_{K-1}$.
   - **Transformación:** Pre-filtro de transposición estructural de bytes aplicado por canal, agrupando bytes de signo/exponente y mantisa para optimizar la compresión entrópica mediante BinPack Tier 2 / Tier 1.

5. **`BinPack_Chunked_PerChannel`:**
   - **Estructura:** Bloques independientes por canal sensor, conteniendo cada uno su copia o referencia de la base temporal correspondiente.
   - **Propósito:** Evaluar el coste de compresión y la ventaja en lectura cuando las consultas TSDB solicitan proyecciones de una sola columna.

6. **`Zstandard_Structured` (Niveles 1 y 3):**
   - **Línea Base Generalista:** Compresión Zstd aplicada sobre el mismo layout binario estructurado que alimenta a los motores especializados, representando el estándar actual de motores como InfluxDB v3 o TimescaleDB.

### 2.2 Principio de Paridad y Contabilidad de Sobrecarga
- **Cero Exclusión:** El tamaño comprimido oficial de cada motor incluirá obligatoriamente todos los bytes de cabecera (*headers*), tablas de saltos (*jump tables*), índices de bloque y metadatos de sincronización.
- **Transparencia Arquitectónica:** No se atribuirá a Chimp penalizaciones derivadas del codec temporal común, registrándose por separado el desglose de bytes de tiempo y bytes de valor.

---

## 3. Especificación de Formatos Binarios y Tratamiento de Anomalías

### 3.1 Tipos de Datos y Representación Canónica
- **Marcas de Tiempo ($t_i$):** Enteros con signo de 64 bits (`int64_t`), representando épocas Unix en milisegundos o microsegundos.
- **Valores de Sensores ($v_{i,k}$):** 
  - Punto flotante IEEE-754 de precisión simple (32 bits, `float`).
  - Punto flotante IEEE-754 de precisión doble (64 bits, `double`), según el dataset.
- **Orden de Canales:** Fijo y determinista, indexado $k \in [0, K-1]$.

### 3.2 Manejo Riguroso de Casos Borde y Datos Especiales
Todo motor evaluado en BP-011 deberá procesar y reconstruir idénticamente los siguientes casos borde sin abortos ni corrupción:

1. **Valores Especiales IEEE-754 (`NaN`, `+Inf`, `-Inf`):**
   - El payload binario exacto de los bits de `NaN` (signo, exponente y mantisa) debe preservarse bit a bit tras la descompresión.
   - Prohibido sustituir `NaN` por ceros o valores por defecto.
2. **Ceros con Signo (`+0.0f` y `-0.0f`):**
   - Deben conservar su bit de signo tras la reconstrucción.
3. **Jitter e Irregularidades Temporales:**
   - Timestamps no perfectamente equiespaciados ($\Delta t_i \ne \text{constante}$).
   - Intervalos con ráfagas (*bursts*, $\Delta t = 0$) y huecos prolongados (*gaps*).
4. **Muestras Duplicadas y Desordenadas:**
   - Si el dataset de origen presenta timestamps idénticos o inversiones leves de orden temporal, el arnés debe reproducir exactamente la secuencia física de entrada.
5. **Cierre de Bloques Parciales:**
   - Tratamiento de bloques finales que no alcancen la cardinalidad de corte predefinida (ej. bloque de 342 muestras en chunks de 1.000).

---

## 4. Inventario Canónico del Corpus de Series Temporales

El corpus de evaluación se clasifica rigurosamente distinguiendo datos disponibles en el repositorio de aquellos sujetos a formalización o adquisición en fases posteriores:

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

### 4.1 Datasets Disponibles para Evaluación Inicial

1. **`DS-01`: NASA IMS Bearing Statistical Features (`nasa_bearing_ims_test1.csv`):**
   - **Origen:** Ensayo de fatiga acelerada de rodamientos del Center for Intelligent Maintenance Systems (IMS), Universidad de Cincinnati.
   - **Naturaleza:** 48 descriptores estadísticos (RMS, media, kurtosis, pico, factor de cresta, etc.) calculados sobre 8 canales de acelerómetros a lo largo de 2.156 ventanas de ensayo.
   - **Perfil:** Telemetría agregada de baja frecuencia con alta correlación intercanal.

2. **`DS-02`: NASA C-MAPSS Turbofan Simulation (`nasa_turbofan_float32.bin`):**
   - **Origen:** Simulación termodinámica de degradación de motores de turbina C-MAPSS.
   - **Naturaleza:** 24 canales de coma flotante de 32 bits (ajustes operacionales y sensores de flujo/presión/temperatura).
   - **Perfil:** Serie multivariada de control industrial continuo (puente de comparación directa con BP-010).

### 4.2 Datasets Pendientes de Formalización (Fase B)

3. **`DS-03`: Señales Crudas de Vibración NASA IMS (20 kHz):**
   - Formato objetivo: Registro de adquisición directo de 20.480 muestras por snapshot a 20 kHz, evaluando el comportamiento ante ruido estocástico de alta frecuencia sin pre-procesamiento.
4. **`DS-04`: Telemetría Industrial IoT / Smart Grid con Jitter:**
   - Formato objetivo: Registro de sensores ambientales o medidores eléctricos inteligentes con timestamps reales desigualmente espaciados, evaluando la robustez del codec ante jitter temporal real.

---

## 5. Definición Física del Acceso Selectivo y Consultas TSDB

Para convertir el benchmark en una evaluación con valor comercial tangible, se define un modelo físico riguroso para la medición de consultas selectivas.

### 5.1 Escenarios de Consulta Estandarizados
Se medirán tres patrones de acceso representativos de cargas de trabajo reales en bases de datos de series temporales:

1. **Consulta $Q_1$ (Ventana Reciente Monocanal - 1.000 puntos):**
   - Recuperación de los últimos 1.000 valores para un canal sensor crítico $C_k$ (ej. graficar el historial reciente de temperatura).
2. **Consulta $Q_2$ (Ventana Histórica Monocanal - 10.000 puntos):**
   - Recuperación de un rango temporal continuo de 10.000 puntos en un canal sensor para cálculo de promedios móviles o detección de anomalías.
3. **Consulta $Q_3$ (Proyección Multicanal Selectiva - 4 canales):**
   - Recuperación de 4 sensores simultáneos (ej. los 4 acelerómetros del eje X) sobre un intervalo temporal determinado, descartando los 44 canales restantes del dataset.

### 5.2 Contabilidad Física del Trabajo de Decodificación
Para cada consulta se medirán de forma estricta los siguientes parámetros de ejecución:

- **$B_{\text{read}}$ (Bytes Leídos):** Volumen de bytes leídos físicamente del bloque comprimido desde almacenamiento/memoria para resolver la consulta.
- **$B_{\text{decomp}}$ (Bytes Descomprimidos):** Volumen total de datos expandidos temporalmente a memoria durante el proceso.
- **$B_{\text{target}}$ (Bytes Solicitados):** Volumen exacto de datos requeridos por la consulta ($N_{\text{puntos}} \times \text{sizeof}(\text{float})$).
- **Factor de Amplificación de Lectura ($A_{\text{read}}$):**
  $$A_{\text{read}} = \frac{B_{\text{decomp}}}{B_{\text{target}}}$$
  *Si un motor obliga a descomprimir los 48 canales completos para entregar un solo canal, su $A_{\text{read}}$ será $\approx 48\times$, penalizando drásticamente la latencia y la memoria.*
- **Memoria Dinámica Asignada:** Cantidad de llamadas a `malloc`/`free` y memoria pico temporal requerida para procesar la consulta.
- **Latencia Wall-Clock End-to-End:** Tiempo total (en microsegundos) desde la emisión de la solicitud hasta la disponibilidad de los valores en un array plano contiguo.

---

## 6. Métricas Multidimensionales de Rendimiento

El desempeño de cada configuración se evaluará bajo seis dimensiones cuantitativas:

1. **Eficiencia de Almacenamiento:**
   - **Ratio de Compresión:** $R = \frac{\text{Tamaño Comprimido}}{\text{Tamaño Original}} \times 100\%$.
   - **Bits por Muestra ($\text{bpc}$):** Promedio de bits utilizados por cada tupla o muestra sensor.
2. **Velocidad de Compresión / Ingesta:**
   - Tasa en Megabytes por segundo ($\text{MB/s}$).
   - Rendimiento en Millones de puntos por segundo ($\text{Mpts/s}$).
3. **Velocidad de Descompresión Masiva:**
   - Tasa en $\text{MB/s}$ y $\text{Mpts/s}$ en descompresión del flujo completo.
4. **Latencia de Acceso Selectivo:**
   - Tiempos de respuesta para consultas $Q_1$, $Q_2$ y $Q_3$ en microsegundos ($\mu\text{s}$).
5. **Huella de Memoria Operativa:**
   - Asignaciones dinámicas y memoria de trabajo máxima requerida por el compresor y descompresor.
6. **Integridad Física Bit-Exact:**
   - Validación mediante `memcmp` continuo de todos los timestamps y valores reconstruidos frente a la entrada original.

---

## 7. Régimen Estadístico y de Integridad (Protocolo R15)

Se adapta y profundiza el rigor estadístico consolidado en BP-010:

### 7.1 Protocolo de Muestreo y Aleatorización
- **Régimen Canónico:** **1 ciclo de warm-up + 20 iteraciones medidas** por configuración y por consulta.
- **Aleatorización Local:** Barajado pseudoaleatorio de motores mediante Fisher-Yates por dataset/bloque, empleando flujo PRNG compartido con semilla fija (`seed = 0`).
- **Aislamiento Ambiental:** Afinidad de CPU fijada a núcleo lógico dedicado y minimización de procesos en segundo plano.

### 7.2 Reglas Estadísticas de Detección de Dispersión
- **Outliers (Regla de Tukey Canónica):**
  $$Q_1 = \frac{x_{(4)} + x_{(5)}}{2}, \quad Q_3 = \frac{x_{(14)} + x_{(15)}}{2}, \quad \text{IQR} = Q_3 - Q_1$$
  $$\text{Límite Inferior} = Q_1 - 1.5 \cdot \text{IQR}, \quad \text{Límite Superior} = Q_3 + 1.5 \cdot \text{IQR}$$
- **Umbrales de Estabilidad Obligatorios:**
  - $\text{Outliers} \le 2$ por dirección.
  - $\text{CV}_{\text{raw}} \le 10,0\%$.
  - $\text{CV}_{\text{filt}} \le 5,0\%$.
- **Preservación Integral:** Toda serie inestable será catalogada e informada sin exclusiones selectivas ni reemplazos arbitrarios.

---

## 8. Criterios de Evaluación y Clasificación Comercial Preestablecida

Para evitar interpretaciones subjetivas post-hoc, los resultados de BP-011 se clasificarán formalmente en tres categorías comerciales en función de su comportamiento en perfiles de carga definidos:

### 8.1 Perfiles de Carga Industrial Estandarizados

1. **Perfil 1 — Ingesta Edge / RTU (Recursos Restringidos):**
   - *Prioridad:* Bajo consumo de CPU, baja memoria ($\le 4\text{ MB}$), throughput sostenido $\ge 10\text{ Mpts/s}$.
   - *Tolerancia:* Puede sacrificar un $10\%$ de ratio si garantiza compresión determinista en streaming.
2. **Perfil 2 — Historiador SCADA Central (Densidad de Almacenamiento):**
   - *Prioridad:* Máxima reducción de volumen ($\text{bits/sample}$ mínimos) en bloques consolidados de tiempo.
   - *Tolerancia:* Acepta mayor tiempo de compresión en batch si la descompresión posterior es rápida.
3. **Perfil 3 — Motor Analítico TSDB (Consultas Concurrentes y Proyección):**
   - *Prioridad:* Mínima latencia en consultas $Q_1, Q_2, Q_3$ y bajo factor de amplificación de lectura ($A_{\text{read}} \approx 1.0$).
   - *Tolerancia:* Acepta un ratio ligeramente inferior si permite decodificar columnas individuales sin tocar el resto del bloque.

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

Una vez que el presente protocolo cuente con la revisión crítica y dictamen del Auditor Líder:
1. **Fase B:** Auditoría estática y validación de las implementaciones fuente de Gorilla, Chimp/Chimp128 y los datasets formalizados.
2. **Fase C:** Construcción del arnés experimental de series temporales `tests/benchmarks/benchmark_timeseries_suite.c` con instrumentación de consultas selectivas.
3. **Fase D:** Auditoría estática pre-flight del arnés antes de autorizar cualquier compilación.
