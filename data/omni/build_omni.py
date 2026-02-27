#!/usr/bin/env python3
"""Download and convert NASA OMNI hourly solar wind data to Parquet.

Source: NASA/GSFC Space Physics Data Facility (SPDF)
        https://omniweb.gsfc.nasa.gov/ow.html

The OMNI dataset provides near-Earth solar wind magnetic field and plasma
parameters, geomagnetic activity indices, and energetic proton fluxes.
Data spans 1963 to present, compiled from multiple spacecraft (IMP, ACE,
Wind, DSCOVR, etc.) and time-shifted to Earth's bow shock nose.

Underlying evaluations:
  - IMF and plasma: ACE, Wind, IMP-8, DSCOVR, and others
  - Geomagnetic indices: IAGA, WDC Kyoto
  - Energetic particles: GOES, IMP-8
"""

import urllib.request
import numpy as np
import pyarrow as pa
import pyarrow.parquet as pq
import os

DATA_URL = "https://spdf.gsfc.nasa.gov/pub/data/omni/low_res_omni/omni2_all_years.dat"
DATA_FILE = "omni2_all_years.dat"
OUTPUT = "omni_hourly.parquet"

# Column definitions: (name, start_word_index, type, fill_value, units, description)
# Word indices are 0-based positions after splitting on whitespace
COLUMNS = [
    # Time
    ("year",           0, "int16",   None,      "",       "Year"),
    ("doy",            1, "int16",   None,      "",       "Day of year"),
    ("hour",           2, "int8",    None,      "",       "Hour (0-23)"),
    # Magnetic field
    ("B_avg",          8, "float32", 999.9,     "nT",     "Field magnitude average <|B|>"),
    ("B_mag",          9, "float32", 999.9,     "nT",     "Magnitude of average field |<B>|"),
    ("Bx_gse",        12, "float32", 999.9,     "nT",     "Bx (GSE)"),
    ("By_gse",        13, "float32", 999.9,     "nT",     "By (GSE)"),
    ("Bz_gse",        14, "float32", 999.9,     "nT",     "Bz (GSE)"),
    ("By_gsm",        15, "float32", 999.9,     "nT",     "By (GSM)"),
    ("Bz_gsm",        16, "float32", 999.9,     "nT",     "Bz (GSM)"),
    # Plasma
    ("T_proton",      22, "float32", 9999999.,  "K",      "Proton temperature"),
    ("n_proton",      23, "float32", 999.9,     "#/cc",   "Proton density"),
    ("v_bulk",        24, "float32", 9999.,     "km/s",   "Bulk flow speed"),
    ("flow_lon",      25, "float32", 999.9,     "deg",    "Flow longitude angle (phi)"),
    ("flow_lat",      26, "float32", 999.9,     "deg",    "Flow latitude angle (theta)"),
    ("alpha_ratio",   27, "float32", 9.999,     "",       "Alpha/proton ratio"),
    ("P_flow",        28, "float32", 99.99,     "nPa",    "Flow pressure"),
    # Derived
    ("E_field",       35, "float32", 999.99,    "mV/m",   "Electric field -V×Bz"),
    ("plasma_beta",   36, "float32", 999.99,    "",       "Plasma beta"),
    ("alfven_mach",   37, "float32", 999.9,     "",       "Alfvén Mach number"),
    # Geomagnetic indices
    ("Kp",            38, "float32", 99,        "",       "Kp index (×10)"),
    ("sunspot_num",   39, "int16",   999,       "",       "Daily sunspot number"),
    ("Dst",           40, "int16",   99999,     "nT",     "Dst index"),
    ("AE",            41, "int16",   9999,      "nT",     "AE index"),
    # Energetic protons
    ("pflux_1MeV",    42, "float32", 999999.99, "1/(cm2 s sr)", "Proton flux >1 MeV"),
    ("pflux_10MeV",   45, "float32", 99999.99,  "1/(cm2 s sr)", "Proton flux >10 MeV"),
    # More indices
    ("ap",            49, "int16",   999,       "nT",     "ap index"),
    ("f10_7",         50, "float32", 999.9,     "sfu",    "F10.7 solar radio flux"),
    ("PC_N",          51, "float32", 999.9,     "",       "PC(N) index"),
    ("AL",            52, "int32",   99999,     "nT",     "AL index"),
    ("AU",            53, "int32",   99999,     "nT",     "AU index"),
    ("mach_ms",       54, "float32", 99.9,      "",       "Magnetosonic Mach number"),
]


def download(url, dest):
    if os.path.exists(dest) and os.path.getsize(dest) > 0:
        print(f"  {dest} already exists, skipping download")
        return
    print(f"  Downloading {dest} ...")
    urllib.request.urlretrieve(url, dest)
    size_mb = os.path.getsize(dest) / 1e6
    print(f"  Done ({size_mb:.1f} MB)")


def parse(filename):
    print(f"  Parsing {filename} ...")
    # Read all lines and split into words
    with open(filename) as f:
        lines = f.readlines()

    n = len(lines)
    print(f"  {n:,} records")

    # Pre-allocate arrays
    arrays = {}
    for name, _, dtype, _, _, _ in COLUMNS:
        if dtype == "float64":
            arrays[name] = np.empty(n, dtype=np.float64)
        elif dtype == "float32":
            arrays[name] = np.empty(n, dtype=np.float64)  # parse as f64, cast later
        elif dtype == "int32":
            arrays[name] = np.empty(n, dtype=np.int64)
        elif dtype == "int16":
            arrays[name] = np.empty(n, dtype=np.int64)
        elif dtype == "int8":
            arrays[name] = np.empty(n, dtype=np.int64)

    for i, line in enumerate(lines):
        words = line.split()
        for name, word_idx, dtype, _, _, _ in COLUMNS:
            try:
                if "float" in dtype:
                    arrays[name][i] = float(words[word_idx])
                else:
                    arrays[name][i] = int(words[word_idx])
            except (IndexError, ValueError):
                arrays[name][i] = np.nan if "float" in dtype else -99999

    # Replace fill values with NaN
    for name, _, dtype, fill, _, _ in COLUMNS:
        if fill is not None:
            if "float" in dtype:
                arrays[name][np.isclose(arrays[name], fill, atol=0.01)] = np.nan
            else:
                arrays[name][arrays[name] == fill] = -99999

    # Kp is stored as Kp×10 integer; convert to float
    arrays["Kp"] = arrays["Kp"] / 10.0

    # Convert Lyman-alpha fill (0.999999) to NaN — already handled above

    return arrays, n


def build_table(arrays, n):
    pa_arrays = {}

    for name, _, dtype, _, _, _ in COLUMNS:
        arr = arrays[name]
        if dtype == "float32":
            pa_arrays[name] = pa.array(arr.astype(np.float32))
        elif dtype == "float64":
            pa_arrays[name] = pa.array(arr)
        elif dtype == "int16":
            # Replace sentinel with null
            mask = arr == -99999
            arr = arr.astype(np.int16)
            if mask.any():
                pa_arrays[name] = pa.array(arr.tolist(),
                                           mask=mask.tolist(),
                                           type=pa.int16())
            else:
                pa_arrays[name] = pa.array(arr, type=pa.int16())
        elif dtype == "int32":
            mask = arr == -99999
            arr = arr.astype(np.int32)
            if mask.any():
                pa_arrays[name] = pa.array(arr.tolist(),
                                           mask=mask.tolist(),
                                           type=pa.int32())
            else:
                pa_arrays[name] = pa.array(arr, type=pa.int32())
        elif dtype == "int8":
            pa_arrays[name] = pa.array(arr.astype(np.int8), type=pa.int8())

    col_names = [name for name, _, _, _, _, _ in COLUMNS]
    return pa.table({name: pa_arrays[name] for name in col_names})


def main():
    print("Building OMNI hourly solar wind dataset")
    download(DATA_URL, DATA_FILE)
    arrays, n = parse(DATA_FILE)
    table = build_table(arrays, n)
    pq.write_table(table, OUTPUT)
    size_mb = os.path.getsize(OUTPUT) / 1e6
    print(f"  Wrote {OUTPUT}: {table.num_rows:,} rows × {table.num_columns} cols ({size_mb:.1f} MB)")
    print(f"  Columns: {table.column_names}")


if __name__ == "__main__":
    main()
