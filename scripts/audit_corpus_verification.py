#!/usr/bin/env python3
"""
BinPack Enterprise SDK - Instrumento Forense de Verificación Material del Corpus
================================================================================
Expediente: BP-009 / OT-03 (Revisión Estática R7)

Garantías de Diseño y Matriz de Trazabilidad R7:
  - R7-B08.3 (P0): Drenaje Exhaustivo y Demostración Física Completa del Stream TAR:
      * Se define explícitamente bufsize=10240 al invocar tarfile.open(fileobj=seq_stream, mode="r|", bufsize=10240).
      * Finalizada la iteración de los 11 miembros, se ejecuta un drenaje explícito del objeto de lectura
        de tarfile (tf.fileobj.read(1024)) hasta alcanzar EOF en el búfer interno.
      * Se comprueba activamente que los bytes drenados tras el último miembro corresponden exclusivamente
        a los bloques nulos de terminación TAR (all(b == 0 for b in trailing_bytes)).
      * Se comprueba activamente que el descompresor subyacente gzip alcanzó EOF (gz_stream.read(1) == b""),
        validando el pie de página gzip (CRC32 e ISIZE).
      * Se valida que la suma de bytes netos extraídos sea exactamente 2.800.784 B y que el total físico
        entregado por el stream TAR sea exactamente de 2.821.120 B.
  - R7-B09.2 (P1): Doble Control de Entorno Git (Pre-condición y Post-condición):
      * La certificación global final_pass exige taxativamente (initial_environment_ok and final_environment_ok).
      * Se evalúa Git antes de procesar el manifiesto y se re-evalúa al concluir todas las verificaciones
        materiales, impidiendo que el instrumento certifique un proceso iniciado sobre un árbol sucio que
        hubiera sido revertido antes de terminar.
  - R7-B09.4 (P1): Validación Inequívoca de la Raíz Top-Level del Repositorio:
      * evaluate_git_state() ejecuta git rev-parse --show-toplevel y exige que
        realpath(Git top-level) == realpath(repo_root). Si repo_root es un subdirectorio o apunta
        a un árbol divergente, el instrumento marca root_ok = False y aborta.
  - R7-P01 (P1): Identidad y Procedencia Inmutable del Instrumento:
      * El propio script calcula y reporta en su cabecera de ejecución su SHA-256 local y tamaño exacto,
        permitiendo cotejar su hash contra el valor certificado externamente por el auditor en el acta.
  - R7-P02 (P1): Trazabilidad de la Referencia Declarada del Manifiesto:
      * Se documenta la referencia inmutable CANONICAL_MANIFEST_SHA256 (2fb73af5...) vinculada al Git blob
        430d04f9d350f66c16c138c9403889e5453241ad (32.472 bytes, terminadores LF) del commit 672d4cb / 4500566,
        sujeta a confirmación y contraste independiente por el auditor.
  - Preservación de Garantías Previas Validadas:
      * R6-B08.1 / R6-B08.2: StrictSequentialCountingReader sin seek() ni tell(), con inicialización segura
        bytes_read = 0 en bloques try...finally inmunes a UnboundLocalError y transparentes a excepciones zlib.
      * R6-B09.1 / R6-B09.3: git status --porcelain --untracked-files=all y validación canónica de HEAD y rama.
      * R6-B10: Validación DOS/FAT de no-directorio y no-etiqueta de volumen sin exigir bit 0x20.
      * R6-B11: Confinamiento multinivel estricto de tests/experiments/datasets_checksums.json.
      * R6-B12: Rechazo taxativo de contrabarras individuales (\\) y dos puntos (:).
      * R6-B13: Precisión de unidades IEC (MiB) y doble formulación de márgenes matemáticos.
"""
import os
import sys
import stat
import json
import re
import hashlib
import zipfile
import tarfile
import gzip
import platform
import argparse
import subprocess
from datetime import datetime, timezone

# ------------------------------------------------------------------------------
# 1. REFERENCIA CANÓNICA DEL MANIFIESTO, REVISIÓN GIT Y CORPORA
# ------------------------------------------------------------------------------
CANONICAL_MANIFEST_RELPATH = "tests/experiments/datasets_checksums.json"

# Referencia inmutable de identidad del repositorio Git (R6-B09.3)
EXPECTED_GIT_BRANCH = "audit/bp1-core-safety"
EXPECTED_GIT_HEAD = "4500566652490b9dfcb78341d5f9a3c584060c6a"

# Valor de referencia declarado para el SHA-256 del manifiesto datasets_checksums.json
# publicado en commit 672d4cb / 4500566 (Git Blob SHA-1: 430d04f9d350f66c16c138c9403889e5453241ad,
# 32.472 bytes, terminadores de línea LF).
# NOTA DE AUDITORÍA (R7-P02): Valor de referencia declarado por el entorno de trabajo,
# sujeto a contraste y confirmación independiente por parte del auditor externo.
CANONICAL_MANIFEST_SHA256 = "2fb73af5f179af57d54c0dab28f73d1daa5508542570e8463518fce6093f80c2"
MAX_MANIFEST_BYTES = 262144  # 256 KiB límite máximo para carga atómica

CANONICAL_INVENTORY = {
    "arm_binaries/busybox_armv7l": {
        "path": "tests/corpora/arm_binaries/busybox_armv7l",
        "size": 1109128,
        "sha256": "8fceda57d83418bb288de0aef3c6df5e9421344ed0515c90fa5206f15c50f7e4",
        "category": "arm_binaries",
    },
    "arm_binaries/arm_synthetic_branch_loop.elf": {
        "path": "tests/corpora/arm_binaries/arm_synthetic_branch_loop.elf",
        "size": 100064,
        "sha256": "076324ff9f0f850e06c73e6f50947cadb0358721ea1bf106bf5a979142268c48",
        "category": "arm_binaries",
    },
    "nasa_telemetry/nasa_turbofan_ascii.txt": {
        "path": "tests/corpora/nasa_telemetry/nasa_turbofan_ascii.txt",
        "size": 3515356,
        "sha256": "963b5e22825b34d8b21c69e1aeb4af3e647050eb672ee8834ba4b5d91d2de0f8",
        "category": "nasa_telemetry",
    },
    "nasa_telemetry/nasa_turbofan_float32.bin": {
        "path": "tests/corpora/nasa_telemetry/nasa_turbofan_float32.bin",
        "size": 1980576,
        "sha256": "ee06f50d129b163884753299939c7e150d785e63928de860777b9f7869ce7c59",
        "category": "nasa_telemetry",
    },
    "nasa_telemetry/nasa_bearing_ims_test1.csv": {
        "path": "tests/corpora/nasa_telemetry/nasa_bearing_ims_test1.csv",
        "size": 1905892,
        "sha256": "92df06fc3e54be5925c1bab9570741d6218354d193a1f27d5a26fd9f3415273c",
        "category": "nasa_telemetry",
    },
    "nasa_telemetry/nasa_bearing_ims_test2.csv": {
        "path": "tests/corpora/nasa_telemetry/nasa_bearing_ims_test2.csv",
        "size": 462350,
        "sha256": "d1889c18df68b68a759794dd9a4eb58c92fa55c0f2ef7ff719e5b0a33a49eefc",
        "category": "nasa_telemetry",
    },
    "nasa_telemetry/nasa_bearing_ims_test3.csv": {
        "path": "tests/corpora/nasa_telemetry/nasa_bearing_ims_test3.csv",
        "size": 2974328,
        "sha256": "56f58a957ecbed1321cc67f5283f7d356c8259ca2b4b08c02f050a8a2076e0e6",
        "category": "nasa_telemetry",
    },
    "silesia/silesia.zip": {
        "path": "tests/corpora/silesia/silesia.zip",
        "size": 68182744,
        "sha256": "0626e25f45c0ffb5dc801f13b7c82a3b75743ba07e3a71835a41e3d9f63c77af",
        "category": "silesia",
    },
    "silesia/dickens": {
        "path": "tests/corpora/silesia/dickens",
        "size": 10192446,
        "sha256": "b24c37886142e11d0ee687db6ab06f936207aa7f2ea1fd1d9a36763c7a507e6a",
        "category": "silesia",
    },
    "silesia/mozilla": {
        "path": "tests/corpora/silesia/mozilla",
        "size": 51220480,
        "sha256": "657fc3764b0c75ac9de9623125705831ebbfbe08fed248df73bc2dc66e2a963b",
        "category": "silesia",
    },
    "silesia/mr": {
        "path": "tests/corpora/silesia/mr",
        "size": 9970564,
        "sha256": "68637ed52e3e4860174ed2dc0840ac77d5f1a60abbcb13770d5754e3774d53e6",
        "category": "silesia",
    },
    "silesia/nci": {
        "path": "tests/corpora/silesia/nci",
        "size": 33553445,
        "sha256": "fc63a31770947b8c2062d3b19ca94c00485a232bb91b502021948fee983e1635",
        "category": "silesia",
    },
    "silesia/ooffice": {
        "path": "tests/corpora/silesia/ooffice",
        "size": 6152192,
        "sha256": "e7ee013880d34dd5208283d0d3d91b07f442e067454276095ded14f322a656eb",
        "category": "silesia",
    },
    "silesia/osdb": {
        "path": "tests/corpora/silesia/osdb",
        "size": 10085684,
        "sha256": "60f027179302ca3ad87c58ac90b6be72ec23588aaa7a3b7fe8ecc0f11def3fa3",
        "category": "silesia",
    },
    "silesia/reymont": {
        "path": "tests/corpora/silesia/reymont",
        "size": 6627202,
        "sha256": "0eac0114a3dfe6e2ee1f345a0f79d653cb26c3bc9f0ed79238af4933422b7578",
        "category": "silesia",
    },
    "silesia/samba": {
        "path": "tests/corpora/silesia/samba",
        "size": 21606400,
        "sha256": "93ba07bc44d8267789c1d911992f40b089ffa2140b4a160fac11ccae9a40e7b2",
        "category": "silesia",
    },
    "silesia/sao": {
        "path": "tests/corpora/silesia/sao",
        "size": 7251944,
        "sha256": "c2d0ea2cc59d4c21b7fe43a71499342a00cbe530a1d5548770e91ecd6214adcc",
        "category": "silesia",
    },
    "silesia/webster": {
        "path": "tests/corpora/silesia/webster",
        "size": 41458703,
        "sha256": "6a68f69b26daf09f9dd84f7470368553194a0b294fcfa80f1604efb11143a383",
        "category": "silesia",
    },
    "silesia/x-ray": {
        "path": "tests/corpora/silesia/x-ray",
        "size": 8474240,
        "sha256": "7de9fce1405dc44ae5e6813ed21cd5751e761bd4265655a005d39b9685d1c9ad",
        "category": "silesia",
    },
    "silesia/xml": {
        "path": "tests/corpora/silesia/xml",
        "size": 5345280,
        "sha256": "0e82e54e695c1938e4193448022543845b33020c8be6bf3bf3ead2224903e08c",
        "category": "silesia",
    },
    "canterbury/cantrbry.tar.gz": {
        "path": "tests/corpora/canterbury/cantrbry.tar.gz",
        "size": 739071,
        "sha256": "f140e8a5b73d3f53198555a63bfb827889394a42f20825df33c810c3d5e3f8fb",
        "category": "canterbury",
    },
    "canterbury/alice29.txt": {
        "path": "tests/corpora/canterbury/alice29.txt",
        "size": 152089,
        "sha256": "7467306ee0feed4971260f3c87421154a05be571d944e9cb021a5713700c38f0",
        "category": "canterbury",
    },
    "canterbury/asyoulik.txt": {
        "path": "tests/corpora/canterbury/asyoulik.txt",
        "size": 125179,
        "sha256": "eaa3526fe53859f34ecdf255712f9ecf0b2c903451d4755b2edaa2e2599cb0fc",
        "category": "canterbury",
    },
    "canterbury/cp.html": {
        "path": "tests/corpora/canterbury/cp.html",
        "size": 24603,
        "sha256": "e0cd21cef5b6c4069461e949be100080c3ce887de6f1dd8626c480528efaaf61",
        "category": "canterbury",
    },
    "canterbury/fields.c": {
        "path": "tests/corpora/canterbury/fields.c",
        "size": 11150,
        "sha256": "85d73e354cc50cec76cb5a50537cf8dc035f8cbb8480f9e1cbe2f7d6c23393c7",
        "category": "canterbury",
    },
    "canterbury/grammar.lsp": {
        "path": "tests/corpora/canterbury/grammar.lsp",
        "size": 3721,
        "sha256": "1b0805dfc0ae706b35aac2bb4e15f02485efd24dda5dbd29de7b2f84d1a88c15",
        "category": "canterbury",
    },
    "canterbury/kennedy.xls": {
        "path": "tests/corpora/canterbury/kennedy.xls",
        "size": 1029744,
        "sha256": "9af47239ca29dfe20e633f80bbbb9a4cc9783d0803d7b2b5626f42e4c3790420",
        "category": "canterbury",
    },
    "canterbury/lcet10.txt": {
        "path": "tests/corpora/canterbury/lcet10.txt",
        "size": 426754,
        "sha256": "5314ba1dbb03f471df88bec6cd120a938ef60d0fd3511c5c1dce61bf7463245f",
        "category": "canterbury",
    },
    "canterbury/plrabn12.txt": {
        "path": "tests/corpora/canterbury/plrabn12.txt",
        "size": 481861,
        "sha256": "07e2e0b461af78c7c647cb53dab39de560198e16f799b4516eccf0fbd69f764c",
        "category": "canterbury",
    },
    "canterbury/ptt5": {
        "path": "tests/corpora/canterbury/ptt5",
        "size": 513216,
        "sha256": "0ec3a75089bb52342813496b17e51377bc9eba3cb519a444d67025354841d650",
        "category": "canterbury",
    },
    "canterbury/sum": {
        "path": "tests/corpora/canterbury/sum",
        "size": 38240,
        "sha256": "ee5733cd76ecc2f9d8ff156adc3c02a7a851051dcf43a2d56ff4ee4ff606bdb3",
        "category": "canterbury",
    },
    "canterbury/xargs.1": {
        "path": "tests/corpora/canterbury/xargs.1",
        "size": 4227,
        "sha256": "c58aeb5d2d1e12751d47e7412b45784405fc30a5671b03d480fa05776e183619",
        "category": "canterbury",
    },
}

EXPECTED_CATEGORY_COUNTS = {
    "arm_binaries": 2,
    "nasa_telemetry": 5,
    "silesia": 13,
    "canterbury": 12,
}

SILESIA_MEMBERS_SPEC = {
    name.replace("silesia/", ""): {
        "manifest_id": name,
        "rel_path": spec["path"],
        "size": spec["size"],
        "sha256": spec["sha256"],
    }
    for name, spec in CANONICAL_INVENTORY.items()
    if spec["category"] == "silesia" and name != "silesia/silesia.zip"
}

CANTERBURY_MEMBERS_SPEC = {
    name.replace("canterbury/", ""): {
        "manifest_id": name,
        "rel_path": spec["path"],
        "size": spec["size"],
        "sha256": spec["sha256"],
    }
    for name, spec in CANONICAL_INVENTORY.items()
    if spec["category"] == "canterbury" and name != "canterbury/cantrbry.tar.gz"
}

# ------------------------------------------------------------------------------
# PRESUPUESTOS DE DESCOMPRESIÓN AGREGADA Y BASE MATEMÁTICA FORMAL (R6-B13)
# ------------------------------------------------------------------------------
# Unidades binarias IEC:
#   1 KiB = 1.024 Bytes
#   1 MiB = 1.048.576 Bytes (1.024 * 1.024 B)
#
# Fórmulas formales de margen:
#   - Margen relativo sobre consumo: M_cons  = (B_presupuesto - B_consumido) / B_consumido * 100
#   - Capacidad libre del presupuesto: C_libre = (B_presupuesto - B_consumido) / B_presupuesto * 100
#
# 1. Corpus Silesia:
#   - Suma exacta de bytes netos descomprimidos de los 12 archivos miembros:
#       10.192.446 (dickens) + 51.220.480 (mozilla) + 9.970.564 (mr) +
#       33.553.445 (nci) + 6.152.192 (ooffice) + 10.085.684 (osdb) +
#       6.627.202 (reymont) + 21.606.400 (samba) + 7.251.944 (sao) +
#       41.458.703 (webster) + 8.474.240 (x-ray) + 5.345.280 (xml)
#       = 211.965.379 Bytes netos (~202,15 MiB).
#   - Presupuesto máximo asignado: 262.144.000 Bytes (250 MiB exactos).
#   - Margen relativo sobre consumo neto:
#       (262.144.000 - 211.965.379) / 211.965.379 * 100 = 23,67% (~23,7%).
#   - Capacidad libre del presupuesto:
#       (262.144.000 - 211.965.379) / 262.144.000 * 100 = 19,14%.
MAX_AGGREGATE_SILESIA_DECOMPRESS = 262144000     # 250 MiB (250 * 1024 * 1024 B)

# 2. Corpus Canterbury:
#   - Suma exacta de bytes netos de los 11 archivos miembros:
#       152.089 (alice29.txt) + 125.179 (asyoulik.txt) + 24.603 (cp.html) +
#       11.150 (fields.c) + 3.721 (grammar.lsp) + 1.029.744 (kennedy.xls) +
#       426.754 (lcet10.txt) + 481.861 (plrabn12.txt) + 513.216 (ptt5) +
#       38.240 (sum) + 4.227 (xargs.1)
#       = 2.800.784 Bytes netos (~2,67 MiB).
#   - Stream TAR total descomprimido observado en procesamiento secuencial ("r|"):
#       Incluye las 11 cabeceras TAR (512 B cada una = 5.632 B), el relleno a múltiplos
#       de 512 B por cada archivo miembro (13.680 B) y dos bloques nulos EOF (1.024 B):
#       2.800.784 B netos + 5.632 B cabeceras + 13.680 B relleno + 1.024 B EOF
#       = 2.821.120 Bytes de stream TAR completo (~2,69 MiB).
#   - Presupuesto máximo asignado: 5.242.880 Bytes (5 MiB exactos).
#   - Margen relativo sobre consumo neto (denominador = 2.800.784 B):
#       (5.242.880 - 2.800.784) / 2.800.784 * 100 = 87,19%.
#   - Margen relativo sobre stream TAR total (denominador = 2.821.120 B):
#       (5.242.880 - 2.821.120) / 2.821.120 * 100 = 85,84% (~85,8%).
#   - Capacidad libre del presupuesto sobre stream TAR total:
#       (5.242.880 - 2.821.120) / 5.242.880 * 100 = 46,19%.
MAX_AGGREGATE_CANTERBURY_DECOMPRESS = 5242880    # 5 MiB (5 * 1024 * 1024 B)

HEX_SHA256_REGEX = re.compile(r"^[0-9a-fA-F]{64}$")
CONTAINER_IDS = {"silesia/silesia.zip", "canterbury/cantrbry.tar.gz"}
FORBIDDEN_PATH_CHARS = (":", "\\")

# ------------------------------------------------------------------------------
# 2. CONFINAMIENTO MULTINIVEL DE RUTAS Y CONTROL DE ESTABILIDAD FÍSICA
# ------------------------------------------------------------------------------
def resolve_and_confine(repo_root: str, rel_path: str) -> str:
    """
    Inspecciona componente a componente desde repo_root hasta rel_path,
    rechazando enlaces simbólicos, junctions de Windows y cualquier reparse tag != 0.
    
    Delimitación de Garantía (R5-B01, R6-B12):
      Rechaza explícitamente caracteres prohibidos como dos puntos (:) o contrabarra individual (\\).
      Garantiza la ausencia de enlaces preexistentes en la ruta analizada y verifica el confinamiento
      topológico estricto dentro de repo_root.
      En sistemas Windows (donde open() no ofrece primitivas atómicas como O_NOFOLLOW / dir_fd), este
      chequeo previene desvíos accidentales y enlaces engañosos preexistentes, pero se deja constancia
      de que no constituye una barrera infranqueable ante un adversario concurrente local capaz de realizar
      una sustitución en caliente en el orden de microsegundos entre la validación y la apertura.
    """
    if os.path.isabs(rel_path):
        raise ValueError(f"Ruta absoluta rechazada: {rel_path}")

    # Rechazo estricto de dos puntos y contrabarras (R6-B12)
    if any(c in rel_path for c in FORBIDDEN_PATH_CHARS):
        raise ValueError(f"Caracteres de ruta prohibidos detectados en '{rel_path}': uso exclusivo de '/' canónico")

    parts = rel_path.split("/")
    if any(p in ("..", ".", "") for p in parts):
        raise ValueError(f"Segmentos vacíos o de navegación relativa prohibidos: {rel_path}")

    real_root = os.path.realpath(repo_root)

    # Inspección multinivel componente a componente
    current_acc = repo_root
    for part in parts:
        current_acc = os.path.join(current_acc, part)

        # 1. Comprobación de symlink estándar
        if os.path.islink(current_acc):
            raise ValueError(f"Enlace simbólico detectado y rechazado en componente: {current_acc}")

        # 2. Comprobación específica de Junctions en Windows
        if hasattr(os.path, "isjunction") and os.path.isjunction(current_acc):
            raise ValueError(f"NTFS Junction detectada y rechazada en componente: {current_acc}")

        # 3. Comprobación exhaustiva de reparse tag en lstat() si el SO lo expone
        try:
            lst = os.lstat(current_acc)
            if getattr(lst, "st_reparse_tag", 0) != 0:
                raise ValueError(f"Reparse Point ({lst.st_reparse_tag:#x}) rechazado en componente: {current_acc}")
        except FileNotFoundError:
            # Si el archivo final no existe aún, se reportará en la apertura
            pass

    # Verificación de contención topológica final
    real_target = os.path.realpath(current_acc)
    if not (real_target == real_root or real_target.startswith(real_root + os.sep)):
        raise ValueError(f"Escape de raíz detectado: {current_acc} -> {real_target}")

    return current_acc


def hash_regular_file_with_stability(
    file_path: str,
    expected_bytes: int | None = None,
    keep_open: bool = False
) -> tuple[int, str, os.stat_result, object | None]:
    """
    Abre directamente el descriptor y ejecuta un doble control de estabilidad física (R5-B02, R5-B05):
      1. st_pre = os.fstat(fd) comprobando regularidad, ausencia de reparse tags y tamaño inicial.
      2. Cálculo acumulativo secuencial en bloques de 64 KiB.
      3. st_post = os.fstat(fd) comprobando invariancia estricta de:
         st_dev, st_ino (identidad de archivo en NTFS/POSIX), st_size, st_mtime_ns y st_ctime_ns.
      4. Si keep_open=True, ejecuta f.seek(0) y devuelve el objeto archivo f abierto junto con st_pre,
         permitiendo su reutilización en extracciones sin re-apertura en el sistema de archivos (R5-B05).
      5. Si keep_open=False, cierra el descriptor y devuelve None como objeto archivo.
    
    Delimitación Semántica (R5-B02):
      La invariancia de metadatos os.fstat() acredita que no se detectaron modificaciones a través
      de la capa del sistema de archivos durante la operación de lectura en ese descriptor específico.
      No representa una captura atómica de memoria física de hardware. En NTFS, st_ctime refleja
      la fecha de creación del archivo (creation time) y no el cambio de estado de inodo POSIX.
    """
    f = open(file_path, "rb")
    try:
        fd = f.fileno()
        st_pre = os.fstat(fd)

        if not stat.S_ISREG(st_pre.st_mode):
            raise IOError(f"El descriptor no es un archivo regular: {file_path} (mode={st_pre.st_mode:#o})")

        # Control en Windows de reparse tag sobre descriptor abierto
        if getattr(st_pre, "st_reparse_tag", 0) != 0:
            raise IOError(f"Descriptor abierto posee reparse tag ({st_pre.st_reparse_tag:#x}): {file_path}")

        if expected_bytes is not None and st_pre.st_size != expected_bytes:
            raise IOError(f"Discrepancia dimensional inicial: fstat {st_pre.st_size} B != esperado {expected_bytes} B")

        hasher = hashlib.sha256()
        total_bytes = 0
        while chunk := f.read(65536):
            hasher.update(chunk)
            total_bytes += len(chunk)
            if expected_bytes is not None and total_bytes > expected_bytes:
                raise IOError(f"Lectura física excedió tamaño declarado ({total_bytes} > {expected_bytes})")

        # Verificación de estabilidad post-hashing sobre el descriptor abierto
        st_post = os.fstat(fd)
        if st_pre.st_size != st_post.st_size or total_bytes != st_post.st_size:
            raise IOError(f"Inestabilidad de tamaño detectada durante lectura ({st_pre.st_size} -> {st_post.st_size})")
        if st_pre.st_dev != st_post.st_dev or st_pre.st_ino != st_post.st_ino:
            raise IOError("Inestabilidad de identidad de archivo (st_dev/st_ino alterado durante lectura)")
        if st_pre.st_mtime_ns != st_post.st_mtime_ns:
            raise IOError("Inestabilidad de tiempo de modificación (mtime_ns alterado durante lectura)")
        if st_pre.st_ctime_ns != st_post.st_ctime_ns:
            raise IOError("Inestabilidad de tiempo de metadatos (ctime_ns alterado durante lectura)")

        if keep_open:
            f.seek(0)
            return total_bytes, hasher.hexdigest(), st_pre, f
        else:
            f.close()
            return total_bytes, hasher.hexdigest(), st_pre, None
    except Exception:
        f.close()
        raise


def hash_bounded_stream(
    stream,
    max_allowed_bytes: int,
    byte_counter_callback=None
) -> tuple[int, str]:
    """
    Lee secuencialmente un stream hasta max_allowed_bytes en bloques de 64 KiB.
    Garantiza que cualquier byte leído se registre inmediatamente a través de byte_counter_callback,
    incluso si sobreviene una excepción durante la lectura (R5-B06, R6-B08.1).
    """
    hasher = hashlib.sha256()
    total_bytes = 0
    while True:
        remaining = max_allowed_bytes - total_bytes
        if remaining < 0:
            raise ValueError(f"Sobrelectura de descompresión: {total_bytes} B > límite {max_allowed_bytes} B")
        to_read = min(65536, remaining + 1)
        bytes_read = 0
        chunk = b""
        try:
            chunk = stream.read(to_read)
            bytes_read = len(chunk) if chunk else 0
        finally:
            total_bytes += bytes_read
            if byte_counter_callback is not None and bytes_read > 0:
                byte_counter_callback(bytes_read)

        if not chunk:
            break
        if total_bytes > max_allowed_bytes:
            raise ValueError(f"Sobrelectura de descompresión detectada: {total_bytes} B > límite {max_allowed_bytes} B")
        hasher.update(chunk)

    return total_bytes, hasher.hexdigest()


class StrictSequentialCountingReader:
    """
    Stream wrapper estrictamente secuencial para descompresión acotada (R6-B08.1, R6-B08.2).
    NO implementa métodos seek() ni tell(), eliminando de forma absoluta cualquier mecanismo
    de salto o desplazamiento interno no contabilizado en gzip.GzipFile.
    Fiscaliza cada byte devuelto por read() contra max_bytes y acumula dentro de try...finally
    con inicialización previa estricta (inmune a UnboundLocalError).
    """
    def __init__(self, raw_stream, max_bytes: int):
        self.raw_stream = raw_stream
        self.max_bytes = max_bytes
        self.total_bytes_read = 0

    def read(self, size: int = -1) -> bytes:
        if self.total_bytes_read > self.max_bytes:
            raise ValueError(f"Presupuesto de descompresión agotado: {self.total_bytes_read} B > {self.max_bytes} B")

        remaining = self.max_bytes - self.total_bytes_read
        if remaining <= 0:
            if size == 0:
                return b""
            bytes_read = 0
            chunk = b""
            try:
                chunk = self.raw_stream.read(1)
                bytes_read = len(chunk) if chunk else 0
            finally:
                self.total_bytes_read += bytes_read
            if bytes_read > 0:
                raise ValueError(f"Presupuesto de descompresión excedido ({self.total_bytes_read} B > {self.max_bytes} B)")
            return b""

        to_read = size if (0 <= size <= remaining) else remaining + 1
        bytes_read = 0
        chunk = b""
        try:
            chunk = self.raw_stream.read(to_read)
            bytes_read = len(chunk) if chunk else 0
            return chunk
        finally:
            self.total_bytes_read += bytes_read
            if self.total_bytes_read > self.max_bytes:
                raise ValueError(
                    f"Límite de descompresión excedido durante lectura: "
                    f"{self.total_bytes_read} B > presupuesto {self.max_bytes} B"
                )

    def close(self):
        if hasattr(self.raw_stream, "close"):
            self.raw_stream.close()

# ------------------------------------------------------------------------------
# 3. LECTURA ATÓMICA Y VALIDACIÓN ESTRUCTURAL DEL MANIFIESTO
# ------------------------------------------------------------------------------
def load_and_validate_manifest_atomically(manifest_target_path: str) -> tuple[list | None, str, str]:
    """
    Lee el archivo de manifiesto en una sola operación binaria acotada (máx 256 KiB)
    habiendo sido previamente confinado al árbol (R6-B11).
    Ejecuta un control quíntuple de estabilidad fstat sobre el descriptor (R5-O01).
    Valida contra CANONICAL_MANIFEST_SHA256 y contra CANONICAL_INVENTORY.
    """
    try:
        with open(manifest_target_path, "rb") as f:
            fd = f.fileno()
            st_pre = os.fstat(fd)
            if not stat.S_ISREG(st_pre.st_mode):
                return None, "", "El manifiesto no es un archivo regular."
            if getattr(st_pre, "st_reparse_tag", 0) != 0:
                return None, "", "Descriptor de manifiesto posee reparse tag."
            if st_pre.st_size > MAX_MANIFEST_BYTES:
                return None, "", f"Tamaño del manifiesto ({st_pre.st_size} B) excede el límite de {MAX_MANIFEST_BYTES} B."
            raw_bytes = f.read(MAX_MANIFEST_BYTES + 1)
            if len(raw_bytes) > MAX_MANIFEST_BYTES:
                return None, "", "Sobredimensión de lectura al cargar manifiesto."
            st_post = os.fstat(fd)
            if st_pre.st_dev != st_post.st_dev or st_pre.st_ino != st_post.st_ino:
                return None, "", "Inestabilidad de identidad detectada en descriptor durante lectura del manifiesto."
            if st_pre.st_size != st_post.st_size or len(raw_bytes) != st_post.st_size:
                return None, "", "Inestabilidad de tamaño detectada en descriptor durante lectura del manifiesto."
            if st_pre.st_mtime_ns != st_post.st_mtime_ns:
                return None, "", "Inestabilidad de mtime_ns detectada en descriptor durante lectura del manifiesto."
            if st_pre.st_ctime_ns != st_post.st_ctime_ns:
                return None, "", "Inestabilidad de ctime_ns detectada en descriptor durante lectura del manifiesto."
    except Exception as e:
        return None, "", f"Error de lectura binaria del manifiesto: {e}"

    manifest_sha = hashlib.sha256(raw_bytes).hexdigest()

    # Validación estricta contra la referencia canónica declarada (R5-O03, R7-P02)
    if manifest_sha.lower() != CANONICAL_MANIFEST_SHA256.lower():
        return None, manifest_sha, (
            f"Fallo de identidad canónica del manifiesto: hash calculado {manifest_sha} "
            f"no coincide con la referencia aprobada {CANONICAL_MANIFEST_SHA256}."
        )

    try:
        manifest = json.loads(raw_bytes.decode("utf-8"))
    except Exception as e:
        return None, manifest_sha, f"Fallo al decodificar JSON del manifiesto: {e}"

    if not isinstance(manifest, list):
        return None, manifest_sha, "La raíz del manifiesto no es una lista JSON."

    if len(manifest) != 32:
        return None, manifest_sha, f"Total de entradas inválido: {len(manifest)} (se exigen exactamente 32)."

    seen_ids = set()
    seen_paths = set()
    category_counts = {k: 0 for k in EXPECTED_CATEGORY_COUNTS}

    for idx, item in enumerate(manifest, 1):
        if not isinstance(item, dict):
            return None, manifest_sha, f"La entrada #{idx} no es un objeto dict."

        for req in ["id", "category", "path", "expected_bytes", "size_bytes", "expected_sha256", "sha256"]:
            if req not in item:
                return None, manifest_sha, f"Entrada #{idx} carece del campo obligatorio '{req}'."

        eid = item["id"]
        cat = item["category"]
        p = item["path"]
        exp_b = item["expected_bytes"]
        siz_b = item["size_bytes"]
        exp_s = item["expected_sha256"]
        sha_s = item["sha256"]

        # Validaciones de tipo estricto (excluyendo booleanos)
        if type(eid) is not str or not eid.strip():
            return None, manifest_sha, f"ID inválido en entrada #{idx}"
        if type(cat) is not str or not cat.strip():
            return None, manifest_sha, f"Categoría inválida en {eid}"
        if type(p) is not str or not p.startswith("tests/corpora/"):
            return None, manifest_sha, f"Ruta inválida en {eid}"
        if type(exp_b) is not int or type(siz_b) is not int or exp_b <= 0 or siz_b <= 0:
            return None, manifest_sha, f"Dimensiones enteras inválidas en {eid}"
        if exp_b != siz_b:
            return None, manifest_sha, f"Discrepancia invariante de tamaño en {eid}"

        if type(exp_s) is not str or type(sha_s) is not str:
            return None, manifest_sha, f"Tipo de hash inválido en {eid}"
        if not HEX_SHA256_REGEX.fullmatch(exp_s) or not HEX_SHA256_REGEX.fullmatch(sha_s):
            return None, manifest_sha, f"Hash SHA-256 no hexadecimal en {eid}"
        if exp_s.lower() != sha_s.lower():
            return None, manifest_sha, f"Discrepancia invariante de hash en {eid}"

        if eid in seen_ids:
            return None, manifest_sha, f"ID duplicado: {eid}"
        seen_ids.add(eid)

        if p in seen_paths:
            return None, manifest_sha, f"Ruta duplicada: {p}"
        seen_paths.add(p)

        if cat not in category_counts:
            return None, manifest_sha, f"Categoría desconocida en {eid}: {cat}"
        category_counts[cat] += 1

        # Vinculación directa con el inventario canónico
        if eid not in CANONICAL_INVENTORY:
            return None, manifest_sha, f"El ID '{eid}' no forma parte del inventario canónico aprobado."
        canon = CANONICAL_INVENTORY[eid]
        if canon["path"] != p or canon["size"] != exp_b or canon["sha256"].lower() != exp_s.lower() or canon["category"] != cat:
            return None, manifest_sha, f"Discrepancia material entre entrada del manifiesto y la referencia canónica en {eid}"

    if seen_ids != set(CANONICAL_INVENTORY.keys()):
        return None, manifest_sha, "El conjunto de IDs no coincide biyectivamente con el inventario canónico."

    if category_counts != EXPECTED_CATEGORY_COUNTS:
        return None, manifest_sha, f"Distribución categorial inválida: {category_counts}"

    return manifest, manifest_sha, "Identidad y estructura del manifiesto 100% verificadas contra referencia inmutable."

# ------------------------------------------------------------------------------
# 4. FUNCIÓN AUDITABLE DE INSPECCIÓN DEL ENTORNO GIT (R6-B09, R7-B09.4)
# ------------------------------------------------------------------------------
def evaluate_git_state(repo_root: str) -> dict:
    """
    Ejecuta una inspección forense completa del entorno Git en repo_root (R6-B09.1, R6-B09.3, R7-B09.4):
      1. rev-parse --show-toplevel (validación de raíz física real del repositorio).
      2. rev-parse HEAD (identidad de commit canónico).
      3. rev-parse --abbrev-ref HEAD (rama activa canónica).
      4. status --porcelain --untracked-files=all (modificaciones versionadas y archivos no versionados).
    
    Delimitación Documental (R6-B09.1):
      git_clean certifica la ausencia de modificaciones en archivos rastreados y la ausencia
      de archivos no versionados que no estén expresamente ignorados por .gitignore.
      No audita archivos explícitamente ignorados por las reglas de Git del repositorio.
    """
    state = {
        "git_ok": False,
        "root_ok": False,
        "toplevel": "No disponible",
        "head": "No disponible",
        "branch": "No disponible",
        "modified_count": -1,
        "untracked_count": -1,
        "tracked_state": "No verificable",
        "untracked_desc": "No verificable",
        "worktree_clean": False,
        "revision_ok": False,
        "environment_ok": False,
        "error": None,
    }
    try:
        # Validación de raíz top-level real (R7-B09.4)
        r_top = subprocess.run(["git", "-C", repo_root, "rev-parse", "--show-toplevel"], capture_output=True, text=True, check=True)
        toplevel_path = os.path.realpath(r_top.stdout.strip())
        state["toplevel"] = toplevel_path
        state["root_ok"] = (toplevel_path.lower() == os.path.realpath(repo_root).lower())

        r_head = subprocess.run(["git", "-C", repo_root, "rev-parse", "HEAD"], capture_output=True, text=True, check=True)
        state["head"] = r_head.stdout.strip()

        r_br = subprocess.run(["git", "-C", repo_root, "rev-parse", "--abbrev-ref", "HEAD"], capture_output=True, text=True, check=True)
        state["branch"] = r_br.stdout.strip()

        r_st = subprocess.run(["git", "-C", repo_root, "status", "--porcelain", "--untracked-files=all"], capture_output=True, text=True, check=True)
        status_lines = [line for line in r_st.stdout.splitlines() if line.strip()]
        modified_lines = [l for l in status_lines if not l.startswith("??")]
        untracked_lines = [l for l in status_lines if l.startswith("??")]

        state["modified_count"] = len(modified_lines)
        state["untracked_count"] = len(untracked_lines)
        state["tracked_state"] = "Limpio (sin cambios versionados)" if not modified_lines else f"Modificado ({len(modified_lines)} cambios)"
        state["untracked_desc"] = f"{len(untracked_lines)} archivos individuales no versionados (no ignorados)"
        state["worktree_clean"] = (len(modified_lines) == 0 and len(untracked_lines) == 0)

        # Validación de revisión canónica esperada (R6-B09.3)
        state["revision_ok"] = (
            state["head"].lower() == EXPECTED_GIT_HEAD.lower() and
            state["branch"] == EXPECTED_GIT_BRANCH
        )

        state["git_ok"] = True
        state["environment_ok"] = (
            state["git_ok"] and
            state["root_ok"] and
            state["worktree_clean"] and
            state["revision_ok"]
        )
    except Exception as e:
        state["error"] = str(e)
        state["git_ok"] = False
        state["root_ok"] = False
        state["worktree_clean"] = False
        state["revision_ok"] = False
        state["environment_ok"] = False

    return state

# ------------------------------------------------------------------------------
# 5. EJECUCIÓN PRINCIPAL DEL INSTRUMENTO FORENSE
# ------------------------------------------------------------------------------
def run_audit(repo_root: str, manifest_rel_path: str = CANONICAL_MANIFEST_RELPATH) -> bool:
    repo_root = os.path.realpath(os.path.abspath(repo_root))
    start_utc = datetime.now(timezone.utc).isoformat()

    # Cálculo de auto-identidad criptográfica del script ejecutado (R7-P01)
    script_self_path = os.path.realpath(os.path.abspath(__file__))
    script_size = os.path.getsize(script_self_path)
    with open(script_self_path, "rb") as sf:
        script_sha256 = hashlib.sha256(sf.read()).hexdigest()

    # 1. Inspección inicial de Git (Pre-condición de entorno) (R7-B09.2)
    git_initial = evaluate_git_state(repo_root)

    print("=" * 105)
    print("BINPACK ENTERPRISE SDK — INSTRUMENTO FORENSE DE VERIFICACIÓN MATERIAL (BP-009 / OT-03 / R7)")
    print("=" * 105)
    print(f"Timestamp UTC:                {start_utc}")
    print(f"Versión Python:               {sys.version.splitlines()[0]}")
    print(f"Plataforma SO:                {platform.platform()}")
    print(f"Ruta Instrumento:             {script_self_path}")
    print(f"Tamaño Instrumento:           {script_size} Bytes")
    print(f"SHA-256 Instrumento:          {script_sha256}")
    print(f"Raíz Repositorio:             {repo_root}")
    print(f"Validación Top-Level Git:     {'PASS (Coincide exactamente)' if git_initial['root_ok'] else 'FAIL (Divergente: ' + str(git_initial['toplevel']) + ')'}")
    print(f"Commit HEAD Git (Inicial):    {git_initial['head']}")
    print(f"Rama Git (Inicial):           {git_initial['branch']}")
    print(f"Estado Git Tracked (Inicial): {git_initial['tracked_state']}")
    print(f"Archivos Untracked (Inicial): {git_initial['untracked_desc']}")
    print(f"Identidad Revisión Canónica:  {'PASS (Coincide con ' + EXPECTED_GIT_HEAD[:8] + ')' if git_initial['revision_ok'] else 'FAIL'}")
    print(f"Pre-condición Entorno Limpio: {'PASS' if git_initial['environment_ok'] else 'FAIL'}")
    print(f"Ruta Relativa Manifiesto:     {manifest_rel_path}")
    print("-" * 105)

    if not git_initial["root_ok"]:
        print("ABORTANDO: repo_root no coincide con la raíz real del repositorio Git (--show-toplevel).")
        return False

    # 2. Confinamiento de ruta y carga atómica del manifiesto (R6-B11)
    if manifest_rel_path != CANONICAL_MANIFEST_RELPATH:
        print(f"ABORTANDO: Ruta de manifiesto '{manifest_rel_path}' no coincide con la canónica '{CANONICAL_MANIFEST_RELPATH}'.")
        return False

    try:
        manifest_target = resolve_and_confine(repo_root, manifest_rel_path)
    except Exception as e:
        print(f"ABORTANDO: Fallo de confinamiento en ruta de manifiesto: {e}")
        return False

    print(f"Ruta Confinada Manifiesto:    {manifest_target}")
    manifest, manifest_sha, msg = load_and_validate_manifest_atomically(manifest_target)
    print(f"SHA-256 Manifiesto Leído:     {manifest_sha}")
    print(f"SHA-256 Referencia Declarada: {CANONICAL_MANIFEST_SHA256} (sujeto a contraste independiente)")
    print(f"Validación de Manifiesto:     {msg}")
    print("-" * 105)

    if manifest is None:
        print("ABORTANDO: Fallo de verificación en la identidad o integridad del manifiesto.")
        return False

    # 3. Verificación de los 32 artefactos físicos con retención de descriptores de contenedores (R5-B05)
    print(f"{'N°':<3} | {'ID Canónico':<35} | {'Bytes':<10} | {'SHA-256 Calculado':<64} | Estado")
    print("-" * 105)

    all_files_ok = True
    manifest_by_id = {}
    artifact_results = {}
    open_container_handles = {}

    try:
        for idx, entry in enumerate(manifest, 1):
            eid = entry["id"]
            rel_p = entry["path"]
            exp_b = entry["expected_bytes"]
            exp_s = entry["expected_sha256"]
            manifest_by_id[eid] = entry

            is_container = eid in CONTAINER_IDS

            try:
                target_path = resolve_and_confine(repo_root, rel_p)
                obs_b, obs_s, st_pre, f_handle = hash_regular_file_with_stability(
                    target_path,
                    expected_bytes=exp_b,
                    keep_open=is_container
                )
                if obs_b == exp_b and obs_s.lower() == exp_s.lower():
                    status = "PASS"
                    passed = True
                else:
                    status = f"FAIL (b:{obs_b==exp_b}, s:{obs_s.lower()==exp_s.lower()})"
                    all_files_ok = False
                    passed = False
                print(f"{idx:02d}  | {eid:<35} | {obs_b:>10} | {obs_s} | {status}")
                artifact_results[eid] = {"passed": passed, "bytes": obs_b, "sha256": obs_s}
                if is_container and passed:
                    open_container_handles[eid] = (f_handle, st_pre)
                elif is_container and f_handle is not None:
                    f_handle.close()
            except FileNotFoundError:
                print(f"{idx:02d}  | {eid:<35} | {'MISSING':<10} | {'NOT_FOUND':<64} | FAIL (Ausente)")
                all_files_ok = False
                artifact_results[eid] = {"passed": False, "bytes": -1, "sha256": "NOT_FOUND"}
            except Exception as e:
                print(f"{idx:02d}  | {eid:<35} | {'ERROR':<10} | {str(e)[:64]:<64} | FAIL (Excepción)")
                all_files_ok = False
                artifact_results[eid] = {"passed": False, "bytes": -1, "sha256": "ERROR"}

        print("-" * 105)
        print(f"Resultado Matriz 32 Artefactos: {'CONFORME (32/32 PASS)' if all_files_ok else 'FALLO DETECTADO'}")
        print("=" * 105)

        # 4. Trazabilidad de Extracción Silesia (12 miembros) reutilizando descriptor único (R5-B05, R6-B10)
        print("\nTRAZABILIDAD DE EXTRACCIÓN Y CORRESPONDENCIA CRUZADA: SILESIA ZIP (12 Miembros)")
        print("-" * 105)
        silesia_ok = True
        total_silesia_decompressed = 0

        silesia_container_id = "silesia/silesia.zip"
        if silesia_container_id not in open_container_handles:
            print(f"SKIPPED — Contenedor '{silesia_container_id}' no validado en la matriz física. Bloqueo fail-closed.")
            silesia_ok = False
        else:
            silesia_file_obj, st_silesia_pre = open_container_handles[silesia_container_id]
            try:
                # Reutilización directa del descriptor de archivo abierto y rebobinado (R5-B05)
                with zipfile.ZipFile(silesia_file_obj, "r") as zf:
                    infolist = zf.infolist()
                    obs_names = [i.filename for i in infolist]
                    exp_names = list(SILESIA_MEMBERS_SPEC.keys())

                    if len(infolist) != 12 or set(obs_names) != set(exp_names) or len(obs_names) != len(set(obs_names)):
                        print(f"FAIL ESTRUCTURAL: Miembros en ZIP ({len(infolist)}) no coinciden con la especificación (12).")
                        silesia_ok = False
                    else:
                        for info in infolist:
                            mname = info.filename
                            spec = SILESIA_MEMBERS_SPEC[mname]
                            exp_size = spec["size"]
                            exp_sha = spec["sha256"]
                            man_id = spec["manifest_id"]
                            member_rel = spec["rel_path"]

                            # Validación de correspondencia cruzada con el manifiesto
                            if man_id not in manifest_by_id:
                                print(f"  {mname:<12} | FAIL: No existe en manifiesto ID '{man_id}'")
                                silesia_ok = False
                                continue
                            m_entry = manifest_by_id[man_id]
                            if m_entry["path"] != member_rel or m_entry["expected_bytes"] != exp_size or m_entry["expected_sha256"].lower() != exp_sha.lower():
                                print(f"  {mname:<12} | FAIL: Discrepancia de correspondencia cruzada con entrada de manifiesto")
                                silesia_ok = False
                                continue

                            # Validación de tipo en miembro ZIP (R6-B10)
                            if info.is_dir():
                                print(f"  {mname:<12} | FAIL: Entrada ZIP es un directorio")
                                silesia_ok = False
                                continue

                            if info.create_system == 0:  # DOS / FAT
                                dos_attr = info.external_attr & 0xFF
                                if (dos_attr & 0x08) != 0:
                                    print(f"  {mname:<12} | FAIL: Entrada ZIP posee atributo de etiqueta de volumen DOS ({dos_attr:#x})")
                                    silesia_ok = False
                                    continue
                                if (dos_attr & 0x10) != 0:
                                    print(f"  {mname:<12} | FAIL: Entrada ZIP posee atributo de directorio DOS ({dos_attr:#x})")
                                    silesia_ok = False
                                    continue
                                # Garantía R6-B10: Entrada DOS no identificada como directorio ni etiqueta de volumen.
                            elif info.create_system == 3:  # Unix
                                mode = info.external_attr >> 16
                                if stat.S_ISLNK(mode):
                                    print(f"  {mname:<12} | FAIL: Entrada ZIP es un symlink Unix ({mode:#o})")
                                    silesia_ok = False
                                    continue
                                if not stat.S_ISREG(mode):
                                    print(f"  {mname:<12} | FAIL: Entrada ZIP no es archivo regular Unix ({mode:#o})")
                                    silesia_ok = False
                                    continue
                            else:
                                print(f"  {mname:<12} | FAIL: create_system desconocido o no soportado ({info.create_system})")
                                silesia_ok = False
                                continue

                            if info.file_size != exp_size:
                                print(f"  {mname:<12} | FAIL: Metadata ZIP declara {info.file_size} B != esperado {exp_size} B")
                                silesia_ok = False
                                continue

                            # Presupuesto agregado aplicado durante la descompresión (R5-B06)
                            remaining_budget = MAX_AGGREGATE_SILESIA_DECOMPRESS - total_silesia_decompressed
                            effective_limit = min(exp_size, remaining_budget)
                            if exp_size > remaining_budget or effective_limit <= 0:
                                print(f"  {mname:<12} | FAIL: Presupuesto agregado Silesia excedido ({total_silesia_decompressed} B)")
                                silesia_ok = False
                                continue

                            def track_silesia_decompressed_bytes(n: int):
                                nonlocal total_silesia_decompressed
                                total_silesia_decompressed += n

                            try:
                                with zf.open(info, "r") as mstream:
                                    stream_bytes, stream_sha = hash_bounded_stream(
                                        mstream,
                                        max_allowed_bytes=effective_limit,
                                        byte_counter_callback=track_silesia_decompressed_bytes
                                    )

                                disk_target = resolve_and_confine(repo_root, member_rel)
                                disk_bytes, disk_sha, _, _ = hash_regular_file_with_stability(
                                    disk_target,
                                    expected_bytes=exp_size,
                                    keep_open=False
                                )

                                match = (
                                    stream_bytes == disk_bytes == exp_size and
                                    stream_sha.lower() == disk_sha.lower() == exp_sha.lower()
                                )
                                if not match:
                                    silesia_ok = False
                                    print(f"  {mname:<12} | FAIL Discrepancia: ZIP={stream_bytes}B/{stream_sha[:8]} Disco={disk_bytes}B/{disk_sha[:8]}")
                                else:
                                    print(f"  {mname:<12} | Stream ZIP: {stream_bytes:>10} B, {stream_sha[:16]}... | Disco: {disk_bytes:>10} B | PASS")
                            except Exception as exc:
                                print(f"  {mname:<12} | FAIL (Excepción en miembro: {exc})")
                                silesia_ok = False

                # Verificación post-extracción de estabilidad sobre el descriptor de silesia.zip (R5-B05)
                st_silesia_post = os.fstat(silesia_file_obj.fileno())
                if (st_silesia_pre.st_dev != st_silesia_post.st_dev or
                    st_silesia_pre.st_ino != st_silesia_post.st_ino or
                    st_silesia_pre.st_size != st_silesia_post.st_size or
                    st_silesia_pre.st_mtime_ns != st_silesia_post.st_mtime_ns or
                    st_silesia_pre.st_ctime_ns != st_silesia_post.st_ctime_ns):
                    print("FAIL: Inestabilidad detectada en descriptor de silesia.zip tras la extracción.")
                    silesia_ok = False

            except Exception as e:
                print(f"ERROR durante procesamiento de contenedor Silesia ZIP: {e}")
                silesia_ok = False

        print(f"Resultado Silesia ZIP (12/12): {'CONFORME' if silesia_ok else 'FALLO'}")
        print(f"Presupuesto Silesia Utilizado: {total_silesia_decompressed} B / {MAX_AGGREGATE_SILESIA_DECOMPRESS} B")

        # 5. Trazabilidad de Extracción Canterbury (11 miembros) estrictamente secuencial en modo 'r|' (R6-B08, R7-B08.3)
        print("\nTRAZABILIDAD DE EXTRACCIÓN Y CORRESPONDENCIA CRUZADA: CANTERBURY TAR.GZ (11 Miembros)")
        print("-" * 105)
        cant_ok = True
        total_cant_decompressed = 0

        cant_container_id = "canterbury/cantrbry.tar.gz"
        if cant_container_id not in open_container_handles:
            print(f"SKIPPED — Contenedor '{cant_container_id}' no validado en la matriz física. Bloqueo fail-closed.")
            cant_ok = False
        else:
            cant_file_obj, st_cant_pre = open_container_handles[cant_container_id]
            try:
                # Reutilización directa del descriptor sin reapertura física (R5-B05)
                # StrictSequentialCountingReader no expone seek() ni tell() (R6-B08.1)
                gz_stream = gzip.GzipFile(fileobj=cant_file_obj, mode="rb")
                seq_stream = StrictSequentialCountingReader(
                    gz_stream,
                    max_bytes=MAX_AGGREGATE_CANTERBURY_DECOMPRESS
                )

                seen_tar_members = []
                extracted_net_bytes = 0

                try:
                    # tarfile en modo "r|" con bufsize explícito procesa secuencialmente sin seek (R6-B08.2, R7-B08.3)
                    with tarfile.open(fileobj=seq_stream, mode="r|", bufsize=10240) as tf:
                        for m in tf:
                            mname = m.name
                            seen_tar_members.append(mname)

                            if mname not in CANTERBURY_MEMBERS_SPEC:
                                print(f"  {mname:<16} | FAIL: Miembro no previsto en especificación canónica")
                                cant_ok = False
                                continue

                            spec = CANTERBURY_MEMBERS_SPEC[mname]
                            exp_size = spec["size"]
                            exp_sha = spec["sha256"]
                            man_id = spec["manifest_id"]
                            member_rel = spec["rel_path"]

                            # Validación de correspondencia cruzada con el manifiesto
                            if man_id not in manifest_by_id:
                                print(f"  {mname:<16} | FAIL: No existe en manifiesto ID '{man_id}'")
                                cant_ok = False
                                continue
                            m_entry = manifest_by_id[man_id]
                            if m_entry["path"] != member_rel or m_entry["expected_bytes"] != exp_size or m_entry["expected_sha256"].lower() != exp_sha.lower():
                                print(f"  {mname:<16} | FAIL: Discrepancia de correspondencia cruzada con entrada de manifiesto")
                                cant_ok = False
                                continue

                            # Control de regularidad y tamaño declarado
                            if not m.isreg():
                                print(f"  {mname:<16} | FAIL: No es archivo regular (tipo={m.type})")
                                cant_ok = False
                                continue
                            if m.size != exp_size:
                                print(f"  {mname:<16} | FAIL: Metadata TAR declara {m.size} B != esperado {exp_size} B")
                                cant_ok = False
                                continue

                            # Límite dinámico restante para este miembro
                            remaining_budget = MAX_AGGREGATE_CANTERBURY_DECOMPRESS - seq_stream.total_bytes_read
                            effective_limit = min(exp_size, remaining_budget)
                            if exp_size > remaining_budget or effective_limit <= 0:
                                print(f"  {mname:<16} | FAIL: Presupuesto agregado Canterbury excedido ({seq_stream.total_bytes_read} B)")
                                cant_ok = False
                                continue

                            try:
                                extracted = tf.extractfile(m)
                                if extracted is None:
                                    raise IOError("tf.extractfile() retornó None para un archivo regular")

                                stream_bytes, stream_sha = hash_bounded_stream(
                                    extracted,
                                    max_allowed_bytes=effective_limit
                                )
                                extracted_net_bytes += stream_bytes

                                disk_target = resolve_and_confine(repo_root, member_rel)
                                disk_bytes, disk_sha, _, _ = hash_regular_file_with_stability(
                                    disk_target,
                                    expected_bytes=exp_size,
                                    keep_open=False
                                )

                                match = (
                                    stream_bytes == disk_bytes == exp_size and
                                    stream_sha.lower() == disk_sha.lower() == exp_sha.lower()
                                )
                                if not match:
                                    cant_ok = False
                                    print(f"  {mname:<16} | FAIL Discrepancia: TAR={stream_bytes}B/{stream_sha[:8]} Disco={disk_bytes}B/{disk_sha[:8]}")
                                else:
                                    print(f"  {mname:<16} | Stream TAR: {stream_bytes:>10} B, {stream_sha[:16]}... | Disco: {disk_bytes:>10} B | PASS")
                            except Exception as exc:
                                print(f"  {mname:<16} | FAIL (Excepción en miembro: {exc})")
                                cant_ok = False

                        # Drenaje exhaustivo de bloques de terminación en el búfer interno de tarfile (R7-B08.3)
                        trailing_bytes = b""
                        while chunk := tf.fileobj.read(1024):
                            trailing_bytes += chunk

                        if any(b != 0 for b in trailing_bytes):
                            print("FAIL: Bytes no nulos encontrados tras la terminación de los miembros TAR.")
                            cant_ok = False

                        # Comprobación de que el flujo gzip subyacente alcanzó EOF y validó CRC32/ISIZE
                        if gz_stream.read(1) != b"":
                            print("FAIL: Flujo gzip contiene datos remanentes no consumidos tras EOF del TAR.")
                            cant_ok = False

                finally:
                    seq_stream.close()
                    gz_stream.close()

                # Verificación de cardinalidad y conjunto biyectivo de miembros
                exp_tar_names = set(CANTERBURY_MEMBERS_SPEC.keys())
                if len(seen_tar_members) != 11 or set(seen_tar_members) != exp_tar_names or len(seen_tar_members) != len(set(seen_tar_members)):
                    print(f"FAIL ESTRUCTURAL: Miembros observados en TAR ({len(seen_tar_members)}) no coinciden biyectivamente con los 11 esperados.")
                    cant_ok = False

                total_cant_decompressed = seq_stream.total_bytes_read

                # Verificación de correspondencia material de la suma de bytes TAR (R6-B08.3, R7-B08.3)
                if extracted_net_bytes != 2800784:
                    print(f"FAIL: Suma de bytes netos extraídos ({extracted_net_bytes} B) != 2.800.784 B esperados.")
                    cant_ok = False

                if total_cant_decompressed != 2821120:
                    print(f"FAIL: Total stream TAR consumido ({total_cant_decompressed} B) difiere de la estructura física esperada (2.821.120 B).")
                    cant_ok = False

                # Verificación post-extracción de estabilidad sobre descriptor de cantrbry.tar.gz (R5-B05)
                st_cant_post = os.fstat(cant_file_obj.fileno())
                if (st_cant_pre.st_dev != st_cant_post.st_dev or
                    st_cant_pre.st_ino != st_cant_post.st_ino or
                    st_cant_pre.st_size != st_cant_post.st_size or
                    st_cant_pre.st_mtime_ns != st_cant_post.st_mtime_ns or
                    st_cant_pre.st_ctime_ns != st_cant_post.st_ctime_ns):
                    print("FAIL: Inestabilidad detectada en descriptor de cantrbry.tar.gz tras la extracción.")
                    cant_ok = False

            except Exception as e:
                print(f"ERROR durante procesamiento de contenedor Canterbury TAR.GZ: {e}")
                cant_ok = False

        print(f"Resultado Canterbury TAR.GZ (11/11): {'CONFORME' if cant_ok else 'FALLO'}")
        print(f"Stream Total Descomprimido Canterbury (Headers + Datos): {total_cant_decompressed} B / {MAX_AGGREGATE_CANTERBURY_DECOMPRESS} B")
        print("=" * 105)

    finally:
        # Cierre garantizado de descriptores de contenedores retenidos (R5-B05)
        for handle, _ in open_container_handles.values():
            if handle is not None and not handle.closed:
                try:
                    handle.close()
                except Exception:
                    pass

    # 6. Re-evaluación final estricta de Git (inmediatamente previa al veredicto) (R6-B09.2, R7-B09.2)
    git_final = evaluate_git_state(repo_root)

    initial_environment_ok = git_initial["environment_ok"]
    final_environment_ok = git_final["environment_ok"]
    material_pass = all_files_ok and silesia_ok and cant_ok
    final_pass = material_pass and initial_environment_ok and final_environment_ok

    print("DIAGNÓSTICO CONSOLIDADO FINAL (REVISIÓN R7):")
    print(f"  - Verificación Material de Artefactos (32/32):       {'PASS' if all_files_ok else 'FAIL'}")
    print(f"  - Trazabilidad y Correspondencia Silesia ZIP (12/12):  {'PASS' if silesia_ok else 'FAIL'}")
    print(f"  - Trazabilidad y Correspondencia Canterbury (11/11):   {'PASS' if cant_ok else 'FAIL'}")
    print(f"  - Entorno Git Pre-condición (Inicial):                {'PASS' if initial_environment_ok else 'FAIL'}")
    print(f"  - Entorno Git Post-condición (Final):                 {'PASS' if final_environment_ok else 'FAIL'}")
    print(f"  - Identidad de Revisión y Raíz Top-Level:             {'PASS' if (git_final['revision_ok'] and git_final['root_ok']) else 'FAIL'}")
    print(f"RESULTADO GLOBAL DEL INSTRUMENTO FORENSE: {'CONFORME AL 100% (PASS)' if final_pass else 'FALLO EN AUDITORÍA'}")
    print("=" * 105)
    return final_pass

def main():
    parser = argparse.ArgumentParser(description="Instrumento Forense de Verificación Material (BP-009 / OT-03 / R7)")
    parser.add_argument("--repo-root", default=".", help="Ruta a la raíz del repositorio (default: directorio actual)")
    parser.add_argument("--manifest", default=CANONICAL_MANIFEST_RELPATH, help=f"Ruta relativa al manifiesto JSON (default: {CANONICAL_MANIFEST_RELPATH})")
    args = parser.parse_args()

    repo_root = os.path.realpath(os.path.abspath(args.repo_root))
    success = run_audit(repo_root, manifest_rel_path=args.manifest)
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()
