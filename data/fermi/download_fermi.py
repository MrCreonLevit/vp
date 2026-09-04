#!/usr/bin/env python3
"""
Download the Fermi LAT 4FGL-DR4 source catalog and convert to Parquet
for Viewpoints.

Source: Abdollahi et al. (2022), "Incremental Fermi Large Area Telescope
Fourth Source Catalog" (4FGL-DR4), ApJS.

Install dependencies:
    pip install astropy pandas pyarrow

Run:
    python data/fermi/download_fermi.py
"""

import os
import urllib.request
import numpy as np
import pandas as pd
from astropy.io import fits

OUTPUT_FILE = "data/fermi/fermi_4fgl_dr4.parquet"
CACHE_DIR = "data/fermi/cache"
FITS_URL = "https://fermi.gsfc.nasa.gov/ssc/data/access/lat/14yr_catalog/gll_psc_v35.fit"
FITS_FILE = os.path.join(CACHE_DIR, "gll_psc_v35.fit")


def download():
    """Download the 4FGL-DR4 FITS catalog."""
    os.makedirs(CACHE_DIR, exist_ok=True)
    if os.path.exists(FITS_FILE):
        mb = os.path.getsize(FITS_FILE) / 1048576
        print(f"Using cached: {FITS_FILE} ({mb:.1f} MB)")
        return

    print(f"Downloading {FITS_URL}")
    req = urllib.request.Request(FITS_URL, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req) as resp, open(FITS_FILE, "wb") as f:
        total = int(resp.headers.get("Content-Length", 0))
        downloaded = 0
        while True:
            chunk = resp.read(1 << 20)
            if not chunk:
                break
            f.write(chunk)
            downloaded += len(chunk)
            if total:
                print(f"\r  {downloaded / 1e6:.1f} / {total / 1e6:.1f} MB", end="")
        print()
    print(f"  Saved to {FITS_FILE}")


def extract_columns(hdul):
    """Extract all useful columns from the FITS table into a dict."""
    data = hdul[1].data
    cols = hdul[1].columns
    n = len(data)

    print(f"\n--- FITS table: {n} sources, {len(cols)} columns ---")

    result = {}

    # Walk through every column and extract scalars + small arrays
    for col in cols:
        name = col.name
        fmt = col.format
        dim = col.dim

        try:
            raw = data[name]
        except Exception:
            continue

        if hasattr(raw, 'ndim') and raw.ndim == 3:
            # 3D column (e.g. Unc_Flux_Band: n_sources x n_bands x 2)
            for i in range(raw.shape[1]):
                for j in range(raw.shape[2]):
                    sub_name = f"{name}_{i}_{j}"
                    arr = np.asarray(raw[:, i, j])
                    if arr.ndim == 1 and arr.dtype.kind in ('f', 'i', 'u'):
                        result[sub_name] = arr.astype(np.float32)
        elif hasattr(raw, 'ndim') and raw.ndim == 2:
            # Array column — expand into separate columns
            width = raw.shape[1]
            for i in range(width):
                sub_name = f"{name}_{i}"
                arr = np.asarray(raw[:, i])
                if arr.ndim != 1:
                    continue
                if arr.dtype.kind in ('f', 'i', 'u'):
                    result[sub_name] = arr.astype(np.float32)
                elif arr.dtype.kind in ('U', 'S', 'O'):
                    result[sub_name] = np.array([
                        s.strip() if isinstance(s, str) else s.decode().strip()
                        for s in arr
                    ])
        elif raw.dtype.kind in ('f', 'i', 'u', 'b'):
            # Scalar numeric / boolean
            if raw.dtype.kind == 'b':
                result[name] = raw.astype(np.int8)
            elif raw.dtype == np.float64:
                result[name] = raw.astype(np.float32)
            else:
                result[name] = raw.astype(np.float32) if raw.dtype.kind == 'f' else raw
        elif raw.dtype.kind in ('U', 'S', 'O'):
            # String column
            result[name] = np.array([
                s.strip() if isinstance(s, str) else s.decode().strip()
                for s in raw
            ])
        else:
            print(f"  Skipping {name} (dtype {raw.dtype})")

    return result


def add_derived_columns(df):
    """Add derived features useful for visualization."""

    # ── Galactic plane distance ────────────────────────────────────────
    if "GLAT" in df.columns:
        df["abs_GLAT"] = np.abs(df["GLAT"]).astype(np.float32)

    # ── Log-scale fluxes (span many orders of magnitude) ───────────────
    for col in df.columns:
        if ("Flux" in col or "flux" in col) and df[col].dtype in (np.float32, np.float64):
            pos = df[col].clip(lower=0).replace(0, np.nan)
            log_col = f"log_{col}"
            if log_col not in df.columns:
                df[log_col] = np.log10(pos).astype(np.float32)

    # ── Log significance ───────────────────────────────────────────────
    if "Signif_Avg" in df.columns:
        df["log_Signif_Avg"] = np.log10(
            df["Signif_Avg"].clip(lower=1)).astype(np.float32)

    # ── Log variability index ──────────────────────────────────────────
    if "Variability_Index" in df.columns:
        df["log_Variability_Index"] = np.log10(
            df["Variability_Index"].clip(lower=1)).astype(np.float32)

    # ── Hardness ratios from flux band columns ─────────────────────────
    # Flux bands: 0=50-100 MeV, 1=100-300 MeV, 2=300 MeV-1 GeV,
    #             3=1-3 GeV, 4=3-10 GeV, 5=10-30 GeV, 6=30-300 GeV
    flux_bands = [f"Flux_Band_{i}" for i in range(7)]
    present_bands = [f for f in flux_bands if f in df.columns]
    if len(present_bands) >= 5:
        eps = 1e-15
        # Soft ratio: (band2 - band4) / (band2 + band4)
        df["hardness_soft"] = (
            (df["Flux_Band_2"] - df["Flux_Band_4"]) /
            (df["Flux_Band_2"] + df["Flux_Band_4"] + eps)
        ).astype(np.float32)
        # Hard ratio: (band4 - band6) / (band4 + band6) if band 6 exists
        if "Flux_Band_6" in df.columns:
            df["hardness_hard"] = (
                (df["Flux_Band_4"] - df["Flux_Band_6"]) /
                (df["Flux_Band_4"] + df["Flux_Band_6"] + eps)
            ).astype(np.float32)

    # ── Spectral curvature significance flag ───────────────────────────
    if "LP_SigCurv" in df.columns:
        df["is_curved"] = (df["LP_SigCurv"] > 4.0).astype(np.int8)

    # ── Variable source flag ───────────────────────────────────────────
    if "Variability_Index" in df.columns:
        # Threshold from 4FGL paper: VI > 24.725 for 14 intervals at 99% CL
        df["is_variable"] = (df["Variability_Index"] > 24.725).astype(np.int8)

    # ── Source class simplification ────────────────────────────────────
    if "CLASS1" in df.columns:
        class_map = {
            "bll": "Blazar (BL Lac)",
            "BLL": "Blazar (BL Lac)",
            "fsrq": "Blazar (FSRQ)",
            "FSRQ": "Blazar (FSRQ)",
            "bcu": "Blazar (uncertain)",
            "BCU": "Blazar (uncertain)",
            "psr": "Pulsar",
            "PSR": "Pulsar",
            "msp": "Pulsar (MSP)",
            "MSP": "Pulsar (MSP)",
            "pwn": "Pulsar Wind Nebula",
            "PWN": "Pulsar Wind Nebula",
            "snr": "Supernova Remnant",
            "SNR": "Supernova Remnant",
            "spp": "SNR/PWN",
            "SPP": "SNR/PWN",
            "hmb": "Binary",
            "HMB": "Binary",
            "lmb": "Binary",
            "LMB": "Binary",
            "bin": "Binary",
            "BIN": "Binary",
            "glc": "Globular Cluster",
            "GLC": "Globular Cluster",
            "gal": "Galaxy",
            "GAL": "Galaxy",
            "agn": "AGN (other)",
            "AGN": "AGN (other)",
            "rdg": "Radio Galaxy",
            "RDG": "Radio Galaxy",
            "nlsy1": "NLSy1",
            "NLSY1": "NLSy1",
            "css": "Compact Steep Spectrum",
            "CSS": "Compact Steep Spectrum",
            "sey": "Seyfert",
            "SEY": "Seyfert",
            "sbg": "Starburst Galaxy",
            "SBG": "Starburst Galaxy",
            "nov": "Nova",
            "NOV": "Nova",
        }
        df["source_class"] = df["CLASS1"].map(
            lambda x: class_map.get(x.strip(), "Unassociated" if x.strip() == "" else x.strip())
        )

    return df


def main():
    os.makedirs(os.path.dirname(OUTPUT_FILE) or ".", exist_ok=True)

    # 1) Download
    print("=== Download ===")
    download()

    # 2) Read FITS
    print("\n=== Reading FITS ===")
    hdul = fits.open(FITS_FILE)
    print("HDU list:")
    hdul.info()

    columns = extract_columns(hdul)
    df = pd.DataFrame(columns)
    hdul.close()

    print(f"\nExtracted: {len(df):,} rows x {len(df.columns)} columns")

    # 3) Derived features
    print("\n=== Derived features ===")
    n_before = len(df.columns)
    df = add_derived_columns(df)
    n_derived = len(df.columns) - n_before
    print(f"Added {n_derived} derived columns")

    # 4) Report column summary
    print(f"\n=== Column summary ===")
    num_cols = [c for c in df.columns if df[c].dtype in (np.float32, np.float64, np.int8, np.int16, np.int32, np.int64)]
    str_cols = [c for c in df.columns if df[c].dtype == object]
    print(f"Numeric: {len(num_cols)}")
    print(f"String:  {len(str_cols)}")

    # Report completeness for key columns
    print(f"\n=== Completeness (selected columns) ===")
    key_cols = ["RA", "DEC", "GLON", "GLAT", "Signif_Avg", "PL_Index",
                "LP_Index", "LP_Beta", "Variability_Index",
                "CLASS1", "source_class", "ASSOC1"]
    for col in key_cols:
        if col in df.columns:
            if df[col].dtype == object:
                non_null = (df[col].str.strip() != "").sum()
            else:
                non_null = df[col].notna().sum()
            pct = 100 * non_null / len(df)
            print(f"  {col:30s}  {non_null:5d}/{len(df)}  ({pct:5.1f}%)")

    # 5) Write Parquet
    print(f"\n=== Writing Parquet ===")
    df.to_parquet(OUTPUT_FILE, engine="pyarrow", index=False)

    mb = os.path.getsize(OUTPUT_FILE) / 1048576
    print(f"File:    {OUTPUT_FILE}")
    print(f"Shape:   {df.shape[0]:,} rows x {df.shape[1]} columns")
    print(f"Size:    {mb:.1f} MB")


if __name__ == "__main__":
    main()
