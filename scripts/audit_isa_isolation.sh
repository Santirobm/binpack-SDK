#!/usr/bin/env bash
# ==============================================================================
# scripts/audit_isa_isolation.sh
# BinPack Enterprise SDK — Strict ISA Isolation Verification Gate (BP-008D-R12)
#
# Inspects every compiled object file to certify that portable modules contain
# ZERO VEX (AVX/AVX2) and ZERO EVEX (AVX-512/AVX10) instructions, and that
# explicit vectorization is confined strictly to binpack_core_avx2.o.
# ==============================================================================

set -u

REPORT="docs/audit/isa_verification_report.log"
mkdir -p "$(dirname "$REPORT")"

echo "================================================================================" > "$REPORT"
echo "BINPACK ENTERPRISE SDK — AUDITORIA FORMAL DE AISLAMIENTO ISA (VEX / EVEX)" >> "$REPORT"
echo "Expediente: BP-008D-R12 | Verificación Automática de Compilación y Opcodes" >> "$REPORT"
echo "Fecha de Ejecución (UTC): $(date -u '+%Y-%m-%d %H:%M:%SZ')" >> "$REPORT"
echo "Sistema Host: $(uname -srm 2>/dev/null || echo 'Unknown')" >> "$REPORT"
echo "Arquitectura / Target: $(uname -m 2>/dev/null || echo 'x86_64') ($(gcc -dumpmachine 2>/dev/null || echo 'x86_64-linux-gnu'))" >> "$REPORT"
echo "Compilador: $(gcc --version 2>/dev/null | head -n 1 || echo 'gcc (GCC)')" >> "$REPORT"

PORTABLE_CFLAGS="${PORTABLE_CFLAGS:--O3 -march=x86-64 -mno-avx -mno-avx2 -fPIC}"
AVX2_CFLAGS="${AVX2_CFLAGS:--O3 -mavx2 -fPIC}"
echo "Flags Portables: $PORTABLE_CFLAGS" >> "$REPORT"
echo "Flags AVX2:      $AVX2_CFLAGS" >> "$REPORT"

DISASSEMBLER=""
if command -v objdump &>/dev/null; then
    DISASSEMBLER="objdump"
    echo "Herramienta de Desensamblado: $(objdump --version | head -n 1)" >> "$REPORT"
elif command -v llvm-objdump &>/dev/null; then
    DISASSEMBLER="llvm-objdump"
    echo "Herramienta de Desensamblado: $(llvm-objdump --version | head -n 1)" >> "$REPORT"
else
    echo "[FATAL] No se encontró objdump ni llvm-objdump en el entorno PATH." >> "$REPORT"
    echo "[FATAL] No se encontró objdump ni llvm-objdump en PATH." >&2
    exit 1
fi

echo "================================================================================" >> "$REPORT"
echo "" >> "$REPORT"

# Lista canónica obligatoria de unidades de traducción portables
MANDATORY_PORTABLE_OBJS=(
    "binpack_core.o"
    "binpack_cpu.o"
    "binpack_eval.o"
    "binpack_stream.o"
    "binpack_sdk.o"
    "binpack_dict.o"
    "binpack_patch.o"
    "binpack_archive.o"
)

# Unidad acelerada exclusiva
AVX2_OBJ="binpack_core_avx2.o"

# Función para localizar el archivo objeto en rutas candidatas
find_object_path() {
    local name="$1"
    local candidates=(
        "$name"
        "src/$name"
        "build/$name"
        "build/src/$name"
        "CMakeFiles/binpack.dir/src/$name"
    )
    for cand in "${candidates[@]}"; do
        if [[ -f "$cand" ]]; then
            echo "$cand"
            return 0
        fi
    done
    return 1
}

OVERALL_PASS=1

echo "1. VERIFICACION DE UNIDADES PORTABLES (Exigencia: VEX = 0, EVEX = 0)" >> "$REPORT"
echo "--------------------------------------------------------------------------------" >> "$REPORT"

for obj_name in "${MANDATORY_PORTABLE_OBJS[@]}"; do
    OBJ_PATH=$(find_object_path "$obj_name" || true)
    
    if [[ -z "$OBJ_PATH" ]]; then
        echo "[FALLO CRITICO] Objeto obligatorio ausente: $obj_name" >> "$REPORT"
        echo "  Rutas buscadas: $obj_name, src/$obj_name, build/$obj_name" >> "$REPORT"
        OVERALL_PASS=0
        continue
    fi
    
    SHA=$(sha256sum "$OBJ_PATH" | awk '{print $1}')
    SHA_VALID=$(echo "$SHA" | grep -E '^[0-9a-fA-F]{64}$' || true)
    if [[ -z "$SHA_VALID" ]]; then
        echo "[FALLO CRITICO] Hash SHA-256 inválido para $obj_name: '$SHA' (debe ser exactamente 64 hex)" >> "$REPORT"
        OVERALL_PASS=0
        continue
    fi
    
    # Desensamblado estricto con exigencia de código de salida 0
    DISASM_TMP=$(mktemp)
    DISASM_ERR=$(mktemp)
    if ! "$DISASSEMBLER" -d "$OBJ_PATH" >"$DISASM_TMP" 2>"$DISASM_ERR"; then
        echo "[FALLO CRITICO] El desensamblador $DISASSEMBLER falló al procesar $obj_name (exit code no cero)." >> "$REPORT"
        cat "$DISASM_ERR" >> "$REPORT"
        rm -f "$DISASM_TMP" "$DISASM_ERR"
        OVERALL_PASS=0
        continue
    fi
    rm -f "$DISASM_ERR"
    DISASM=$(cat "$DISASM_TMP")
    rm -f "$DISASM_TMP"

    # Rechazo de salida vacía o truncada
    if [[ -z "$DISASM" ]]; then
        echo "[FALLO CRITICO] Salida vacía o incompleta del desensamblador para $obj_name." >> "$REPORT"
        OVERALL_PASS=0
        continue
    fi
    
    # Validación estricta de presencia de bytes crudos en la salida
    HAS_BYTES=$(echo "$DISASM" | grep -E "^[[:space:]]*[0-9a-f]+:[[:space:]]+[0-9a-f]{2}" | head -n 1 || true)
    if [[ -z "$HAS_BYTES" ]]; then
        echo "[FALLO CRITICO] El desensamblador $DISASSEMBLER no emitió bytes crudos de instrucción para $obj_name." >> "$REPORT"
        echo "[FALLO CRITICO] Inspección binaria abortada para prevenir falsos negativos de opcodes." >> "$REPORT"
        OVERALL_PASS=0
        continue
    fi
    
    # Capa 1: Inspección de Mnemónicos vectoriales (prefijo 'v' en mnemónico)
    VEX_MNEM=$(echo "$DISASM" | grep -E "^[[:space:]]*[0-9a-f]+:[[:space:]]+([0-9a-f]{2}[[:space:]]+)+[[:space:]]*v[a-z0-9]+" || true)
    VEX_MNEM_COUNT=0
    [[ -n "$VEX_MNEM" ]] && VEX_MNEM_COUNT=$(echo "$VEX_MNEM" | wc -l)
    
    # Capa 2: Inspección de Opcodes binarios x86-64 para prefijos VEX (C5, C4 en 64 bits son VEX puros)
    VEX_BYTES=$(echo "$DISASM" | grep -E "^[[:space:]]*[0-9a-f]+:[[:space:]]+(c4|c5)[[:space:]]+" || true)
    VEX_BYTES_COUNT=0
    [[ -n "$VEX_BYTES" ]] && VEX_BYTES_COUNT=$(echo "$VEX_BYTES" | wc -l)
    
    # Capa 3: Inspección de Mnemónicos y decoradores EVEX (AVX-512: zmm, opmasks, broadcast)
    EVEX_MNEM=$(echo "$DISASM" | grep -E "(zmm[0-9]+|%k[0-7]|\{k[0-7]\}|\{1to[0-9]+\}|\{z\})" || true)
    EVEX_MNEM_COUNT=0
    [[ -n "$EVEX_MNEM" ]] && EVEX_MNEM_COUNT=$(echo "$EVEX_MNEM" | wc -l)
    
    # Capa 4: Inspección de Opcodes binarios para prefijo EVEX (62 en 64 bits es EVEX puro)
    EVEX_BYTES=$(echo "$DISASM" | grep -E "^[[:space:]]*[0-9a-f]+:[[:space:]]+62[[:space:]]+" || true)
    EVEX_BYTES_COUNT=0
    [[ -n "$EVEX_BYTES" ]] && EVEX_BYTES_COUNT=$(echo "$EVEX_BYTES" | wc -l)
    
    TOTAL_VEX=$(( VEX_MNEM_COUNT > VEX_BYTES_COUNT ? VEX_MNEM_COUNT : VEX_BYTES_COUNT ))
    TOTAL_EVEX=$(( EVEX_MNEM_COUNT > EVEX_BYTES_COUNT ? EVEX_MNEM_COUNT : EVEX_BYTES_COUNT ))
    
    if [[ "$TOTAL_VEX" -eq 0 && "$TOTAL_EVEX" -eq 0 ]]; then
        STATUS="PASS (Scalar Portable)"
    else
        STATUS="FAIL (Contaminación Vectorial Detectada: VEX=$TOTAL_VEX, EVEX=$TOTAL_EVEX)"
        OVERALL_PASS=0
    fi
    
    printf "%-22s | SHA256: %s | VEX: %3d | EVEX: %3d | %s\n" \
        "$obj_name" "$SHA" "$TOTAL_VEX" "$TOTAL_EVEX" "$STATUS" >> "$REPORT"
done

echo "" >> "$REPORT"
echo "2. VERIFICACION DE UNIDAD SIMD AISLADA (Exigencia: VEX > 0, EVEX = 0)" >> "$REPORT"
echo "--------------------------------------------------------------------------------" >> "$REPORT"

OBJ_PATH=$(find_object_path "$AVX2_OBJ" || true)
if [[ -z "$OBJ_PATH" ]]; then
    echo "[FALLO CRITICO] Objeto acelerado obligatorio ausente: $AVX2_OBJ" >> "$REPORT"
    OVERALL_PASS=0
else
    SHA=$(sha256sum "$OBJ_PATH" | awk '{print $1}')
    SHA_VALID=$(echo "$SHA" | grep -E '^[0-9a-fA-F]{64}$' || true)
    if [[ -z "$SHA_VALID" ]]; then
        echo "[FALLO CRITICO] Hash SHA-256 inválido para $AVX2_OBJ: '$SHA' (debe ser exactamente 64 hex)" >> "$REPORT"
        OVERALL_PASS=0
    else
        # Desensamblado estricto con exigencia de código de salida 0
        DISASM_TMP=$(mktemp)
        DISASM_ERR=$(mktemp)
        if ! "$DISASSEMBLER" -d "$OBJ_PATH" >"$DISASM_TMP" 2>"$DISASM_ERR"; then
            echo "[FALLO CRITICO] El desensamblador $DISASSEMBLER falló al procesar $AVX2_OBJ (exit code no cero)." >> "$REPORT"
            cat "$DISASM_ERR" >> "$REPORT"
            rm -f "$DISASM_TMP" "$DISASM_ERR"
            OVERALL_PASS=0
        else
            rm -f "$DISASM_ERR"
            DISASM=$(cat "$DISASM_TMP")
            rm -f "$DISASM_TMP"

            # Rechazo de salida vacía o truncada
            if [[ -z "$DISASM" ]]; then
                echo "[FALLO CRITICO] Salida vacía o incompleta del desensamblador para $AVX2_OBJ." >> "$REPORT"
                OVERALL_PASS=0
            else
                # Validación estricta de presencia de bytes crudos en la salida
                HAS_BYTES=$(echo "$DISASM" | grep -E "^[[:space:]]*[0-9a-f]+:[[:space:]]+[0-9a-f]{2}" | head -n 1 || true)
                if [[ -z "$HAS_BYTES" ]]; then
                    echo "[FALLO CRITICO] El desensamblador $DISASSEMBLER no emitió bytes crudos de instrucción para $AVX2_OBJ." >> "$REPORT"
                    echo "[FALLO CRITICO] Inspección binaria abortada para prevenir falsos negativos de opcodes." >> "$REPORT"
                    OVERALL_PASS=0
                else
                    # Capa 1: Inspección de Mnemónicos vectoriales AVX2
                    VEX_MNEM=$(echo "$DISASM" | grep -E "^[[:space:]]*[0-9a-f]+:[[:space:]]+([0-9a-f]{2}[[:space:]]+)+[[:space:]]*v[a-z0-9]+" || true)
                    VEX_MNEM_COUNT=0
                    [[ -n "$VEX_MNEM" ]] && VEX_MNEM_COUNT=$(echo "$VEX_MNEM" | wc -l)
                    
                    # Capa 2: Inspección de Opcodes binarios x86-64 para prefijos VEX (C4/C5)
                    VEX_BYTES=$(echo "$DISASM" | grep -E "^[[:space:]]*[0-9a-f]+:[[:space:]]+(c4|c5)[[:space:]]+" || true)
                    VEX_BYTES_COUNT=0
                    [[ -n "$VEX_BYTES" ]] && VEX_BYTES_COUNT=$(echo "$VEX_BYTES" | wc -l)
                    TOTAL_VEX=$(( VEX_MNEM_COUNT > VEX_BYTES_COUNT ? VEX_MNEM_COUNT : VEX_BYTES_COUNT ))

                    # Capa 3: Inspección de Mnemónicos y decoradores EVEX (AVX-512)
                    EVEX_MNEM=$(echo "$DISASM" | grep -E "(zmm[0-9]+|%k[0-7]|\{k[0-7]\}|\{1to[0-9]+\}|\{z\})" || true)
                    EVEX_MNEM_COUNT=0
                    [[ -n "$EVEX_MNEM" ]] && EVEX_MNEM_COUNT=$(echo "$EVEX_MNEM" | wc -l)
                    
                    # Capa 4: Inspección de Opcodes binarios para prefijo EVEX (0x62)
                    EVEX_BYTES=$(echo "$DISASM" | grep -E "^[[:space:]]*[0-9a-f]+:[[:space:]]+62[[:space:]]+" || true)
                    EVEX_BYTES_COUNT=0
                    [[ -n "$EVEX_BYTES" ]] && EVEX_BYTES_COUNT=$(echo "$EVEX_BYTES" | wc -l)
                    TOTAL_EVEX=$(( EVEX_MNEM_COUNT > EVEX_BYTES_COUNT ? EVEX_MNEM_COUNT : EVEX_BYTES_COUNT ))

                    if [[ "$TOTAL_VEX" -gt 0 && "$TOTAL_EVEX" -eq 0 ]]; then
                        STATUS="PASS (AVX2 Vectorization Verified, EVEX=0)"
                    else
                        STATUS="FAIL (Inconsistencia SIMD: VEX=$TOTAL_VEX, EVEX=$TOTAL_EVEX)"
                        OVERALL_PASS=0
                    fi
                    
                    printf "%-22s | SHA256: %s | VEX: %3d | EVEX: %3d | %s\n" \
                        "$AVX2_OBJ" "$SHA" "$TOTAL_VEX" "$TOTAL_EVEX" "$STATUS" >> "$REPORT"
                fi
            fi
        fi
    fi
fi

echo "" >> "$REPORT"
echo "================================================================================" >> "$REPORT"
if [[ "$OVERALL_PASS" -eq 1 ]]; then
    echo "DICTAMEN GLOBAL DE AUDITORIA ISA: PASS (Aislamiento de Silicio Certificado)" >> "$REPORT"
    echo "Certificado: Las unidades portables contienen estrictamente 0 emisiones VEX/EVEX." >> "$REPORT"
    echo "Certificado: La aceleración SIMD está confinada exclusivamente a binpack_core_avx2.o." >> "$REPORT"
else
    echo "DICTAMEN GLOBAL DE AUDITORIA ISA: FAIL (Violación de Aislamiento de Silicio)" >> "$REPORT"
fi
echo "================================================================================" >> "$REPORT"

cat "$REPORT"

if [[ "$OVERALL_PASS" -ne 1 ]]; then
    exit 1
fi
