#!/usr/bin/env python3
"""
Download the SDSS APOGEE DR17 stellar catalog via SQL and convert to
Parquet for Viewpoints.

Source: Abdurro'uf et al. (2022), "The Seventeenth Data Release of the
Sloan Digital Sky Surveys" (DR17).

Queries the apogeeStar and aspcapStar tables via the SkyServer SQL API,
which is faster and more reliable than downloading the full FITS file.

Install dependencies:
    pip install pandas pyarrow

Run:
    python data/apogee/download_apogee.py
"""

import os
import urllib.request
import urllib.parse
import pandas as pd
import numpy as np
import io
import time
import sys

OUTPUT_FILE = "data/apogee/apogee_dr17.parquet"

API = "https://skyserver.sdss.org/dr17/SkyServerWS/SearchTools/SqlSearch"

# Columns from apogeeStar (positions, kinematics, photometry, Gaia)
STAR_COLUMNS = """
s.apogee_id,
s.telescope,
s.field,
s.nvisits,
s.ra, s.dec, s.glon, s.glat,
s.snr,
s.starflag,
s.extratarg,
s.vhelio_avg, s.vscatter, s.verr,
s.gaiaedr3_parallax, s.gaiaedr3_parallax_error,
s.gaiaedr3_pmra, s.gaiaedr3_pmdec,
s.gaiaedr3_phot_g_mean_mag,
s.gaiaedr3_phot_bp_mean_mag,
s.gaiaedr3_phot_rp_mean_mag,
s.gaiaedr3_r_med_photogeo,
s.gaiaedr3_r_lo_photogeo,
s.gaiaedr3_r_hi_photogeo
"""

# Columns from aspcapStar (stellar params, abundances)
ASPCAP_COLUMNS = """
a.teff, a.teff_err,
a.logg, a.logg_err,
a.m_h, a.m_h_err,
a.alpha_m, a.alpha_m_err,
a.fe_h, a.fe_h_err,
a.vmicro, a.vmacro, a.vsini,
a.aspcapflag,
a.c_fe, a.c_fe_err,
a.ci_fe, a.ci_fe_err,
a.n_fe, a.n_fe_err,
a.o_fe, a.o_fe_err,
a.na_fe, a.na_fe_err,
a.mg_fe, a.mg_fe_err,
a.al_fe, a.al_fe_err,
a.si_fe, a.si_fe_err,
a.p_fe, a.p_fe_err,
a.s_fe, a.s_fe_err,
a.k_fe, a.k_fe_err,
a.ca_fe, a.ca_fe_err,
a.ti_fe, a.ti_fe_err,
a.tiii_fe, a.tiii_fe_err,
a.v_fe, a.v_fe_err,
a.cr_fe, a.cr_fe_err,
a.mn_fe, a.mn_fe_err,
a.co_fe, a.co_fe_err,
a.ni_fe, a.ni_fe_err,
a.cu_fe, a.cu_fe_err,
a.ce_fe, a.ce_fe_err
"""

# 2MASS photometry from apogeeObject
PHOT_COLUMNS = """
p.j, p.j_err,
p.h, p.h_err,
p.k, p.k_err,
p.ak_targ,
p.sfd_ebv
"""

JOINS = """
FROM apogeeStar s
JOIN aspcapStar a ON a.apstar_id = s.apstar_id
LEFT JOIN apogeeObject p ON p.apogee_id = s.apogee_id
"""


def download_chunks():
    """Download in RA slices to stay within SkyServer limits."""
    RA_STEP = 30
    chunks = []

    all_columns = f"{STAR_COLUMNS},\n{ASPCAP_COLUMNS},\n{PHOT_COLUMNS}"

    for ra_lo in range(0, 360, RA_STEP):
        ra_hi = ra_lo + RA_STEP
        sql = (f"SELECT {all_columns} {JOINS} "
               f"WHERE s.ra >= {ra_lo} AND s.ra < {ra_hi}")

        params = urllib.parse.urlencode({"cmd": sql, "format": "csv"})
        url = f"{API}?{params}"

        print(f"  RA [{ra_lo:3d}, {ra_hi:3d}) ... ", end="", flush=True)

        for attempt in range(3):
            try:
                req = urllib.request.Request(url)
                with urllib.request.urlopen(req, timeout=600) as resp:
                    text = resp.read().decode("utf-8")
                break
            except Exception as e:
                if attempt < 2:
                    print(f"retry ({e}) ... ", end="", flush=True)
                    time.sleep(10)
                else:
                    print(f"FAILED: {e}")
                    sys.exit(1)

        # Skip the "#Table1" header line
        lines = text.split("\n")
        csv_start = 0
        for i, line in enumerate(lines):
            if line.startswith("apogee_id"):
                csv_start = i
                break

        df = pd.read_csv(io.StringIO("\n".join(lines[csv_start:])))
        print(f"{len(df):,} stars")
        if len(df) > 0:
            chunks.append(df)

        time.sleep(2)

    print(f"\nConcatenating {len(chunks)} chunks...")
    full = pd.concat(chunks, ignore_index=True)
    return full


def clean(df):
    """Replace sentinel values with NaN."""
    # ASPCAP uses -9999 for unmeasured values
    for col in df.columns:
        if df[col].dtype in (np.float64, np.float32):
            bad = df[col] <= -9999.0
            if bad.any():
                df.loc[bad, col] = np.nan

    # Quality flags
    if "aspcapflag" in df.columns:
        df["star_bad"] = ((df["aspcapflag"] & (1 << 23)) != 0).astype(np.int8)

    if "snr" in df.columns and "aspcapflag" in df.columns:
        df["good_star"] = (
            ((df["aspcapflag"] & (1 << 23)) == 0) &
            (df["snr"] >= 50)
        ).astype(np.int8)
        print(f"  Good stars (not BAD, SNR>=50): {df['good_star'].sum():,}")

    return df


def add_derived_columns(df):
    """Add derived columns useful for visualization."""

    # Absolute Galactic latitude
    if "glat" in df.columns:
        df["abs_glat"] = np.abs(df["glat"]).astype(np.float32)

    # 2MASS colors
    if "j" in df.columns and "h" in df.columns:
        df["j_h"] = (df["j"] - df["h"]).astype(np.float32)
    if "j" in df.columns and "k" in df.columns:
        df["j_k"] = (df["j"] - df["k"]).astype(np.float32)
    if "h" in df.columns and "k" in df.columns:
        df["h_k"] = (df["h"] - df["k"]).astype(np.float32)

    # Gaia BP-RP color
    bp = "gaiaedr3_phot_bp_mean_mag"
    rp = "gaiaedr3_phot_rp_mean_mag"
    if bp in df.columns and rp in df.columns:
        df["bp_rp"] = (df[bp] - df[rp]).astype(np.float32)

    # Distance in kpc from Gaia photogeometric estimate
    if "gaiaedr3_r_med_photogeo" in df.columns:
        df["dist_kpc"] = (df["gaiaedr3_r_med_photogeo"] / 1000.0).astype(np.float32)

    # Galactocentric XYZ (Sun at R=8.2 kpc)
    if "dist_kpc" in df.columns and "glon" in df.columns and "glat" in df.columns:
        R_SUN = 8.2
        d = df["dist_kpc"].values.astype(float)
        l = np.radians(df["glon"].values.astype(float))
        b = np.radians(df["glat"].values.astype(float))
        df["x_gal"] = (R_SUN - d * np.cos(b) * np.cos(l)).astype(np.float32)
        df["y_gal"] = (-d * np.cos(b) * np.sin(l)).astype(np.float32)
        df["z_gal"] = (d * np.sin(b)).astype(np.float32)
        df["r_gal"] = np.sqrt(df["x_gal"]**2 + df["y_gal"]**2).astype(np.float32)

    # Absolute K magnitude
    if "k" in df.columns and "dist_kpc" in df.columns and "ak_targ" in df.columns:
        with np.errstate(invalid="ignore", divide="ignore"):
            DM = 5.0 * np.log10(df["dist_kpc"].values.astype(float) * 100.0)
        ak = df["ak_targ"].values.astype(float)
        df["M_K"] = (df["k"].values.astype(float) - DM - ak).astype(np.float32)

    # Giant flag
    if "logg" in df.columns:
        df["is_giant"] = (df["logg"] < 3.5).astype(np.int8)

    # [C/N] ratio (age indicator for giants)
    if "c_fe" in df.columns and "n_fe" in df.columns:
        df["c_n"] = (df["c_fe"] - df["n_fe"]).astype(np.float32)

    # [Mg/Mn] (accreted vs in-situ discriminator)
    if "mg_fe" in df.columns and "mn_fe" in df.columns:
        df["mg_mn"] = (df["mg_fe"] - df["mn_fe"]).astype(np.float32)

    # Total proper motion
    if "gaiaedr3_pmra" in df.columns and "gaiaedr3_pmdec" in df.columns:
        df["pm_total"] = np.sqrt(
            df["gaiaedr3_pmra"]**2 + df["gaiaedr3_pmdec"]**2
        ).astype(np.float32)

    return df


def main():
    os.makedirs(os.path.dirname(OUTPUT_FILE) or ".", exist_ok=True)

    print("=== Downloading APOGEE DR17 via SQL ===")
    df = download_chunks()
    print(f"Raw: {len(df):,} rows x {len(df.columns)} columns")

    print("\n=== Cleaning ===")
    df = clean(df)

    print("\n=== Derived features ===")
    n_before = len(df.columns)
    df = add_derived_columns(df)
    n_derived = len(df.columns) - n_before
    print(f"Added {n_derived} derived columns")

    # Completeness
    print(f"\n=== Completeness (selected) ===")
    key_cols = ["teff", "logg", "fe_h", "alpha_m",
                "c_fe", "n_fe", "o_fe", "mg_fe", "si_fe", "ca_fe",
                "mn_fe", "ni_fe", "al_fe", "ce_fe",
                "snr", "vhelio_avg", "dist_kpc", "M_K"]
    for col in key_cols:
        if col in df.columns:
            non_null = df[col].notna().sum()
            pct = 100 * non_null / len(df)
            print(f"  {col:30s}  {non_null:7,d}/{len(df):,}  ({pct:5.1f}%)")

    # Write Parquet
    print(f"\n=== Writing Parquet ===")
    df.to_parquet(OUTPUT_FILE, engine="pyarrow", index=False)
    mb = os.path.getsize(OUTPUT_FILE) / 1048576
    print(f"File:    {OUTPUT_FILE}")
    print(f"Shape:   {df.shape[0]:,} rows x {df.shape[1]} columns")
    print(f"Size:    {mb:.1f} MB")


if __name__ == "__main__":
    main()
