# BinPack Enterprise SDK

**Estado: ARCHIVED — Desarrollo comercial discontinuado**

BinPack fue un proyecto independiente de investigación y desarrollo de un motor de compresión de datos escrito en C, orientado principalmente a datos numéricos, telemetría, series temporales y estructuras binarias.

El objetivo fue desarrollar una alternativa especializada a compresores generalistas como Zstandard y LZ4, mediante preprocesamiento SIMD, transposición de datos, Byte-Delta y distintos backends de compresión.

El proyecto alcanzó una implementación funcional, incorporó mecanismos de streaming, formatos de contenedor, optimizaciones específicas de arquitectura y herramientas de evaluación. Sin embargo, las campañas comparativas posteriores no demostraron una ventaja suficientemente consistente en rendimiento, relación de compresión y acceso selectivo como para justificar su continuidad como producto comercial.

El desarrollo activo se encuentra discontinuado. Este repositorio se conserva como archivo técnico e histórico del proyecto.

---

## Resultado final

La evaluación final mostró resultados mixtos y dependientes del tipo de datos. BinPack consiguió mejoras de compresión en determinados escenarios, pero no alcanzó una ventaja general frente a las alternativas existentes.

### BP-011 — Evaluación de codecs para series temporales

Resultados reportados en la campaña comparativa:

| Dataset | Codec | Tamaño comprimido |
| :--- | :--- | :--- |
| C-MAPSS | Gorilla | 2.024.295 B |
| C-MAPSS | Chimp128 | 1.128.382 B |
| C-MAPSS | BinPack Transpose | 1.249.969 B |
| C-MAPSS | Zstandard L3 | 609.301 B |
| IMS | Gorilla | 740.936 B |
| IMS | Chimp128 | 703.551 B |
| IMS | BinPack | 672.329 B |
| IMS | Zstandard L3 | 632.404 B |

**Interpretación:**
- En C-MAPSS, BinPack produjo archivos un 38,25 % más pequeños que Gorilla, pero un 10,78 % más grandes que Chimp128.
- En IMS, produjo archivos un 9,26 % más pequeños que Gorilla y un 4,44 % más pequeños que Chimp128.
- Zstandard L3 consiguió archivos más pequeños en ambos datasets.
- La velocidad de compresión de BinPack fue considerablemente inferior a Gorilla: aproximadamente 0,05× en C-MAPSS y 0,10× en IMS.
- Las ventajas de acceso selectivo no alcanzaron los criterios establecidos para demostrar una ventaja general o un nicho comercial concluyente.

La campaña documentó resultados experimentales, pero no justificó presentar BinPack como reemplazo superior de Gorilla, Chimp128 o Zstandard.

### BP-010 — Benchmarking general

La campaña evaluó 28 configuraciones y registró 1.060 mediciones, con controles de integridad y análisis de estabilidad.
- 13 configuraciones fueron clasificadas como estables.
- 15 configuraciones fueron clasificadas como inestables.
- Se documentaron resultados favorables en determinadas cargas numéricas y estructuradas.
- No se demostró superioridad consistente en cargas generales.

Los resultados históricos iniciales fueron prometedores, pero no se tradujeron en una ventaja universal reproducible.

---

## Desarrollo técnico realizado

Durante su desarrollo, BinPack incorporó:
- Núcleo de compresión en C99/C11.
- Preprocesamiento numérico mediante SIMD AVX2.
- Stride Transposition y Byte-Delta.
- Integración de backends basados en tecnologías de compresión existentes.
- Modos Runtime, Default y Archival.
- Arquitectura de streaming y contenedores BPK2.
- Funcionalidades de compresión diferencial y OTA.
- Soporte para distintos tipos de datos y arquitecturas.
- Interfaces y herramientas para evaluación e integración.
- Pruebas de límites, concurrencia, memoria, seguridad y compatibilidad.

El repositorio conserva tanto la implementación como el historial de decisiones técnicas y evaluaciones.

---

## Historial de validación y auditorías

El desarrollo pasó por distintas etapas formales de revisión:

| Hito | Alcance | Resultado |
| :--- | :--- | :--- |
| **BP-001 a BP-007** | Seguridad, límites, concurrencia, memoria y componentes internos | Cerrados |
| **BP-008C / BP-008D** | Auditoría del sistema de benchmarking | Aprobados |
| **BP-009** | Preparación, integridad de corpus y validación experimental | Cerrado |
| **BP-010** | Campaña comparativa general | Completada; resultados mixtos |
| **BP-011** | Evaluación de codecs de series temporales | Fases A-E cerradas y documentadas |

*Nota:* La aprobación de auditorías de ingeniería no debe confundirse con una certificación de superioridad comercial o de rendimiento.

### Referencias históricas
- BP-010: `922f8f1` y subsanación `5f4fea4`.
- BP-011 Fase A: `ee2a2c9`.
- BP-011 Diseño técnico Fase C: `64b5cc6`.
- BP-011 Diseño Fase D: `4323064`.
- BP-011 Campaña Fase E: `459630b`.

Los documentos y commits asociados conservan el detalle metodológico y la trazabilidad de las evaluaciones.

---

## Conclusión

BinPack consiguió materializar una arquitectura de compresión especializada y resolver problemas técnicos reales durante su desarrollo. Sin embargo, el resultado experimental final no alcanzó el nivel de ventaja competitiva, estabilidad y rendimiento necesario para justificar su comercialización frente a alternativas consolidadas.

El proyecto se considera cerrado como iniciativa de producto.

No se recomienda su adopción en producción sobre la base de las afirmaciones comerciales históricas de este repositorio. Cualquier reutilización futura deberá realizar su propia evaluación comparativa, con datasets, hardware y criterios independientes.

Este repositorio permanece público como registro del desarrollo, sus experimentos, sus resultados y las lecciones técnicas obtenidas.

---

- **Author:** Santiago Robledo
- **Repository:** https://github.com/Santirobm/binpack-SDK
- **License:** See LICENSE
- **Project status:** Archived / No active commercial development
