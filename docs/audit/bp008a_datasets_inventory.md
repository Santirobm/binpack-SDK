# BP-008A — Dossier de Congelamiento e Inventario Exhaustivo de Datasets

**Estado:** Completado y Congelado  
**Alcance:** Inventario completo, verificación de procedencia, licenciamiento, especificación de transformaciones y cálculo de hashes criptográficos SHA-256 completos (sin truncamiento).  
**Rama:** `audit/bp1-core-safety`  
**Regla de Integridad:** Prohibida cualquier modificación posterior de los archivos del corpus. Toda corrida de benchmark debe verificar `sha256(file) == EXPECTED_HASH`.

---

## 1. Inventario Global y Registro de Hashes Criptográficos

Todos los archivos han sido verificados directamente en disco. A continuación se detalla el inventario exhaustivo con tamaño exacto en bytes y sumas de verificación criptográficas completas (SHA-256 de 64 caracteres hex y MD5):

### 1.1. Categoría: Binarios y Firmware Embebido (`arm_binaries/`)
| Archivo | Tamaño (Bytes) | SHA-256 Completo | MD5 | Procedencia y Licencia |
|---|---|---|---|---|
| `busybox_armv7l` | 1.109.128 | `8fceda57d83418bb288de0aef3c6df5e9421344ed0515c90fa5206f15c50f7e4` | `e24d678d9c18c7811e16f4755dfd1e19` | BusyBox v1.21.1 oficial (ARMv7-A ELF). Licencia GNU GPL v2. |
| `arm_synthetic_branch_loop.elf` | 100.064 | `076324ff9f0f850e06c73e6f50947cadb0358721ea1bf106bf5a979142268c48` | `7dfad41e2cad64dd30759a07de8e1e26` | Micro-benchmark sintético Thumb-2 (`BL/B.W`). Licencia MIT. |

### 1.2. Categoría: Telemetría Industrial y Series Temporales (`nasa_telemetry/`)
| Archivo | Tamaño (Bytes) | SHA-256 Completo | MD5 | Procedencia y Licencia |
|---|---|---|---|---|
| `nasa_turbofan_ascii.txt` | 3.515.356 | `963b5e22825b34d8b21c69e1aeb4af3e647050eb672ee8834ba4b5d91d2de0f8` | `259f340bac32ce6fa8894815600fa757` | NASA C-MAPSS `train_FD001.txt` (Saxena & Goebel, 2008). NASA Open Data / Public Domain. |
| `nasa_turbofan_float32.bin`| 1.980.576 | `ee06f50d129b163884753299939c7e150d785e63928de860777b9f7869ce7c59` | `bbf186a2599069bb7b0a8f3141a8aedc` | Matriz binaria IEEE 754 float32 empaquetada (24 canales continuos). |
| `nasa_bearing_ims_test1.csv`| 1.905.892 | `92df06fc3e54be5925c1bab9570741d6218354d193a1f27d5a26fd9f3415273c` | `7efeced4ac9053b20c341e0d0956a341` | Univ. of Cincinnati IMS / NASA PCoE Bearing Test 1 (48 features estadísticas). Public Domain. |
| `nasa_bearing_ims_test2.csv`| 462.350 | `d1889c18df68b68a759794dd9a4eb58c92fa55c0f2ef7ff719e5b0a33a49eefc` | `4b9d1063119f31b84436cca2da7d89da` | Univ. of Cincinnati IMS / NASA PCoE Bearing Test 2. Public Domain. |
| `nasa_bearing_ims_test3.csv`| 2.974.328 | `56f58a957ecbed1321cc67f5283f7d356c8259ca2b4b08c02f050a8a2076e0e6` | `bc940ac06fcab0d806bc895ec42213ce` | Univ. of Cincinnati IMS / NASA PCoE Bearing Test 3. Public Domain. |

### 1.3. Categoría: Corpus General Silesia (`silesia/`)
| Archivo | Tamaño (Bytes) | SHA-256 Completo | MD5 | Descripción / Contenido |
|---|---|---|---|---|
| `dickens` | 10.192.446 | `b24c37886142e11d0ee687db6ab06f936207aa7f2ea1fd1d9a36763c7a507e6a` | `88334708559f6db57d79096bc0aca07e` | Texto literario en inglés (obras de Charles Dickens). |
| `mozilla` | 51.220.480 | `657fc3764b0c75ac9de9623125705831ebbfbe08fed248df73bc2dc66e2a963b` | `c7789a2097f1ff944b0c737430a339b3` | Binario ejecutable compilado (tar de componentes Mozilla). |
| `mr` | 9.970.564 | `68637ed52e3e4860174ed2dc0840ac77d5f1a60abbcb13770d5754e3774d53e6` | `38e623e3093b7bf2003ca4b1bbc19927` | Resonancia magnética médica (imágenes en escala de grises de 16-bit). |
| `nci` | 33.553.445 | `fc63a31770947b8c2062d3b19ca94c00485a232bb91b502021948fee983e1635` | `31f85bc8706f3c921104e7c169e2e2e1` | Base de datos química del National Cancer Institute. |
| `ooffice` | 6.152.192 | `e7ee013880d34dd5208283d0d3d91b07f442e067454276095ded14f322a656eb` | `573c4ae915e36631d8f2dcffb9b9b66d` | Binario ejecutable DLL/SO de OpenOffice.org. |
| `osdb` | 10.085.684 | `60f027179302ca3ad87c58ac90b6be72ec23588aaa7a3b7fe8ecc0f11def3fa3` | `e734b0c48e6a982adfb5802da3032ecd` | Volcado de base de datos relacional Open Source Database Benchmark. |
| `reymont` | 6.627.202 | `0eac0114a3dfe6e2ee1f345a0f79d653cb26c3bc9f0ed79238af4933422b7578` | `d8f54d78105079775f32d76dc55fc671` | Texto literario en polaco con codificación de 8 bits (Władysław Reymont). |
| `samba` | 21.606.400 | `93ba07bc44d8267789c1d911992f40b089ffa2140b4a160fac11ccae9a40e7b2` | `154eaea7ea70e89f6339ff0abf4112ca` | Archivo tar de código fuente C del proyecto Samba. |
| `sao` | 7.251.944 | `c2d0ea2cc59d4c21b7fe43a71499342a00cbe530a1d5548770e91ecd6214adcc` | `79e95a22e18cd82b7e42bf91b380d30b` | Catálogo astronómico estelar (Smithsonian Astrophysical Observatory). |
| `webster` | 41.458.703 | `6a68f69b26daf09f9dd84f7470368553194a0b294fcfa80f1604efb11143a383` | `474931ad907ac27bf962c75ded46c069` | Diccionario en inglés (1913 Webster's Unabridged Dictionary). |
| `x-ray` | 8.474.240 | `7de9fce1405dc44ae5e6813ed21cd5751e761bd4265655a005d39b9685d1c9ad` | `9baec32ad14ec3eff487d254382cb91c` | Imagen médica radiográfica de 8 bits. |
| `xml` | 5.345.280 | `0e82e54e695c1938e4193448022543845b33020c8be6bf3bf3ead2224903e08c` | `9b09c0c80104adb8aae910b7d7db003e` | Archivo XML estructurado con etiquetas altamente repetitivas. |
| *(Archivo contenedor)* `silesia.zip` | 68.182.744 | `0626e25f45c0ffb5dc801f13b7c82a3b75743ba07e3a71835a41e3d9f63c77af` | `c240c17d6805fb8a0bde763f1b94cd99` | Archivo ZIP original descargado de `mattmahoney.net`. |

### 1.4. Categoría: Corpus Clásico Canterbury (`canterbury/`)
| Archivo | Tamaño (Bytes) | SHA-256 Completo | MD5 | Descripción / Contenido |
|---|---|---|---|---|
| `alice29.txt` | 152.089 | `7467306ee0feed4971260f3c87421154a05be571d944e9cb021a5713700c38f0` | `74c3b556c76ea0cfae111cdb64d08255` | Alicia en el País de las Maravillas (texto en inglés). |
| `asyoulik.txt` | 125.179 | `eaa3526fe53859f34ecdf255712f9ecf0b2c903451d4755b2edaa2e2599cb0fc` | `2183e4e23c67c1dcc6cb84e13d8863bf` | Obra teatral de Shakespeare (As You Like It). |
| `cp.html` | 24.603 | `e0cd21cef5b6c4069461e949be100080c3ce887de6f1dd8626c480528efaaf61` | `d4b4e81b46ae7a3cbc2b733bbd6d8cc8` | Documento HTML estándar con etiquetas. |
| `fields.c` | 11.150 | `85d73e354cc50cec76cb5a50537cf8dc035f8cbb8480f9e1cbe2f7d6c23393c7` | `82640457a3569c49615974b5053a73df` | Código fuente C estándar. |
| `grammar.lsp` | 3.721 | `1b0805dfc0ae706b35aac2bb4e15f02485efd24dda5dbd29de7b2f84d1a88c15` | `ad6ff075a8058262564493050f67f702` | Código fuente LISP con paréntesis anidados. |
| `kennedy.xls` | 1.029.744 | `9af47239ca29dfe20e633f80bbbb9a4cc9783d0803d7b2b5626f42e4c3790420` | `b408d2207b18aba5a378548698735d64` | Hoja de cálculo de Excel en formato binario OLE2 BIFF8. |
| `lcet10.txt` | 426.754 | `5314ba1dbb03f471df88bec6cd120a938ef60d0fd3511c5c1dce61bf7463245f` | `5d69b132c7929dec190daa69f081d472` | Documento técnico de informática (texto plano). |
| `plrabn12.txt` | 481.861 | `07e2e0b461af78c7c647cb53dab39de560198e16f799b4516eccf0fbd69f764c` | `4655507b26054b80b98bac2b44d8200f` | Poema Paradise Lost de Milton (texto en inglés). |
| `ptt5` | 513.216 | `0ec3a75089bb52342813496b17e51377bc9eba3cb519a444d67025354841d650` | `29eca86237730fce52232612036284b9` | Imagen binaria de facsímil (CCITT Grupo 4). |
| `sum` | 38.240 | `ee5733cd76ecc2f9d8ff156adc3c02a7a851051dcf43a2d56ff4ee4ff606bdb3` | `0d347e6c137c15616ee2becc0123e0a3` | Binario compilado SPARC executable. |
| `xargs.1` | 4.227 | `c58aeb5d2d1e12751d47e7412b45784405fc30a5671b03d480fa05776e183619` | `7bcc27abddbcc8dc56d9b1950ce93a69` | Página de manual Unix (formato groff/troff). |
| *(Archivo contenedor)* `cantrbry.tar.gz` | 739.071 | `f140e8a5b73d3f53198555a63bfb827889394a42f20825df33c810c3d5e3f8fb` | `442e56cfffdf460d25b0b91650a55908` | Tarball original del corpus de Canterbury. |

---

## 2. Documentación Forense de Procedencia y Transformaciones

### 2.1. Binario de Producción: `busybox_armv7l`
- **Origen:** Repositorio oficial de binarios precompilados del proyecto BusyBox (`https://busybox.net/downloads/binaries/1.21.1/busybox-armv7l`).
- **Arquitectura:** ARMv7-A de 32 bits, Little-Endian, EABI5, enlazado estáticamente con musl libc.
- **Licencia:** GNU General Public License v2 (GPL-2.0).
- **Transformaciones:** **Ninguna (0 transformaciones)**. Se utiliza exactamente el binario binario descargado bit a bit.

### 2.2. Micro-Benchmark: `arm_synthetic_branch_loop.elf`
- **Origen:** Generador sintético de instrucciones ARM Thumb-2 para pruebas de esfuerzo de filtros branch de CPU.
- **Contenido:** 25.354 pares de instrucciones de salto relativo repetitivo (`BL` y `B.W`) dirigidas hacia un conjunto cerrado de funciones destino.
- **Propósito:** Medición del límite teórico de eficiencia del filtro de normalización de saltos (`Target - PC \to Target`).
- **Condición de Divulgación:** Este archivo debe aislarse en los reportes como un micro-benchmark de caracterización de kernel y no extrapolarse a código general de producción.

### 2.3. Serie Temporal: `nasa_turbofan_float32.bin`
- **Origen del Dataset Original:** NASA Ames Prognostics Center of Excellence (PCoE), dataset C-MAPSS (*Commercial Modular Aero-Propulsion System Simulation* Turbofan Engine Degradation Simulation, subconjunto `train_FD001.txt`).
  - Publicación: A. Saxena y K. Goebel (2008), NASA Ames Data Repository.
  - Licencia: NASA Open Data / Public Domain.
- **Estructura del Archivo de Origen (`train_FD001.txt`):**
  - Formato ASCII tabular con 20.631 filas y 26 columnas separadas por espacios.
  - Columna 1: ID de la unidad (entero).
  - Columna 2: Ciclo operativo (entero).
  - Columnas 3 a 5: 3 ajustes operativos (*operational settings*).
  - Columnas 6 a 26: 21 lecturas de sensores de turbina (presión HPC, temperatura fan, flujo de combustible, etc.).
- **Proceso Exacto de Transformación:**
  1. Se iteran las 20.631 filas completas de `train_FD001.txt`.
  2. Se extraen exclusivamente las 24 columnas numéricas de telemetría de ingeniería (columnas 3 a 26, `parts[2:]`), omitiendo los identificadores enteros `unit_id` y `cycle`.
  3. Cada valor de texto se analiza como número de coma flotante de precisión simple.
  4. Total de elementos flotantes resultantes:
     $$20.631 \text{ filas} \times 24 \text{ columnas} = 495.144 \text{ flotantes IEEE 754}$$
  5. Se empaquetan en memoria mediante `struct.pack("<f", val)` (orden **Little-Endian IEEE 754** nativo).
  6. Cálculo del tamaño binario final:
     $$495.144 \text{ flotantes} \times 4 \text{ bytes/float} = 1.980.576 \text{ bytes}$$
- **Tratamiento de Valores Faltantes:** El dataset `train_FD001.txt` es un registro continuo y completo generado mediante simulación aero-termodinámica cerrada; **no contiene valores nulos, NaNs ni celdas vacías**.
- **Inmutabilidad y Preservación:** El script generador queda fijado en [`tests/fetch_corpora.py`](file:///c:/Users/sgi.lm/Desktop/binpack-SDK/tests/fetch_corpora.py#L153-L164), y el archivo `nasa_turbofan_ascii.txt` original se conserva íntegro para verificar la trazabilidad de la conversión.

### 2.4. Telemetría Tabular: `nasa_bearing_ims_test1.csv`
- **Origen:** Center for Intelligent Maintenance Systems (IMS), Universidad de Cincinnati en colaboración con NASA PCoE.
- **Instalación:** Banco de pruebas de fatiga de rodamientos de rodillos Rexnord ZA-2115 montados sobre un eje a 2.000 RPM constante.
- **Estructura de Datos:**
  - 4 rodamientos instrumentados con 2 acelerómetros PCB 353B33 por rodamiento (canales x e y) = 8 canales de adquisición de vibración a 20 kHz.
  - Cada registro corresponde a una ventana de muestreo de 10 minutos (20.480 puntos) procesada estadísticamente.
  - Cabecera: `file_index, timestamp, filename` + 48 columnas con métricas temporales (RMS, Mean, Std, Max, Kurtosis, Skewness para cada uno de los 8 canales).
  - Filas: 2.156 registros temporales continuos desde el estado nominal hasta el fallo final.
- **Transformaciones:** **Ninguna (0 transformaciones)**. Se preserva el archivo ASCII CSV original delimitado por comas con separadores de línea LF/CRLF estándares, sin normalizaciones matemáticas ni truncamiento de precisión numérica.

---

## 3. Protocolo de Verificación Automatizada

Para asegurar que ningún archivo sufra alteraciones durante la ejecución de los benchmarks:
1. El script [`tests/fetch_corpora.py`](file:///c:/Users/sgi.lm/Desktop/binpack-SDK/tests/fetch_corpora.py) fue actualizado para validar el 100% de los hashes SHA-256 de 64 caracteres.
2. Cada arnés de benchmarking ejecutará una rutina previa de validación de integridad (`pre_flight_checksum_check()`). Si algún archivo discrepa en un solo bit de su hash de congelamiento, el proceso aborta inmediatamente con error fatal antes de registrar cualquier métrica.

---

## 4. Estado del Hito BP-008A

Con la emisión de este expediente y el cálculo completo e inmutable de los hashes de los 32 archivos:
- **Hito BP-008A:** **Completado y Congelado**.
- **Próximo Paso:** Habilitación de **BP-008B (Auditoría de Baselines, Versiones y Comparabilidad)**.
