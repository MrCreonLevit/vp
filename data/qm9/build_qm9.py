#!/usr/bin/env python3
"""Download and build the QM9 dataset as Parquet with SMILES column.

Source: Ramakrishnan et al., "Quantum chemistry structures and properties
of 134 kilo molecules", Scientific Data 1, 140022 (2014).

Uses the preprocessed CSV from DeepChem/MoleculeNet which includes SMILES.
"""

import os
import subprocess
import pyarrow as pa
import pyarrow.parquet as pq
import pyarrow.csv as pcsv

CSV_URL = "https://deepchemdata.s3-us-west-1.amazonaws.com/datasets/qm9.csv"
CSV_FILE = "qm9.csv"
OUTPUT = "qm9.parquet"


def download(url, dest):
    if os.path.exists(dest) and os.path.getsize(dest) > 0:
        print(f"  {dest} already exists, skipping download")
        return
    print(f"  Downloading {dest}...")
    subprocess.run(["curl", "-L", "-o", dest, url], check=True)
    print(f"  Done ({os.path.getsize(dest) / 1e6:.1f} MB)")


def main():
    print("Building QM9 dataset with SMILES column")
    download(CSV_URL, CSV_FILE)

    # Read CSV
    print("  Reading CSV...")
    table = pcsv.read_csv(CSV_FILE)
    print(f"  Raw: {table.num_rows} rows x {table.num_columns} cols")
    print(f"  Columns: {table.column_names}")

    # Select and rename columns for Viewpoints
    # Original: mol_id, smiles, A, B, C, mu, alpha, homo, lumo, gap, r2,
    #           zpve, u0, u298, h298, g298, cv, u0_atom, u298_atom, h298_atom, g298_atom
    rename = {
        "smiles": "smiles",
        "A": "rot_A",
        "B": "rot_B",
        "C": "rot_C",
        "mu": "dipole",
        "alpha": "polarizability",
        "homo": "HOMO",
        "lumo": "LUMO",
        "gap": "gap",
        "r2": "R2",
        "zpve": "ZPVE",
        "u0": "U0",
        "u298": "U298",
        "h298": "H298",
        "g298": "G298",
        "cv": "Cv",
    }

    # Also compute atom counts from SMILES
    smiles_col = table.column("smiles").to_pylist()

    # Count heavy atoms from SMILES (simple heuristic: uppercase letters)
    n_C, n_N, n_O, n_F, n_heavy = [], [], [], [], []
    for smi in smiles_col:
        c = smi.count("C") - smi.count("Cl")  # C but not Cl
        n = smi.count("N")
        o = smi.count("O")
        f = smi.count("F") - smi.count("Fe")  # F but not Fe (unlikely in QM9)
        n_C.append(c)
        n_N.append(n)
        n_O.append(o)
        n_F.append(f)
        n_heavy.append(c + n + o + f)

    # Build output columns
    col_order = ["smiles", "n_C", "n_N", "n_O", "n_F", "n_heavy"]
    arrays = {
        "smiles": pa.array(smiles_col, type=pa.string()),
        "n_C": pa.array(n_C, type=pa.int32()),
        "n_N": pa.array(n_N, type=pa.int32()),
        "n_O": pa.array(n_O, type=pa.int32()),
        "n_F": pa.array(n_F, type=pa.int32()),
        "n_heavy": pa.array(n_heavy, type=pa.int32()),
    }

    for orig, new_name in rename.items():
        if orig == "smiles":
            continue
        col_order.append(new_name)
        col = table.column(orig)
        # Cast to float32
        arrays[new_name] = col.cast(pa.float32())

    out_table = pa.table({col: arrays[col] for col in col_order})

    pq.write_table(out_table, OUTPUT)
    size_mb = os.path.getsize(OUTPUT) / 1e6
    print(f"  Wrote {OUTPUT}: {out_table.num_rows} rows x {out_table.num_columns} cols ({size_mb:.1f} MB)")
    print(f"  Columns: {out_table.column_names}")


if __name__ == "__main__":
    main()
