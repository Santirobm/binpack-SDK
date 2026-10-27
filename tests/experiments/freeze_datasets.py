import os
import hashlib
import json

CORPORA = [
    # Silesia Corpus
    ("silesia/dickens", "C:/Users/sgi.lm/Desktop/Compiler/corpora/silesia/dickens", "Silesia Corpus - Charles Dickens literature"),
    ("silesia/mozilla", "C:/Users/sgi.lm/Desktop/Compiler/corpora/silesia/mozilla", "Silesia Corpus - Mozilla binary tar archive"),
    ("silesia/mr",      "C:/Users/sgi.lm/Desktop/Compiler/corpora/silesia/mr",      "Silesia Corpus - 16-bit Magnetic Resonance scan"),
    ("silesia/nci",     "C:/Users/sgi.lm/Desktop/Compiler/corpora/silesia/nci",     "Silesia Corpus - Chemical structure database"),
    ("silesia/ooffice", "C:/Users/sgi.lm/Desktop/Compiler/corpora/silesia/ooffice", "Silesia Corpus - OpenOffice.org x86_64 binary"),
    ("silesia/osdb",    "C:/Users/sgi.lm/Desktop/Compiler/corpora/silesia/osdb",    "Silesia Corpus - PostgreSQL sample database"),
    ("silesia/reymont", "C:/Users/sgi.lm/Desktop/Compiler/corpora/silesia/reymont", "Silesia Corpus - Polish text by Wladyslaw Reymont"),
    ("silesia/samba",   "C:/Users/sgi.lm/Desktop/Compiler/corpora/silesia/samba",   "Silesia Corpus - Samba source code tarball"),
    ("silesia/sao",     "C:/Users/sgi.lm/Desktop/Compiler/corpora/silesia/sao",     "Silesia Corpus - Smithsonian Astrophysical Star Catalog"),
    ("silesia/webster", "C:/Users/sgi.lm/Desktop/Compiler/corpora/silesia/webster", "Silesia Corpus - 1913 Webster Dictionary HTML"),
    ("silesia/x-ray",   "C:/Users/sgi.lm/Desktop/Compiler/corpora/silesia/x-ray",   "Silesia Corpus - 16-bit medical X-ray scan"),
    ("silesia/xml",     "C:/Users/sgi.lm/Desktop/Compiler/corpora/silesia/xml",     "Silesia Corpus - XML documentation tarball"),

    # NASA Telemetry
    ("nasa/etth1",      "C:/Users/sgi.lm/Desktop/Compiler/corpora/nasa_telemetry/etth1_transformer.csv",      "IEEE IES / Tsinghua - Electricity Transformer Temp ETTh1"),
    ("nasa/etth2",      "C:/Users/sgi.lm/Desktop/Compiler/corpora/nasa_telemetry/etth2_transformer.csv",      "IEEE IES / Tsinghua - Electricity Transformer Temp ETTh2"),
    ("nasa/ims_test1",  "C:/Users/sgi.lm/Desktop/Compiler/corpora/nasa_telemetry/nasa_bearing_ims_test1.csv", "NASA Prognostics - IMS Bearing Test 1 (4-bearing vibration)"),
    ("nasa/ims_test2",  "C:/Users/sgi.lm/Desktop/Compiler/corpora/nasa_telemetry/nasa_bearing_ims_test2.csv", "NASA Prognostics - IMS Bearing Test 2 (Inner race failure)"),
    ("nasa/ims_test3",  "C:/Users/sgi.lm/Desktop/Compiler/corpora/nasa_telemetry/nasa_bearing_ims_test3.csv", "NASA Prognostics - IMS Bearing Test 3 (Flock fault)"),
    ("nasa/turbofan_fl32", "C:/Users/sgi.lm/Desktop/Compiler/corpora/nasa_telemetry/nasa_turbofan_float32.bin", "NASA PCoE C-MAPSS - Turbofan Engine Degradation Float32"),
    ("nasa/turbofan_asc",  "C:/Users/sgi.lm/Desktop/Compiler/corpora/nasa_telemetry/nasa_turbofan_ascii.txt",     "NASA PCoE C-MAPSS - Turbofan Engine ASCII tabular data"),

    # Canterbury & Microcontroller
    ("canterbury/grammar.lsp", "C:/Users/sgi.lm/Desktop/Compiler/corpora/canterbury/grammar.lsp", "Canterbury Corpus - Lisp grammar (Small text)"),
    ("canterbury/fields.c",    "C:/Users/sgi.lm/Desktop/Compiler/corpora/canterbury/fields.c",    "Canterbury Corpus - C language source file"),
    ("canterbury/alice29.txt", "C:/Users/sgi.lm/Desktop/Compiler/corpora/canterbury/alice29.txt", "Canterbury Corpus - Alice in Wonderland English text"),
    ("canterbury/kennedy.xls", "C:/Users/sgi.lm/Desktop/Compiler/corpora/canterbury/kennedy.xls", "Canterbury Corpus - Excel binary spreadsheet"),
    ("canterbury/sum",         "C:/Users/sgi.lm/Desktop/Compiler/corpora/canterbury/sum",         "Canterbury Corpus - SPARC binary object"),
    ("archive/busybox_armv7l", "C:/Users/sgi.lm/Desktop/Compiler/corpora/busybox_armv7l",         "Embedded Linux - BusyBox ARMv7-A executable binary"),
]

records = []
for name, path, desc in CORPORA:
    if os.path.exists(path):
        with open(path, "rb") as f:
            data = f.read()
        sha256 = hashlib.sha256(data).hexdigest()
        records.append({
            "id": name,
            "path": path,
            "description": desc,
            "size_bytes": len(data),
            "sha256": sha256
        })

out_json = "C:/Users/sgi.lm/Desktop/binpack-SDK/tests/experiments/datasets_checksums.json"
with open(out_json, "w", encoding="utf-8") as f:
    json.dump(records, f, indent=2)

print(f"Calculated and saved SHA-256 for {len(records)} official frozen datasets:")
for r in records[:10]:
    print(f"  {r['id']:<20} | {r['size_bytes']:>10} B | {r['sha256'][:16]}...")
