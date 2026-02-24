# QM9 Molecular Properties Dataset

Quantum-chemical properties of 133,885 small organic molecules (up to 9 heavy
atoms: C, N, O, F).

## Source

**Ramakrishnan et al.**, "Quantum chemistry structures and properties of
134 kilo molecules", *Scientific Data* 1, 140022 (2014).

Downloaded via the **DeepChem/MoleculeNet** preprocessed CSV which includes
SMILES strings:
- URL: `https://deepchemdata.s3-us-west-1.amazonaws.com/datasets/qm9.csv`

## Build

```
python3 build_qm9.py
```

Downloads the CSV, selects and renames columns, computes atom counts from
SMILES, casts to float32, and writes `qm9.parquet`.

## Columns

| Column | Type | Units | Description |
|--------|------|-------|-------------|
| smiles | string | — | SMILES molecular string |
| n_C, n_N, n_O, n_F | int32 | — | Atom counts (estimated from SMILES) |
| n_heavy | int32 | — | Total heavy atom count |
| rot_A, rot_B, rot_C | float32 | GHz | Rotational constants |
| dipole | float32 | D | Dipole moment |
| polarizability | float32 | a₀³ | Isotropic polarizability |
| HOMO | float32 | Ha | Highest occupied molecular orbital energy |
| LUMO | float32 | Ha | Lowest unoccupied molecular orbital energy |
| gap | float32 | Ha | HOMO-LUMO gap |
| R2 | float32 | a₀² | Electronic spatial extent |
| ZPVE | float32 | Ha | Zero-point vibrational energy |
| U0 | float32 | Ha | Internal energy at 0 K |
| U298 | float32 | Ha | Internal energy at 298.15 K |
| H298 | float32 | Ha | Enthalpy at 298.15 K |
| G298 | float32 | Ha | Free energy at 298.15 K |
| Cv | float32 | cal/(mol·K) | Heat capacity at 298.15 K |
