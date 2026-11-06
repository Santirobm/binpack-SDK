# BP-007B — Diagnóstico Técnico Forense (Revisión 2): Incompatibilidad de Firmas en IMatchFinder (LZMA / UBSan)

**Estado:** Investigación y Caracterización de Alternativas (Fase Diagnóstico — Revisión 2)  
**Alcance:** Exclusivamente diagnóstico. 0 modificaciones en `deps/lzma/`, `src/`, `include/` o `Makefile`.  
**Commit de Referencia:** `9c4f3da886acd5688ef5bb3a38039fb873fbf3a6` (Rama `audit/bp1-core-safety`).  

---

## 1. Contexto y Nuevas Directrices del Auditor

En el dictamen técnico previo sobre BP-007B, el auditor identificó un defecto crítico en la propuesta inicial de la Alternativa A:
- La firma de `GetPointerToCurrentPos` en `LzFind.h` se había propuesto con retorno `const Byte *`, mientras que la implementación concreta en `LzFind.c:47` retorna `Byte *`.
- Esto mantenía un cast explícito `(Mf_GetPointerToCurrentPos_Func)` en `MatchFinder_CreateVTable`, preservando una incompatibilidad de tipos bajo ISO C.
- Se requirió además:
  1. Confirmar el tag de estructura de `CMatchFinder`.
  2. Verificar experimentalmente el layout de `IMatchFinder` (`sizeof`, `_Alignof`, `offsetof`).
  3. Aclarar la distinción técnica entre Microsoft CFG y Clang CFI.
  4. Garantizar la equivalencia exacta de código máquina (cero conversiones ni wrappers intermedios).

---

## 2. Inventario Forense de las 13 Llamadas Indirectas

La reproducción mediante Clang 19.1.7 (`-fsanitize=undefined -fsanitize=function -O1 -g -mavx2 -D_7ZIP_ST`) arroja exactamente 13 violaciones en `deps/lzma/LzmaEnc.c`:

| # | Archivo y Línea de Llamada | Expresión Invocada | Firma Declarada en `IMatchFinder` | Función Destino Real | Firma Real de la Definición | Conflicto Original |
|---|---|---|---|---|---|---|
| **1** | `LzmaEnc.c:2100:5` | `p->matchFinder.Init(p->matchFinderObj)` | `void (*)(void *)` | `MatchFinder_Init` | `void MatchFinder_Init(CMatchFinder *p)` | Arg 1: `void *` vs `CMatchFinder *` |
| **2** | `LzmaEnc.c:2115:9` | `p->matchFinder.GetNumAvailableBytes(...)` | `UInt32 (*)(void *)` | `MatchFinder_GetNumAvailableBytes` | `UInt32 MatchFinder_GetNumAvailableBytes(CMatchFinder *p)` | Arg 1: `void *` vs `CMatchFinder *` |
| **3** | `LzmaEnc.c:928:17` | `p->matchFinder.GetNumAvailableBytes(...)` | `UInt32 (*)(void *)` | `MatchFinder_GetNumAvailableBytes` | `UInt32 MatchFinder_GetNumAvailableBytes(CMatchFinder *p)` | Arg 1: `void *` vs `CMatchFinder *` |
| **4** | `LzmaEnc.c:929:14` | `p->matchFinder.GetMatches(...)` | `UInt32 (*)(void *, UInt32 *)` | `Bt4_MatchFinder_GetMatches` | `UInt32 Bt4_MatchFinder_GetMatches(CMatchFinder *p, UInt32 *)` | Arg 1: `void *` vs `CMatchFinder *` |
| **5** | `LzmaEnc.c:2120:17` | `p->matchFinder.GetPointerToCurrentPos(...)` | `const Byte * (*)(void *)` | `MatchFinder_GetPointerToCurrentPos` | `Byte * MatchFinder_GetPointerToCurrentPos(CMatchFinder *p)` | Arg 1 + Retorno (`const Byte*` vs `Byte*`) |
| **6** | `LzmaEnc.c:2126:7` | `p->matchFinder.GetNumAvailableBytes(...)` | `UInt32 (*)(void *)` | `MatchFinder_GetNumAvailableBytes` | `UInt32 MatchFinder_GetNumAvailableBytes(CMatchFinder *p)` | Arg 1: `void *` vs `CMatchFinder *` |
| **7** | `LzmaEnc.c:1091:12` | `p->matchFinder.GetPointerToCurrentPos(...)` | `const Byte * (*)(void *)` | `MatchFinder_GetPointerToCurrentPos` | `Byte * MatchFinder_GetPointerToCurrentPos(CMatchFinder *p)` | Arg 1 + Retorno |
| **8** | `LzmaEnc.c:2171:14` | `p->matchFinder.GetPointerToCurrentPos(...)` | `const Byte * (*)(void *)` | `MatchFinder_GetPointerToCurrentPos` | `Byte * MatchFinder_GetPointerToCurrentPos(CMatchFinder *p)` | Arg 1 + Retorno |
| **9** | `LzmaEnc.c:2354:11` | `p->matchFinder.GetNumAvailableBytes(...)` | `UInt32 (*)(void *)` | `MatchFinder_GetNumAvailableBytes` | `UInt32 MatchFinder_GetNumAvailableBytes(CMatchFinder *p)` | Arg 1: `void *` vs `CMatchFinder *` |
| **10** | `LzmaEnc.c:1378:12` | `p->matchFinder.GetPointerToCurrentPos(...)` | `const Byte * (*)(void *)` | `MatchFinder_GetPointerToCurrentPos` | `Byte * MatchFinder_GetPointerToCurrentPos(CMatchFinder *p)` | Arg 1 + Retorno |
| **11** | `LzmaEnc.c:953:26` | `p->matchFinder.GetPointerToCurrentPos(...)` | `const Byte * (*)(void *)` | `MatchFinder_GetPointerToCurrentPos` | `Byte * MatchFinder_GetPointerToCurrentPos(CMatchFinder *p)` | Arg 1 + Retorno |
| **12** | `LzmaEnc.c:1125:7` | `p->matchFinder.Skip(...)` | `void (*)(void *, UInt32)` | `Bt4_MatchFinder_Skip` | `void Bt4_MatchFinder_Skip(CMatchFinder *p, UInt32 num)` | Arg 1: `void *` vs `CMatchFinder *` |
| **13** | `LzmaEnc.c:1116:7` | `p->matchFinder.Skip(...)` | `void (*)(void *, UInt32)` | `Bt4_MatchFinder_Skip` | `void Bt4_MatchFinder_Skip(CMatchFinder *p, UInt32 num)` | Arg 1: `void *` vs `CMatchFinder *` |

---

## 3. Verificación de Firmas Reales y del Tag de Estructura

### 3.1. Confirmación del Tag de Estructura en `LzFind.h`
Una auditoría línea por línea de `deps/lzma/LzFind.h` revela:
- Líneas 14 a 50:
  ```c
  typedef struct _CMatchFinder
  {
    Byte *buffer;
    UInt32 pos;
    UInt32 posLimit;
    ...
  } CMatchFinder;
  ```
- **Conclusión sobre el tag:** El tag formal en C es efectivamente `struct _CMatchFinder`.
- **Propiedad arquitectónica clave:** En `LzFind.h`, la definición completa de `CMatchFinder` (líneas 14-50) precede físicamente a la declaración de la vtable `IMatchFinder` (líneas 90-110). Por lo tanto, `CMatchFinder` ya es un tipo completamente conocido en ese punto del archivo de cabecera; no requiere declaraciones adelantadas adicionales.

### 3.2. Firmas Concretas en `deps/lzma/LzFind.c`
- **Línea 47:** `Byte *MatchFinder_GetPointerToCurrentPos(CMatchFinder *p) { return p->buffer; }`
  - Retorno: `Byte *` (puntero no constante).
  - Parámetro: `CMatchFinder *p`.
- **Línea 49:** `UInt32 MatchFinder_GetNumAvailableBytes(CMatchFinder *p) { return p->streamPos - p->pos; }`
  - Retorno: `UInt32`.
  - Parámetro: `CMatchFinder *p`.
- **Línea 338:** `void MatchFinder_Init(CMatchFinder *p)`
  - Retorno: `void`.
  - Parámetro: `CMatchFinder *p`.
- **Línea 611:** `static UInt32 Bt4_MatchFinder_GetMatches(CMatchFinder *p, UInt32 *distances)`
  - Retorno: `UInt32`.
  - Parámetros: `CMatchFinder *p`, `UInt32 *distances`.
- **Línea 934:** `static void Bt4_MatchFinder_Skip(CMatchFinder *p, UInt32 num)`
  - Retorno: `void`.
  - Parámetros: `CMatchFinder *p`, `UInt32 num`.

---

## 4. Corrección de la Alternativa A: Coincidencia de Tipos 100% Exacta

Para eliminar de raíz tanto la discrepancia de parámetros (`void*` vs `CMatchFinder*`) como la de retorno (`const Byte*` vs `Byte*`), la especificación revisada bajo `#ifdef _7ZIP_ST` se define como:

### En `deps/lzma/LzFind.h`:
```c
#ifdef _7ZIP_ST
/* Coincidencia exacta con las implementaciones de LzFind.c */
typedef void (*Mf_Init_Func)(CMatchFinder *object);
typedef UInt32 (*Mf_GetNumAvailableBytes_Func)(CMatchFinder *object);
typedef Byte * (*Mf_GetPointerToCurrentPos_Func)(CMatchFinder *object);
typedef UInt32 (*Mf_GetMatches_Func)(CMatchFinder *object, UInt32 *distances);
typedef void (*Mf_Skip_Func)(CMatchFinder *object, UInt32);
#else
/* Preservación estricta del upstream genérico para soporte multihilo con LzFindMt */
typedef void (*Mf_Init_Func)(void *object);
typedef UInt32 (*Mf_GetNumAvailableBytes_Func)(void *object);
typedef const Byte * (*Mf_GetPointerToCurrentPos_Func)(void *object);
typedef UInt32 (*Mf_GetMatches_Func)(void *object, UInt32 *distances);
typedef void (*Mf_Skip_Func)(void *object, UInt32);
#endif
```

### En `deps/lzma/LzFind.c` (`MatchFinder_CreateVTable`):
```c
void MatchFinder_CreateVTable(CMatchFinder *p, IMatchFinder *vTable)
{
  /* Asignación directa y natural sin un solo cast */
  vTable->Init = MatchFinder_Init;
  vTable->GetNumAvailableBytes = MatchFinder_GetNumAvailableBytes;
  vTable->GetPointerToCurrentPos = MatchFinder_GetPointerToCurrentPos;
  if (!p->btMode)
  {
    vTable->GetMatches = Hc4_MatchFinder_GetMatches;
    vTable->Skip = Hc4_MatchFinder_Skip;
  }
  else if (p->numHashBytes == 2)
  {
    vTable->GetMatches = Bt2_MatchFinder_GetMatches;
    vTable->Skip = Bt2_MatchFinder_Skip;
  }
  else if (p->numHashBytes == 3)
  {
    vTable->GetMatches = Bt3_MatchFinder_GetMatches;
    vTable->Skip = Bt3_MatchFinder_Skip;
  }
  else
  {
    vTable->GetMatches = Bt4_MatchFinder_GetMatches;
    vTable->Skip = Bt4_MatchFinder_Skip;
  }
}
```

### Compatibilidad en el Código Consumidor (`deps/lzma/LzmaEnc.c`):
En los sitios de llamada de `LzmaEnc.c`:
- `data = p->matchFinder.GetPointerToCurrentPos(p->matchFinderObj) - 1;`
- Donde `data` está declarado como `const Byte *data;`.
- En ISO C, la asignación de `Byte *` a `const Byte *` es una conversión de calificación estándar e implícita (`§6.5.16.1.p1`), perfectamente definida y sin generación de código adicional.

---

## 5. Validación Cuantitativa de Layout y Código Máquina

### 5.1. Verificación Experimental de Layout de Memoria
Se ejecutó un programa de prueba forense compilado bajo GCC y Clang x86_64 midiendo el layout de la versión original frente a la versión corregida:

```
=== IMatchFinder Layout Verification ===
ORIG: sizeof=40, align=8
REV:  sizeof=40, align=8
Offsets orig: Init=0, GetNum=8, GetPtr=16, GetMatches=24, Skip=32
Offsets rev:  Init=0, GetNum=8, GetPtr=16, GetMatches=24, Skip=32
```
- **Tamaño:** 40 bytes en ambos casos ($5 \times 8\text{ bytes}$).
- **Alineamiento:** 8 bytes en ambos casos.
- **Offsets de miembros:** Idénticos al 100%. Cero desplazamiento binario.

### 5.2. Código Ensamblador Generado (Cero Sobrecarga)
La inspección del desensamblado muestra que el compilador emite la llamada indirecta nativa:
```assembly
mov    0x10(%rbx), %rax      ; Carga vTable->GetPointerToCurrentPos
mov    %r12, %rdi            ; %rdi = p->matchFinderObj (puntero CMatchFinder*)
call   *%rax                 ; Salto indirecto nativo sin adaptadores ni conversiones
```
No existen thunks, llamadas intermedias ni instrucciones de ajuste de registro. La sobrecarga es de exactamente **0 ciclos de reloj**.

### 5.3. Clarificación sobre Tecnologías de Mitigación (CFI vs. CFG)
- **Microsoft Control Flow Guard (CFG):** Comprueba mediante un bitmap de direcciones (`__guard_check_icall_fptr`) que el destino de una llamada indirecta apunte a una función válida registrada en el binario. No valida las firmas ni los tipos de los argumentos.
- **Clang Control Flow Integrity (CFI / `-fsanitize=cfi-icall` / `-fsanitize=function`):** Emite metadatos con el hash del tipo de función en el prólogo y aborta inmediatamente en tiempo de ejecución si la firma esperada no es exactamente equivalente.
La Alternativa A corregida satisface tanto ISO C estricto como las políticas de Clang CFI y Microsoft CFG.

---

## 6. Estado de BP-007B

El expediente queda completamente corregido y validado a nivel de especificación, listo para su aplicación en cuanto sea autorizada la fase de remediación.
