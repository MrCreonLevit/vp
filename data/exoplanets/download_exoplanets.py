#!/usr/bin/env python3
"""
Download the NASA Exoplanet Archive pscomppars table (composite parameters,
one row per confirmed exoplanet) and save as Parquet for Viewpoints.

Install dependencies:
    pip install requests pandas pyarrow

Run:
    python data/exoplanets/download_exoplanets.py
"""

import os
import io
import requests
import pandas as pd
import numpy as np

OUTPUT_FILE = "data/exoplanets/exoplanets.parquet"

# NASA Exoplanet Archive TAP endpoint
TAP_URL = "https://exoplanetarchive.ipac.caltech.edu/TAP/sync"

# Columns to keep from the full table (SELECT *), grouped by category.
# The archive uses suffixes like "err1"/"err2" (no underscore before err).
KEEP_COLUMNS = [
    # ── Identification ─────────────────────────────────────────────────
    "pl_name", "hostname", "pl_letter",
    "sy_snum", "sy_pnum", "cb_flag",

    # ── Discovery ──────────────────────────────────────────────────────
    "discoverymethod", "disc_year", "disc_facility",

    # ── Detection method flags ─────────────────────────────────────────
    "rv_flag", "tran_flag", "ima_flag", "micro_flag",
    "pul_flag", "ast_flag", "etv_flag", "dkin_flag", "ttv_flag",

    # ── Orbital parameters ─────────────────────────────────────────────
    "pl_orbper", "pl_orbpererr1", "pl_orbpererr2",
    "pl_orbsmax", "pl_orbsmaxerr1", "pl_orbsmaxerr2",
    "pl_orbeccen", "pl_orbeccenerr1", "pl_orbeccenerr2",
    "pl_orbincl", "pl_orbinclerr1", "pl_orbinclerr2",
    "pl_orblper",
    "pl_rvamp", "pl_rvamperr1", "pl_rvamperr2",

    # ── Planet physical parameters ─────────────────────────────────────
    "pl_rade", "pl_radeerr1", "pl_radeerr2",
    "pl_radj", "pl_radjerr1", "pl_radjerr2",
    "pl_bmasse", "pl_bmasseerr1", "pl_bmasseerr2",
    "pl_bmassj", "pl_bmassjerr1", "pl_bmassjerr2",
    "pl_bmassprov",
    "pl_masse", "pl_masseerr1", "pl_masseerr2",
    "pl_massj", "pl_massjerr1", "pl_massjerr2",
    "pl_msinie", "pl_msinieerr1", "pl_msinieerr2",
    "pl_msinij", "pl_msinijerr1", "pl_msinijerr2",
    "pl_dens", "pl_denserr1", "pl_denserr2",
    "pl_insol", "pl_insolerr1", "pl_insolerr2",
    "pl_eqt", "pl_eqterr1", "pl_eqterr2",

    # ── Transit parameters ─────────────────────────────────────────────
    "pl_trandep", "pl_trandeperr1", "pl_trandeperr2",
    "pl_trandur", "pl_trandurerr1", "pl_trandurerr2",
    "pl_imppar", "pl_impparerr1", "pl_impparerr2",
    "pl_ratdor", "pl_ratdorerr1", "pl_ratdorerr2",
    "pl_ratror", "pl_ratrorerr1", "pl_ratrorerr2",
    "pl_occdep", "pl_occdeperr1", "pl_occdeperr2",
    "pl_projobliq", "pl_projobliqerr1", "pl_projobliqerr2",
    "pl_trueobliq", "pl_trueobliqerr1", "pl_trueobliqerr2",

    # ── Stellar parameters ─────────────────────────────────────────────
    "st_spectype",
    "st_teff", "st_tefferr1", "st_tefferr2",
    "st_rad", "st_raderr1", "st_raderr2",
    "st_mass", "st_masserr1", "st_masserr2",
    "st_met", "st_meterr1", "st_meterr2", "st_metratio",
    "st_lum", "st_lumerr1", "st_lumerr2",
    "st_logg", "st_loggerr1", "st_loggerr2",
    "st_age", "st_ageerr1", "st_ageerr2",
    "st_dens", "st_denserr1", "st_denserr2",
    "st_vsin", "st_vsinerr1", "st_vsinerr2",
    "st_rotp", "st_rotperr1", "st_rotperr2",
    "st_radv", "st_radverr1", "st_radverr2",

    # ── Position & astrometry ──────────────────────────────────────────
    "ra", "dec", "glat", "glon",
    "sy_dist", "sy_disterr1", "sy_disterr2",
    "sy_plx", "sy_plxerr1", "sy_plxerr2",
    "sy_pm", "sy_pmra", "sy_pmdec",

    # ── Photometry ─────────────────────────────────────────────────────
    "sy_bmag", "sy_vmag",
    "sy_jmag", "sy_hmag", "sy_kmag",
    "sy_umag", "sy_gmag", "sy_rmag", "sy_imag", "sy_zmag",
    "sy_w1mag", "sy_w2mag", "sy_w3mag", "sy_w4mag",
    "sy_gaiamag", "sy_tmag", "sy_kepmag",
]


# ── Derived columns ───────────────────────────────────────────────────────

def add_derived_columns(df):
    """Add computed columns useful for visualization."""
    # Log-scale versions of spanning-many-orders quantities
    for col, log_col in [
        ("pl_orbper",  "log_period_days"),
        ("pl_orbsmax", "log_sma_au"),
        ("pl_bmasse",  "log_mass_earth"),
        ("pl_insol",   "log_insolation"),
        ("sy_dist",    "log_distance_pc"),
    ]:
        if col in df.columns:
            df[log_col] = np.log10(df[col].astype(float).replace(0, np.nan))

    # Planet radius in log scale
    if "pl_rade" in df.columns:
        df["log_radius_earth"] = np.log10(
            df["pl_rade"].astype(float).replace(0, np.nan))

    # Stellar luminosity from log to linear
    if "st_lum" in df.columns:
        df["st_lum_linear"] = 10.0 ** df["st_lum"].astype(float)

    # Habitable zone estimate (simple sqrt(luminosity) scaling)
    if "st_lum" in df.columns:
        lum = 10.0 ** df["st_lum"].astype(float)
        df["hz_inner_au"] = 0.75 * np.sqrt(lum)
        df["hz_outer_au"] = 1.77 * np.sqrt(lum)
        if "pl_orbsmax" in df.columns:
            sma = df["pl_orbsmax"].astype(float)
            df["in_hz"] = ((sma >= df["hz_inner_au"]) &
                           (sma <= df["hz_outer_au"])).astype(np.int8)

    # Surface gravity estimate: g ~ M/R^2 (in Earth units)
    if "pl_bmasse" in df.columns and "pl_rade" in df.columns:
        m = df["pl_bmasse"].astype(float)
        r = df["pl_rade"].astype(float)
        df["pl_surf_grav_earth"] = m / (r ** 2)
        df["log_surf_grav"] = np.log10(
            df["pl_surf_grav_earth"].replace(0, np.nan))

    # Escape velocity estimate: v_esc ~ sqrt(M/R) (relative to Earth)
    if "pl_bmasse" in df.columns and "pl_rade" in df.columns:
        m = df["pl_bmasse"].astype(float)
        r = df["pl_rade"].astype(float)
        df["pl_vesc_earth"] = np.sqrt(m / r.replace(0, np.nan))

    # TSM-like metric (Transmission Spectroscopy Metric, simplified)
    # TSM ~ (Rp^3 * Teq) / (Mp * Rs^2 * 10^(mag/5))
    if all(c in df.columns for c in
           ["pl_rade", "pl_eqt", "pl_bmasse", "st_rad", "sy_jmag"]):
        rp = df["pl_rade"].astype(float)
        teq = df["pl_eqt"].astype(float)
        mp = df["pl_bmasse"].astype(float)
        rs = df["st_rad"].astype(float)
        jmag = df["sy_jmag"].astype(float)
        df["tsm_approx"] = (rp**3 * teq) / (mp * rs**2 * 10**(jmag / 5))

    # Color indices
    if "sy_bmag" in df.columns and "sy_vmag" in df.columns:
        df["bv_color"] = df["sy_bmag"].astype(float) - df["sy_vmag"].astype(float)
    if "sy_jmag" in df.columns and "sy_hmag" in df.columns:
        df["jh_color"] = df["sy_jmag"].astype(float) - df["sy_hmag"].astype(float)
    if "sy_hmag" in df.columns and "sy_kmag" in df.columns:
        df["hk_color"] = df["sy_hmag"].astype(float) - df["sy_kmag"].astype(float)
    if "sy_jmag" in df.columns and "sy_kmag" in df.columns:
        df["jk_color"] = df["sy_jmag"].astype(float) - df["sy_kmag"].astype(float)

    return df


# ── Main ───────────────────────────────────────────────────────────────────

def main():
    os.makedirs(os.path.dirname(OUTPUT_FILE) or ".", exist_ok=True)

    # Download all columns, filter locally (only ~6K rows, simpler than
    # debugging column name mismatches with the TAP API)
    query = "SELECT * FROM pscomppars"

    print("─── Querying NASA Exoplanet Archive ───")
    print(f"Table:   pscomppars (SELECT *)")

    resp = requests.get(TAP_URL, params={
        "query": query,
        "format": "csv",
    }, timeout=180)
    resp.raise_for_status()

    print(f"Download: {len(resp.content) / 1048576:.1f} MB")

    df = pd.read_csv(io.StringIO(resp.text), low_memory=False)
    print(f"Rows:    {len(df):,}")
    print(f"Columns from archive: {len(df.columns)}")

    # Keep only the columns we care about (skip any that don't exist)
    present = [c for c in KEEP_COLUMNS if c in df.columns]
    missing = [c for c in KEEP_COLUMNS if c not in df.columns]
    if missing:
        print(f"Note: {len(missing)} requested columns not found: {missing}")
    df = df[present]
    print(f"Kept:    {len(df.columns)} columns")

    # ── Encode string columns as categoricals ──────────────────────────
    string_cols = ["pl_name", "hostname", "pl_letter", "discoverymethod",
                   "disc_facility", "pl_bmassprov", "st_spectype",
                   "st_metratio"]
    for col in string_cols:
        if col in df.columns:
            df[col] = df[col].astype("category")

    # ── Add derived columns ────────────────────────────────────────────
    print("\n─── Computing derived columns ───")
    n_before = len(df.columns)
    df = add_derived_columns(df)
    n_derived = len(df.columns) - n_before
    print(f"Added {n_derived} derived columns")

    # ── Report completeness ────────────────────────────────────────────
    print("\n─── Column completeness ───")
    total = len(df)
    for col in df.columns:
        non_null = df[col].notna().sum()
        pct = 100.0 * non_null / total
        if pct < 100:
            print(f"  {col:30s}  {non_null:6d}/{total}  ({pct:5.1f}%)")

    # ── Write Parquet ──────────────────────────────────────────────────
    print(f"\n─── Writing Parquet ───")
    df.to_parquet(OUTPUT_FILE, engine="pyarrow", index=False)

    mb = os.path.getsize(OUTPUT_FILE) / 1048576
    print(f"File:    {OUTPUT_FILE}")
    print(f"Shape:   {df.shape[0]:,} rows x {df.shape[1]} columns")
    print(f"Size:    {mb:.1f} MB")
    print(f"Columns: {', '.join(df.columns)}")


if __name__ == "__main__":
    main()
