#!/usr/bin/env python3
"""
Download the UCI Higgs Boson dataset (11M collision events, 28 features)
and save as Parquet for Viewpoints.

Source: Baldi, Sadowski, Whiteson. "Searching for Exotic Particles in
High-Energy Physics with Deep Learning." Nature Comms 5, 4308 (2014).

Install dependencies:
    pip install pandas pyarrow

Run:
    python data/higgs/download_higgs.py
"""

import os
import gzip
import urllib.request
import numpy as np
import pandas as pd

OUTPUT_FILE = "data/higgs/higgs.parquet"
URL = "https://archive.ics.uci.edu/ml/machine-learning-databases/00280/HIGGS.csv.gz"
CACHE_FILE = "data/higgs/cache/HIGGS.csv.gz"

# Column definitions from the dataset documentation.
# Col 0: label (1 = signal, 0 = background)
# Cols 1-21: 21 low-level detector features (kinematic properties)
# Cols 22-28: 7 high-level physicist-derived features
COLUMNS = [
    "label",
    # Low-level features: lepton (the charged lepton from W decay)
    "lepton_pT",          # transverse momentum
    "lepton_eta",         # pseudorapidity
    "lepton_phi",         # azimuthal angle
    # Low-level: missing energy (neutrino proxy)
    "missing_energy_mag", # missing transverse energy magnitude
    "missing_energy_phi", # missing energy azimuthal angle
    # Low-level: jet 1 (leading jet)
    "jet1_pT",
    "jet1_eta",
    "jet1_phi",
    "jet1_btag",          # b-tagging discriminant
    # Low-level: jet 2
    "jet2_pT",
    "jet2_eta",
    "jet2_phi",
    "jet2_btag",
    # Low-level: jet 3
    "jet3_pT",
    "jet3_eta",
    "jet3_phi",
    "jet3_btag",
    # Low-level: jet 4
    "jet4_pT",
    "jet4_eta",
    "jet4_phi",
    "jet4_btag",
    # High-level physicist-derived features
    "m_jj",               # invariant mass of jet pair
    "m_jjj",              # invariant mass of 3-jet system
    "m_lv",               # invariant mass of lepton + MET (W mass proxy)
    "m_jlv",              # invariant mass of jet + lepton + MET
    "m_bb",               # invariant mass of b-tagged jet pair
    "m_wbb",              # invariant mass of W + bb system
    "m_wwbb",             # invariant mass of WW + bb system (Higgs proxy)
]


def download():
    """Download the gzipped CSV."""
    os.makedirs(os.path.dirname(CACHE_FILE), exist_ok=True)
    if os.path.exists(CACHE_FILE):
        mb = os.path.getsize(CACHE_FILE) / 1048576
        print(f"Using cached file: {CACHE_FILE} ({mb:.0f} MB)")
        return

    print(f"Downloading {URL}")
    print(f"  (this is ~2.6 GB compressed, ~8 GB uncompressed)")
    req = urllib.request.Request(URL, headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req) as resp, open(CACHE_FILE, "wb") as f:
        total = int(resp.headers.get("Content-Length", 0))
        downloaded = 0
        while True:
            chunk = resp.read(1 << 20)
            if not chunk:
                break
            f.write(chunk)
            downloaded += len(chunk)
            if total:
                pct = 100 * downloaded / total
                print(f"\r  {downloaded / 1e6:.0f} / {total / 1e6:.0f} MB ({pct:.0f}%)", end="")
        print()
    print(f"  Saved to {CACHE_FILE}")


def add_derived_columns(df):
    """Add useful derived features."""
    # Signal vs background as readable string
    df["event_type"] = np.where(df["label"] == 1, "signal", "background")

    # Total jet transverse momentum (scalar sum)
    df["jet_HT"] = df["jet1_pT"] + df["jet2_pT"] + df["jet3_pT"] + df["jet4_pT"]

    # Number of b-tagged jets (btag > 0.5 threshold)
    df["n_btags"] = (
        (df["jet1_btag"] > 0.5).astype(np.int8) +
        (df["jet2_btag"] > 0.5).astype(np.int8) +
        (df["jet3_btag"] > 0.5).astype(np.int8) +
        (df["jet4_btag"] > 0.5).astype(np.int8)
    )

    # Delta phi between lepton and MET
    dphi = df["lepton_phi"] - df["missing_energy_phi"]
    df["dphi_lep_met"] = np.arctan2(np.sin(dphi), np.cos(dphi))

    # Transverse mass of lepton + MET system
    # mT = sqrt(2 * pT_lep * MET * (1 - cos(dphi)))
    cos_dphi = np.cos(df["dphi_lep_met"])
    df["mT_lep_met"] = np.sqrt(
        2 * df["lepton_pT"] * df["missing_energy_mag"] * (1 - cos_dphi)
    ).astype(np.float32)

    # Delta eta between leading jets
    df["deta_j1j2"] = (df["jet1_eta"] - df["jet2_eta"]).astype(np.float32)

    # Delta R between leading jets: sqrt(deta^2 + dphi^2)
    dphi_jj = np.arctan2(
        np.sin(df["jet1_phi"] - df["jet2_phi"]),
        np.cos(df["jet1_phi"] - df["jet2_phi"])
    )
    df["dR_j1j2"] = np.sqrt(df["deta_j1j2"]**2 + dphi_jj**2).astype(np.float32)

    # pT ratio: leading jet / subleading jet
    df["pT_ratio_j1j2"] = (df["jet1_pT"] / df["jet2_pT"].replace(0, np.nan)).astype(np.float32)

    # Log of invariant masses (span many orders of magnitude)
    for col in ["m_jj", "m_jjj", "m_lv", "m_jlv", "m_bb", "m_wbb", "m_wwbb"]:
        df[f"log_{col}"] = np.log1p(df[col].clip(lower=0)).astype(np.float32)

    return df


def main():
    os.makedirs(os.path.dirname(OUTPUT_FILE) or ".", exist_ok=True)

    # 1) Download
    print("=== Download ===")
    download()

    # 2) Read CSV
    print("\n=== Reading CSV ===")
    print(f"  Reading {CACHE_FILE} (this may use ~8 GB of RAM)...")
    df = pd.read_csv(CACHE_FILE, header=None, names=COLUMNS,
                     dtype=np.float32, compression="gzip")
    print(f"  {len(df):,} events x {len(df.columns)} columns")

    n_signal = (df["label"] == 1).sum()
    n_bg = (df["label"] == 0).sum()
    print(f"  Signal: {n_signal:,}  Background: {n_bg:,}")

    # 3) Derived features
    print("\n=== Derived features ===")
    n_before = len(df.columns)
    df = add_derived_columns(df)
    print(f"  Added {len(df.columns) - n_before} derived columns")

    # 4) Cast to float32 where possible (save space)
    for col in df.columns:
        if df[col].dtype == np.float64:
            df[col] = df[col].astype(np.float32)

    # 5) Write Parquet
    print(f"\n=== Writing Parquet ===")
    df.to_parquet(OUTPUT_FILE, engine="pyarrow", index=False)

    mb = os.path.getsize(OUTPUT_FILE) / 1048576
    print(f"File:    {OUTPUT_FILE}")
    print(f"Shape:   {df.shape[0]:,} rows x {df.shape[1]} columns")
    print(f"Size:    {mb:.1f} MB")
    print(f"Columns: {', '.join(df.columns)}")


if __name__ == "__main__":
    main()
