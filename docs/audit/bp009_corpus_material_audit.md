# Informe Forense de Validación de Artefactos del Corpus — Hito BP-009 / OT-03

**Expediente:** BP-009 / Orden de Trabajo 03 — Validación Material, Integridad Criptográfica y Trazabilidad del Corpus  
**Proyecto:** BinPack Enterprise SDK  
**Ramas:** `audit/bp1-core-safety`  
**Commit Documental Base:** [`a9fe52e19644bbb6f30d6b485adda0ad5df46f0f`](https://github.com/Santirobm/binpack-SDK/commit/a9fe52e19644bbb6f30d6b485adda0ad5df46f0f)  
**Manifiesto Canónico Reconciliado:** [`tests/experiments/datasets_checksums.json`](file:///C:/Users/sgi.lm/Desktop/binpack-SDK/tests/experiments/datasets_checksums.json) (32 entradas)  
**Fecha de Apertura:** 10 de Noviembre de 2026  
**Fecha de Cierre Formal:** 11 de Noviembre de 2026  
**Estado Administrativo:** **CERRADO Y CERTIFICADO — FASE BP-009 FORMALMENTE CERRADA (OT-01, OT-02, OT-03 APROBADAS)**  

---

## 1. Resumen Ejecutivo y Marco de Apertura de la Validación Material

El presente informe técnico formaliza la conclusión y certificación de la fase de **Validación Material de Artefactos** correspondiente a la **Orden de Trabajo 03 (OT-03)** del hito **BP-009**.

> [!IMPORTANT]
> **Delimitación de Garantías Epistemológicas y Estado Probatorio:**  
> Las mediciones de longitud en bytes, sumas criptográficas SHA-256 y correspondencias de extracción expuestas en este informe constituyen **evidencia técnica obtenida y reportada en el entorno de trabajo local** del ejecutor.  
> Conforme al dictamen formal de auditoría, las verificaciones independientes efectuadas por el Auditor Líder comprendieron la inspección estática del código, la comprobación del árbol y commits Git en GitHub, el análisis del diff de cambios y la verificación matemática y estructural de los contenedores. No se efectuó una reproducción física independiente de la ejecución en Windows ni un recálculo directo de los 32 hashes desde el disco del auditor; por ende, la conformidad material se fundamenta en la evidencia local reportada y contrastada documentalmente.

### 1.1 Parámetros del Entorno de Comprobación Local
- **Plataforma:** Windows 10 Pro (x64) Build 19045
- **Entorno de Ejecución:** Python 3.13.5 (tags/v3.13.5:6cb20a2, Jun 11 2025, 16:15:46) [MSC v.1943 64 bit (AMD64)]
- **Algoritmo de Hashing:** `hashlib.sha256()` con lectura secuencial en bloques de 64 KB (65.536 bytes) y cálculo acumulativo continuo
- **Control de Invarianza:** `obs_bytes == expected_bytes` y `obs_sha256.lower() == expected_sha256.lower()`

---

## 2. Matriz Completa de los 32 Artefactos del Manifiesto Canónico

A continuación se detalla la matriz de correspondencia dimensional y criptográfica de las 32 entradas canónicas reconciliadas en el manifiesto. En la totalidad de las filas, la ruta física observada coincide de forma exacta con la ruta canónica declarada.

| N° | ID Canónico | Categoría | Ruta Canónica Declarada y Física | Tamaño Esperado = Observado (B) | Hash SHA-256 Declarado y Calculado | Concordancia Observada | Estado Probatorio |
| :---: | :--- | :--- | :--- | :---: | :--- | :---: | :--- |
| 01 | `arm_binaries/busybox_armv7l` | `arm_binaries` | `tests/corpora/arm_binaries/busybox_armv7l` | 1,109,128 | `8fceda57d83418bb288de0aef3c6df5e9421344ed0515c90fa5206f15c50f7e4` | **Conforme** | Evidencia local reportada |
| 02 | `arm_binaries/arm_synthetic_branch_loop.elf` | `arm_binaries` | `tests/corpora/arm_binaries/arm_synthetic_branch_loop.elf` | 100,064 | `076324ff9f0f850e06c73e6f50947cadb0358721ea1bf106bf5a979142268c48` | **Conforme** | Evidencia local reportada |
| 03 | `nasa_telemetry/nasa_turbofan_ascii.txt` | `nasa_telemetry` | `tests/corpora/nasa_telemetry/nasa_turbofan_ascii.txt` | 3,515,356 | `963b5e22825b34d8b21c69e1aeb4af3e647050eb672ee8834ba4b5d91d2de0f8` | **Conforme** | Evidencia local reportada |
| 04 | `nasa_telemetry/nasa_turbofan_float32.bin` | `nasa_telemetry` | `tests/corpora/nasa_telemetry/nasa_turbofan_float32.bin` | 1,980,576 | `ee06f50d129b163884753299939c7e150d785e63928de860777b9f7869ce7c59` | **Conforme** | Evidencia local reportada |
| 05 | `nasa_telemetry/nasa_bearing_ims_test1.csv` | `nasa_telemetry` | `tests/corpora/nasa_telemetry/nasa_bearing_ims_test1.csv` | 1,905,892 | `92df06fc3e54be5925c1bab9570741d6218354d193a1f27d5a26fd9f3415273c` | **Conforme** | Evidencia local reportada |
| 06 | `nasa_telemetry/nasa_bearing_ims_test2.csv` | `nasa_telemetry` | `tests/corpora/nasa_telemetry/nasa_bearing_ims_test2.csv` | 462,350 | `d1889c18df68b68a759794dd9a4eb58c92fa55c0f2ef7ff719e5b0a33a49eefc` | **Conforme** | Evidencia local reportada |
| 07 | `nasa_telemetry/nasa_bearing_ims_test3.csv` | `nasa_telemetry` | `tests/corpora/nasa_telemetry/nasa_bearing_ims_test3.csv` | 2,974,328 | `56f58a957ecbed1321cc67f5283f7d356c8259ca2b4b08c02f050a8a2076e0e6` | **Conforme** | Evidencia local reportada |
| 08 | `silesia/silesia.zip` | `silesia` | `tests/corpora/silesia/silesia.zip` | 68,182,744 | `0626e25f45c0ffb5dc801f13b7c82a3b75743ba07e3a71835a41e3d9f63c77af` | **Conforme** | Evidencia local reportada |
| 09 | `silesia/dickens` | `silesia` | `tests/corpora/silesia/dickens` | 10,192,446 | `b24c37886142e11d0ee687db6ab06f936207aa7f2ea1fd1d9a36763c7a507e6a` | **Conforme** | Evidencia local reportada |
| 10 | `silesia/mozilla` | `silesia` | `tests/corpora/silesia/mozilla` | 51,220,480 | `657fc3764b0c75ac9de9623125705831ebbfbe08fed248df73bc2dc66e2a963b` | **Conforme** | Evidencia local reportada |
| 11 | `silesia/mr` | `silesia` | `tests/corpora/silesia/mr` | 9,970,564 | `68637ed52e3e4860174ed2dc0840ac77d5f1a60abbcb13770d5754e3774d53e6` | **Conforme** | Evidencia local reportada |
| 12 | `silesia/nci` | `silesia` | `tests/corpora/silesia/nci` | 33,553,445 | `fc63a31770947b8c2062d3b19ca94c00485a232bb91b502021948fee983e1635` | **Conforme** | Evidencia local reportada |
| 13 | `silesia/ooffice` | `silesia` | `tests/corpora/silesia/ooffice` | 6,152,192 | `e7ee013880d34dd5208283d0d3d91b07f442e067454276095ded14f322a656eb` | **Conforme** | Evidencia local reportada |
| 14 | `silesia/osdb` | `silesia` | `tests/corpora/silesia/osdb` | 10,085,684 | `60f027179302ca3ad87c58ac90b6be72ec23588aaa7a3b7fe8ecc0f11def3fa3` | **Conforme** | Evidencia local reportada |
| 15 | `silesia/reymont` | `silesia` | `tests/corpora/silesia/reymont` | 6,627,202 | `0eac0114a3dfe6e2ee1f345a0f79d653cb26c3bc9f0ed79238af4933422b7578` | **Conforme** | Evidencia local reportada |
| 16 | `silesia/samba` | `silesia` | `tests/corpora/silesia/samba` | 21,606,400 | `93ba07bc44d8267789c1d911992f40b089ffa2140b4a160fac11ccae9a40e7b2` | **Conforme** | Evidencia local reportada |
| 17 | `silesia/sao` | `silesia` | `tests/corpora/silesia/sao` | 7,251,944 | `c2d0ea2cc59d4c21b7fe43a71499342a00cbe530a1d5548770e91ecd6214adcc` | **Conforme** | Evidencia local reportada |
| 18 | `silesia/webster` | `silesia` | `tests/corpora/silesia/webster` | 41,458,703 | `6a68f69b26daf09f9dd84f7470368553194a0b294fcfa80f1604efb11143a383` | **Conforme** | Evidencia local reportada |
| 19 | `silesia/x-ray` | `silesia` | `tests/corpora/silesia/x-ray` | 8,474,240 | `7de9fce1405dc44ae5e6813ed21cd5751e761bd4265655a005d39b9685d1c9ad` | **Conforme** | Evidencia local reportada |
| 20 | `silesia/xml` | `silesia` | `tests/corpora/silesia/xml` | 5,345,280 | `0e82e54e695c1938e4193448022543845b33020c8be6bf3bf3ead2224903e08c` | **Conforme** | Evidencia local reportada |
| 21 | `canterbury/cantrbry.tar.gz` | `canterbury` | `tests/corpora/canterbury/cantrbry.tar.gz` | 739,071 | `f140e8a5b73d3f53198555a63bfb827889394a42f20825df33c810c3d5e3f8fb` | **Conforme** | Evidencia local reportada |
| 22 | `canterbury/alice29.txt` | `canterbury` | `tests/corpora/canterbury/alice29.txt` | 152,089 | `7467306ee0feed4971260f3c87421154a05be571d944e9cb021a5713700c38f0` | **Conforme** | Evidencia local reportada |
| 23 | `canterbury/asyoulik.txt` | `canterbury` | `tests/corpora/canterbury/asyoulik.txt` | 125,179 | `eaa3526fe53859f34ecdf255712f9ecf0b2c903451d4755b2edaa2e2599cb0fc` | **Conforme** | Evidencia local reportada |
| 24 | `canterbury/cp.html` | `canterbury` | `tests/corpora/canterbury/cp.html` | 24,603 | `e0cd21cef5b6c4069461e949be100080c3ce887de6f1dd8626c480528efaaf61` | **Conforme** | Evidencia local reportada |
| 25 | `canterbury/fields.c` | `canterbury` | `tests/corpora/canterbury/fields.c` | 11,150 | `85d73e354cc50cec76cb5a50537cf8dc035f8cbb8480f9e1cbe2f7d6c23393c7` | **Conforme** | Evidencia local reportada |
| 26 | `canterbury/grammar.lsp` | `canterbury` | `tests/corpora/canterbury/grammar.lsp` | 3,721 | `1b0805dfc0ae706b35aac2bb4e15f02485efd24dda5dbd29de7b2f84d1a88c15` | **Conforme** | Evidencia local reportada |
| 27 | `canterbury/kennedy.xls` | `canterbury` | `tests/corpora/canterbury/kennedy.xls` | 1,029,744 | `9af47239ca29dfe20e633f80bbbb9a4cc9783d0803d7b2b5626f42e4c3790420` | **Conforme** | Evidencia local reportada |
| 28 | `canterbury/lcet10.txt` | `canterbury` | `tests/corpora/canterbury/lcet10.txt` | 426,754 | `5314ba1dbb03f471df88bec6cd120a938ef60d0fd3511c5c1dce61bf7463245f` | **Conforme** | Evidencia local reportada |
| 29 | `canterbury/plrabn12.txt` | `canterbury` | `tests/corpora/canterbury/plrabn12.txt` | 481,861 | `07e2e0b461af78c7c647cb53dab39de560198e16f799b4516eccf0fbd69f764c` | **Conforme** | Evidencia local reportada |
| 30 | `canterbury/ptt5` | `canterbury` | `tests/corpora/canterbury/ptt5` | 513,216 | `0ec3a75089bb52342813496b17e51377bc9eba3cb519a444d67025354841d650` | **Conforme** | Evidencia local reportada |
| 31 | `canterbury/sum` | `canterbury` | `tests/corpora/canterbury/sum` | 38,240 | `ee5733cd76ecc2f9d8ff156adc3c02a7a851051dcf43a2d56ff4ee4ff606bdb3` | **Conforme** | Evidencia local reportada |
| 32 | `canterbury/xargs.1` | `canterbury` | `tests/corpora/canterbury/xargs.1` | 4,227 | `c58aeb5d2d1e12751d47e7412b45784405fc30a5671b03d480fa05776e183619` | **Conforme** | Evidencia local reportada |

---

## 3. Trazabilidad de Contenedores y Evidencia de Extracción

Para satisfacer el requerimiento de separar la integridad del contenedor de la integridad de sus miembros, se auditó exhaustivamente la cadena de extracción de los dos archivos comprimidos de referencia.

### 3.1 Reconciliación de Rutas de Contenedores (Auditoría Antes/Después)

En cumplimiento del procedimiento controlado de reconciliación de rutas anidadas aprobado por auditoría:

| Artefacto Contenedor | Ruta Original Observada | Ruta Canónica Destino | Tamaño Antes / Después | SHA-256 Antes / Después | Mecanismo de Movimiento |
| :--- | :--- | :--- | :---: | :---: | :--- |
| **Silesia ZIP** | `tests/corpora/silesia.zip` | `tests/corpora/silesia/silesia.zip` | 68.182.744 B $\equiv$ 68.182.744 B | `0626e25f45c0ffb5dc801f13b7c82a3b75743ba07e3a71835a41e3d9f63c77af` | Reubicación controlada (`shutil.move`), comprobación previa de no existencia en destino |
| **Canterbury TAR.GZ** | `tests/corpora/cantrbry.tar.gz` | `tests/corpora/canterbury/cantrbry.tar.gz` | 739.071 B $\equiv$ 739.071 B | `f140e8a5b73d3f53198555a63bfb827889394a42f20825df33c810c3d5e3f8fb` | Reubicación controlada (`shutil.move`), comprobación previa de no existencia en destino |

### 3.2 Trazabilidad de Miembros de Silesia (12 Artefactos Extraídos)

Se procedió a la apertura in-memory de `tests/corpora/silesia/silesia.zip` mediante `zipfile.ZipFile` en modo solo lectura, calculando el SHA-256 del flujo descomprimido directamente desde el archivo ZIP y contrastándolo contra los bytes físicos del archivo individual existente en disco en `tests/corpora/silesia/`:

| N° | Miembro Interno ZIP | Tamaño Descomprimido (B) | SHA-256 Flujo Interno ZIP | SHA-256 Archivo Físico en Disco | Correspondencia de Integridad |
| :---: | :--- | :---: | :--- | :--- | :---: |
| 01 | `dickens` | 10,192,446 | `b24c37886142e11d0ee687db6ab06f936207aa7f2ea1fd1d9a36763c7a507e6a` | `b24c37886142e11d0ee687db6ab06f936207aa7f2ea1fd1d9a36763c7a507e6a` | **100% Bit-a-bit** |
| 02 | `mozilla` | 51,220,480 | `657fc3764b0c75ac9de9623125705831ebbfbe08fed248df73bc2dc66e2a963b` | `657fc3764b0c75ac9de9623125705831ebbfbe08fed248df73bc2dc66e2a963b` | **100% Bit-a-bit** |
| 03 | `mr` | 9,970,564 | `68637ed52e3e4860174ed2dc0840ac77d5f1a60abbcb13770d5754e3774d53e6` | `68637ed52e3e4860174ed2dc0840ac77d5f1a60abbcb13770d5754e3774d53e6` | **100% Bit-a-bit** |
| 04 | `nci` | 33,553,445 | `fc63a31770947b8c2062d3b19ca94c00485a232bb91b502021948fee983e1635` | `fc63a31770947b8c2062d3b19ca94c00485a232bb91b502021948fee983e1635` | **100% Bit-a-bit** |
| 05 | `ooffice` | 6,152,192 | `e7ee013880d34dd5208283d0d3d91b07f442e067454276095ded14f322a656eb` | `e7ee013880d34dd5208283d0d3d91b07f442e067454276095ded14f322a656eb` | **100% Bit-a-bit** |
| 06 | `osdb` | 10,085,684 | `60f027179302ca3ad87c58ac90b6be72ec23588aaa7a3b7fe8ecc0f11def3fa3` | `60f027179302ca3ad87c58ac90b6be72ec23588aaa7a3b7fe8ecc0f11def3fa3` | **100% Bit-a-bit** |
| 07 | `reymont` | 6,627,202 | `0eac0114a3dfe6e2ee1f345a0f79d653cb26c3bc9f0ed79238af4933422b7578` | `0eac0114a3dfe6e2ee1f345a0f79d653cb26c3bc9f0ed79238af4933422b7578` | **100% Bit-a-bit** |
| 08 | `samba` | 21,606,400 | `93ba07bc44d8267789c1d911992f40b089ffa2140b4a160fac11ccae9a40e7b2` | `93ba07bc44d8267789c1d911992f40b089ffa2140b4a160fac11ccae9a40e7b2` | **100% Bit-a-bit** |
| 09 | `sao` | 7,251,944 | `c2d0ea2cc59d4c21b7fe43a71499342a00cbe530a1d5548770e91ecd6214adcc` | `c2d0ea2cc59d4c21b7fe43a71499342a00cbe530a1d5548770e91ecd6214adcc` | **100% Bit-a-bit** |
| 10 | `webster` | 41,458,703 | `6a68f69b26daf09f9dd84f7470368553194a0b294fcfa80f1604efb11143a383` | `6a68f69b26daf09f9dd84f7470368553194a0b294fcfa80f1604efb11143a383` | **100% Bit-a-bit** |
| 11 | `xml` | 5,345,280 | `0e82e54e695c1938e4193448022543845b33020c8be6bf3bf3ead2224903e08c` | `0e82e54e695c1938e4193448022543845b33020c8be6bf3bf3ead2224903e08c` | **100% Bit-a-bit** |
| 12 | `x-ray` | 8,474,240 | `7de9fce1405dc44ae5e6813ed21cd5751e761bd4265655a005d39b9685d1c9ad` | `7de9fce1405dc44ae5e6813ed21cd5751e761bd4265655a005d39b9685d1c9ad` | **100% Bit-a-bit** |

*Conclusión del examen:* Los 12 archivos físicos en `tests/corpora/silesia/` son idénticos bit a bit a las corrientes de datos descomprimidas del contenedor `silesia.zip`. No se evidencia sustitución, corrupción ni alteración de miembros.

### 3.3 Trazabilidad de Miembros de Canterbury (11 Artefactos Extraídos)

Se procedió a la apertura in-memory de `tests/corpora/canterbury/cantrbry.tar.gz` mediante `tarfile.open(mode='r:gz')` en modo solo lectura, calculando el SHA-256 del flujo descomprimido directamente desde el archivo tarball y contrastándolo contra los bytes físicos del archivo individual en `tests/corpora/canterbury/`:

| N° | Miembro Interno TAR | Tamaño Descomprimido (B) | SHA-256 Flujo Interno TAR | SHA-256 Archivo Físico en Disco | Correspondencia de Integridad |
| :---: | :--- | :---: | :--- | :--- | :---: |
| 01 | `alice29.txt` | 152,089 | `7467306ee0feed4971260f3c87421154a05be571d944e9cb021a5713700c38f0` | `7467306ee0feed4971260f3c87421154a05be571d944e9cb021a5713700c38f0` | **100% Bit-a-bit** |
| 02 | `asyoulik.txt` | 125,179 | `eaa3526fe53859f34ecdf255712f9ecf0b2c903451d4755b2edaa2e2599cb0fc` | `eaa3526fe53859f34ecdf255712f9ecf0b2c903451d4755b2edaa2e2599cb0fc` | **100% Bit-a-bit** |
| 03 | `cp.html` | 24,603 | `e0cd21cef5b6c4069461e949be100080c3ce887de6f1dd8626c480528efaaf61` | `e0cd21cef5b6c4069461e949be100080c3ce887de6f1dd8626c480528efaaf61` | **100% Bit-a-bit** |
| 04 | `fields.c` | 11,150 | `85d73e354cc50cec76cb5a50537cf8dc035f8cbb8480f9e1cbe2f7d6c23393c7` | `85d73e354cc50cec76cb5a50537cf8dc035f8cbb8480f9e1cbe2f7d6c23393c7` | **100% Bit-a-bit** |
| 05 | `grammar.lsp` | 3,721 | `1b0805dfc0ae706b35aac2bb4e15f02485efd24dda5dbd29de7b2f84d1a88c15` | `1b0805dfc0ae706b35aac2bb4e15f02485efd24dda5dbd29de7b2f84d1a88c15` | **100% Bit-a-bit** |
| 06 | `kennedy.xls` | 1,029,744 | `9af47239ca29dfe20e633f80bbbb9a4cc9783d0803d7b2b5626f42e4c3790420` | `9af47239ca29dfe20e633f80bbbb9a4cc9783d0803d7b2b5626f42e4c3790420` | **100% Bit-a-bit** |
| 07 | `lcet10.txt` | 426,754 | `5314ba1dbb03f471df88bec6cd120a938ef60d0fd3511c5c1dce61bf7463245f` | `5314ba1dbb03f471df88bec6cd120a938ef60d0fd3511c5c1dce61bf7463245f` | **100% Bit-a-bit** |
| 08 | `plrabn12.txt` | 481,861 | `07e2e0b461af78c7c647cb53dab39de560198e16f799b4516eccf0fbd69f764c` | `07e2e0b461af78c7c647cb53dab39de560198e16f799b4516eccf0fbd69f764c` | **100% Bit-a-bit** |
| 09 | `ptt5` | 513,216 | `0ec3a75089bb52342813496b17e51377bc9eba3cb519a444d67025354841d650` | `0ec3a75089bb52342813496b17e51377bc9eba3cb519a444d67025354841d650` | **100% Bit-a-bit** |
| 10 | `sum` | 38,240 | `ee5733cd76ecc2f9d8ff156adc3c02a7a851051dcf43a2d56ff4ee4ff606bdb3` | `ee5733cd76ecc2f9d8ff156adc3c02a7a851051dcf43a2d56ff4ee4ff606bdb3` | **100% Bit-a-bit** |
| 11 | `xargs.1` | 4,227 | `c58aeb5d2d1e12751d47e7412b45784405fc30a5671b03d480fa05776e183619` | `c58aeb5d2d1e12751d47e7412b45784405fc30a5671b03d480fa05776e183619` | **100% Bit-a-bit** |

*Conclusión del examen:* Los 11 archivos físicos en `tests/corpora/canterbury/` son idénticos bit a bit a las corrientes de datos extraídas del tarball `cantrbry.tar.gz`.

---

## 4. Delimitación y Registro Expreso de Reservas Documentales

De acuerdo con el mandato de auditoría, se mantienen explícitamente abiertas y no resueltas de forma dogmática las siguientes reservas:

### 4.1 NASA IMS Bearing Test 3 (Neutralidad Documental)
- **Ruta:** `tests/corpora/nasa_telemetry/nasa_bearing_ims_test3.csv` (2.974.328 bytes, SHA-256: `56f58a957ecbed1321cc67f5283f7d356c8259ca2b4b08c02f050a8a2076e0e6`).
- **Declaración Formal:** Se preserva la redacción neutral de auditoría:  
  *«Se conserva la variante documental correspondiente al intervalo temporal del archivo congelado en el SDK (6.324 registros temporales continuos comprendidos entre 2004-03-04 09:27:46 y 2004-04-18 02:42:55); no se afirma de forma concluyente que represente la totalidad de una fractura física completa sin verificación pericial independiente.»*

### 4.2 NASA C-MAPSS Turbofan (Distinción de Procedencia)
- **Rutas:** `tests/corpora/nasa_telemetry/nasa_turbofan_ascii.txt` (3.515.356 bytes) y `tests/corpora/nasa_telemetry/nasa_turbofan_float32.bin` (1.980.576 bytes).
- **Declaración Formal:** Los archivos provienen del repositorio de datos del SDK; su equivalencia byte por byte frente a la distribución original de NASA Ames PCoE permanece catalogada como una **correspondencia declarada**, sujeta a contraste criptográfico externo cuando se disponga de la fuente primaria original.

### 4.3 Fixture Binario ARM Sintético (`arm_synthetic_branch_loop.elf`)
- **Ruta:** `tests/corpora/arm_binaries/arm_synthetic_branch_loop.elf` (100.064 bytes, SHA-256: `076324ff9f0f850e06c73e6f50947cadb0358721ea1bf106bf5a979142268c48`).
- **Declaración Formal:** Clasificado formalmente como fixture binario sintético con prefijo ELF-like no conforme (`e_ehsize = 0`, `e_entry = 0x0`), con 12 bytes de padding y 25.000 instrucciones decrementales ARM A32 `BL`. No constituye un ejecutable ELF32 estándar.  
- **Instrumento de Reproducción:** El script [`scripts/reproduce_arm_synthetic_branch_loop.py`](file:///C:/Users/sgi.lm/Desktop/binpack-SDK/scripts/reproduce_arm_synthetic_branch_loop.py) se conserva **estrictamente como propuesta de diseño documental abstracta**; no ha sido incorporado al repositorio, no existe en disco y no ha sido ejecutado.

---

## 5. Instrumento Forense de Verificación Material (R7.1) y Resultados de Ejecución

Para someter el corpus a una verificación física reproducible, robusta y automatizada, se diseñó e implementó el script forense independiente [`scripts/audit_corpus_verification.py`](https://github.com/Santirobm/binpack-SDK/blob/audit/bp009-instrument-r7/scripts/audit_corpus_verification.py), sometido a rondas sucesivas de auditoría estática (R1 a R7) hasta alcanzar la versión definitiva aprobada **Revisión R7.1**.

### 5.1 Identidad y Trazabilidad de la Publicación R7.1
- **Rama de Publicación Dedicada:** [`audit/bp009-instrument-r7`](https://github.com/Santirobm/binpack-SDK/tree/audit/bp009-instrument-r7)
- **Commit en GitHub:** [`02a41f7d8da611ad8569a38e84b27c1ed094f403`](https://github.com/Santirobm/binpack-SDK/commit/02a41f7d8da611ad8569a38e84b27c1ed094f403)
- **Condición Git del Commit:** *Unsigned* (sin firma GPG verificada, registrado como condición de procedencia).
- **Git Blob SHA-1:** `262137c5cab2d8bf9373f605edbdee26ff86a9a4`
- **Dimensión del Instrumento:** `61.729 Bytes` (1.204 líneas, terminadores LF)
- **SHA-256 Declarado del Instrumento R7.1:**  
  `72f5002bd80939d77a1032c59771d62083571eb555d1295db14a70ff7b21d828`
- **Verificación de Sintaxis:** Validado con `py_compile` (código de salida 0).

### 5.2 Corrección Aritmética del Oráculo Canterbury (R7 $\rightarrow$ R7.1)
En la ejecución preliminar de R7, los 32 artefactos y los 23 cotejos de extracción resultaron conformes, pero el proceso finalizó con código 1 debido a una errata de $10.000\text{ Bytes}$ en la constante de comprobación de la suma neta Canterbury (`2800784` en vez de `2810784`).  
La Revisión R7.1 subsanó exclusivamente dicha constante, respaldada por la identidad matemática formal:
$$\begin{aligned}
\text{Suma neta de los 11 miembros:} & \quad 2.810.784\text{ B} \\
11\text{ cabeceras TAR }(11 \times 512\text{ B}): & \quad 5.632\text{ B} \\
\text{Relleno de miembros a múltiplos de } 512\text{ B}: & \quad 3.680\text{ B} \\
\text{Dos bloques nulos de terminación EOF }(2 \times 512\text{ B}): & \quad 1.024\text{ B} \\
\hline
\mathbf{\text{Total Stream TAR Consumido:}} & \quad \mathbf{2.821.120\text{ Bytes}}
\end{aligned}$$

### 5.3 Resultados de la Ejecución Material Informada de R7.1
- **Fecha y Hora UTC:** `2026-11-10T19:56:08.638593+00:00`
- **Comando:** `python scratch/audit_corpus_verification.py --repo-root C:\Users\sgi.lm\Desktop\binpack-SDK`
- **Código de Salida:** **`0`** (Éxito completo)
- **Desglose de Resultados:**
  1. **Matriz de Artefactos Físicos (32/32 PASS):** Recalculados desde sus bytes en disco, bajo control de estabilidad de descriptor `os.fstat()` (ausencia de reparse tags, invarianza de tamaño, dispositivo, inodo y marcas temporales durante la lectura). Coincidencia biyectiva con el manifiesto canónico `tests/experiments/datasets_checksums.json` (SHA-256: `2fb73af5...`).
  2. **Extracción y Correspondencia Silesia ZIP (12/12 PASS):** Descriptor único retenido (`silesia.zip`), validación de no-directorio y atributos DOS. Descompresión agregada observada: `211.938.580 B` (dentro del presupuesto de $250\text{ MiB} = 262.144.000\text{ B}$).
  3. **Extracción y Correspondencia Canterbury TAR.GZ (11/11 PASS):** Procesamiento secuencial sin desplazamiento (`mode="r|"`, sin métodos `seek()` ni `tell()`). Los 11 miembros coincidieron individualmente con los archivos físicos. Drenaje de búfer confirmó bytes remanentes exclusivamente nulos (`all(b == 0)`). Lectura post-terminación en `gz_stream.read(1)` retornó `b""`, validando fin de archivo y pie de página gzip (CRC32 e ISIZE). Consumo de stream TAR: `2.821.120 B` (dentro del presupuesto de $5\text{ MiB} = 5.242.880\text{ B}$).
  4. **Entorno Git Pre y Post-Condición (PASS):** Se comprobó árbol 100% limpio (0 modificaciones, 0 untracked no ignorados) al inicio y al final, rama `audit/bp1-core-safety`, commit canónico `4500566652490b9dfcb78341d5f9a3c584060c6a`, y coincidencia estricta con `git rev-parse --show-toplevel`.
  5. **Diagnóstico Global:** **`CONFORME AL 100% (PASS)`**.

---

## 6. Dictamen Formal de Cierre de Auditoría — BP-009 / OT-03 y Fase BP-009

En virtud de la evidencia forense presentada y de las verificaciones independientes realizadas sobre el repositorio:

### 6.1 Dictamen Emitido por el Auditor Líder
- **Estado de OT-03:** **APROBADA — CONFORMIDAD MATERIAL INFORMADA Y CONTRASTADA DOCUMENTALMENTE**.
- **Cierre de Fase:** **BP-009 FORMALMENTE CERRADA**.
- **Alcance de la Verificación Independiente:**  
  *La auditoría verificó de forma independiente la publicación en GitHub, el diff de cambios, el blob SHA-1 del instrumento R7.1 y del manifiesto, y recalculó las identidades matemáticas del stream TAR. La ejecución sobre el sistema de archivos local y el cálculo de hashes en disco quedan aceptados sobre la base de la evidencia reportada y contrastada documentalmente, sin constituir una reproducción física efectuada directamente por el auditor.*

---

## 7. Cuadro Consolidado de Situación de Hitos y Límites Operativos

| Hito / Orden de Trabajo | Alcance y Contenido Técnico | Estado | Determinación de Auditoría |
| :--- | :--- | :---: | :--- |
| **BP-008C** | Arnés de benchmarking y control de fugas de memoria | **Cerrado** | Certificado en commit `a9fe52e` |
| **BP-008D** | Protocolo experimental decisivo (R14) | **Cerrado** | Certificado en commit `9afd69a` |
| **BP-009 / OT-01** | Diagnóstico, desvío de rutas y reconciliación de 32 artefactos | **Cerrada** | Certificada documentalmente en commit `672d4cb` |
| **BP-009 / OT-02** | Arnés de adquisición y preparación del corpus (R13) | **Cerrada** | Certificada formalmente en commit `cb022fb` |
| **BP-009 / OT-03** | Verificación material física del corpus e instrumento forense R7.1 | **Cerrada** | Aprobada conforme al dictamen de cierre |
| **Fase BP-009 (Consolidado)** | **Validación Material Integral del Corpus y Arnés** | **CERRADA** | **Fase formalmente concluida y archivada** |
| **Hito BP-010** | **Campaña Decisiva de Benchmarking (10 repeticiones)** | **ESTRICTAMENTE CONGELADO** | **No habilitado**. Requiere autorización expresa y separada |
| **Código Productivo** | Árbol de código fuente del SDK | **Inalterable** | `src/`, `include/` y `deps/` 100% congelados (*code freeze*) |

---

## 8. Resolución y Próximos Pasos

Con la aprobación y cierre formal de **BP-009 / OT-03**, la totalidad de los requisitos previos de integridad del corpus de prueba han quedado satisfechos y contrastados.  
Cualquier avance hacia la apertura o planificación del **Hito BP-010 (Campaña Decisiva de Benchmarking)** queda supeditado a la autorización previa, expresa y separada del Auditor Líder.
