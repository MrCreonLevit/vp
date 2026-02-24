#!/usr/bin/env python3
"""Download nuclide ground-state data from IAEA Nuclear Data Services.

Source: IAEA Nuclear Data Services (NDS) LiveChart API
        https://nds.iaea.org/relnsd/vcharthtml/VChartHTML.html

The underlying data comes from:
  - ENSDF (Evaluated Nuclear Structure Data File) for structure/decay data
  - AME2020 / NUBASE2020 for atomic masses and binding energies

API endpoint:
  https://nds.iaea.org/relnsd/v1/data?fields=ground_states&nuclides=all
"""

import urllib.request
import io
import numpy as np
import pyarrow as pa
import pyarrow.parquet as pq
import pandas as pd
import os

API_URL = "https://nds.iaea.org/relnsd/v1/data?fields=ground_states&nuclides=all"
OUTPUT = "nuclides.parquet"


def download():
    print("  Downloading ground-state data from IAEA NDS...")
    req = urllib.request.Request(API_URL, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req, timeout=60) as resp:
        text = resp.read().decode("utf-8")
    df = pd.read_csv(io.StringIO(text))
    print(f"  Raw: {len(df)} nuclides x {len(df.columns)} columns")
    print(f"  Columns: {list(df.columns)}")
    return df


def clean(df):
    """Select, rename, and transform columns into a clean parquet."""

    out = pd.DataFrame()

    # --- Identity ---
    out["Z"] = pd.to_numeric(df["z"], errors="coerce")
    out["N"] = pd.to_numeric(df["n"], errors="coerce")
    out["A"] = out["Z"] + out["N"]
    out["symbol"] = df["symbol"].astype(str).str.strip()

    # --- Masses (keV and micro-u) ---
    # IAEA "binding" field is already binding energy per nucleon (keV)
    out["binding_per_A"] = pd.to_numeric(df["binding"], errors="coerce")
    out["mass_excess"] = pd.to_numeric(df["massexcess"], errors="coerce")
    out["atomic_mass"] = pd.to_numeric(df["atomic_mass"], errors="coerce")

    # --- Half-life ---
    hl_sec = pd.to_numeric(df["half_life_sec"], errors="coerce")
    out["half_life_log10"] = np.where(hl_sec > 0, np.log10(hl_sec), np.nan)
    # Stable nuclides have half_life text == "STABLE" in the IAEA data;
    # set to 50 so they're visible on plots (max unstable is ~32 in log10 s)
    stable_mask = df["half_life"].astype(str).str.strip().str.upper() == "STABLE"
    out.loc[stable_mask, "half_life_log10"] = 50.0

    # --- Spin, parity, isospin ---
    out["jp"] = df["jp"].astype(str).str.strip() if "jp" in df.columns else np.nan
    # Mark truly missing jp as empty string
    out.loc[out["jp"].isin(["nan", ""]), "jp"] = ""

    if "isospin" in df.columns:
        out["isospin"] = pd.to_numeric(df["isospin"], errors="coerce")

    # --- Nuclear radius ---
    out["radius"] = pd.to_numeric(df["radius"], errors="coerce")

    # --- Natural abundance ---
    out["abundance"] = pd.to_numeric(df["abundance"], errors="coerce")

    # --- Separation energies (keV) ---
    out["Sn"] = pd.to_numeric(df["sn"], errors="coerce")
    out["Sp"] = pd.to_numeric(df["sp"], errors="coerce")

    # --- Q-values (keV) ---
    out["Qa"] = pd.to_numeric(df["qa"], errors="coerce")
    out["Qbm"] = pd.to_numeric(df["qbm"], errors="coerce")
    out["Qec"] = pd.to_numeric(df["qec"], errors="coerce")
    if "qbm_n" in df.columns:
        out["Qbm_n"] = pd.to_numeric(df["qbm_n"], errors="coerce")

    # --- Electromagnetic moments ---
    out["mag_dipole"] = pd.to_numeric(df["magnetic_dipole"], errors="coerce")
    out["elec_quadrupole"] = pd.to_numeric(df["electric_quadrupole"], errors="coerce")

    # --- Decay modes ---
    for i in range(1, 4):
        mode_col = f"decay_{i}"
        pct_col = f"decay_{i}_%"
        if mode_col in df.columns:
            out[mode_col] = df[mode_col].astype(str).str.strip()
            out.loc[out[mode_col].isin(["nan", ""]), mode_col] = ""
        if pct_col in df.columns:
            out[pct_col] = pd.to_numeric(df[pct_col], errors="coerce")

    # --- Discovery year ---
    out["discovery"] = pd.to_numeric(df["discovery"], errors="coerce")

    # Cast float columns to float32 for compactness
    float_cols = out.select_dtypes(include=["float64"]).columns
    for col in float_cols:
        out[col] = out[col].astype("float32")

    # Cast Z, N, A to int16
    for col in ["Z", "N", "A"]:
        out[col] = out[col].astype("int16")

    return out


def main():
    print("Building nuclides dataset from IAEA NDS")
    df = download()
    out = clean(df)
    print(f"  Output: {len(out)} nuclides x {len(out.columns)} columns")
    print(f"  Columns: {list(out.columns)}")

    table = pa.Table.from_pandas(out, preserve_index=False)
    pq.write_table(table, OUTPUT)
    size_kb = os.path.getsize(OUTPUT) / 1024
    print(f"  Wrote {OUTPUT} ({size_kb:.0f} KB)")


if __name__ == "__main__":
    main()
