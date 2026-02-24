# Nuclides Dataset

Ground-state nuclear properties for 3386 experimentally known nuclides.

## Source

**IAEA Nuclear Data Services (NDS) LiveChart API**
- API endpoint: `https://nds.iaea.org/relnsd/v1/data?fields=ground_states&nuclides=all`
- Interactive chart: https://nds.iaea.org/relnsd/vcharthtml/VChartHTML.html

The underlying data comes from:
- **ENSDF** (Evaluated Nuclear Structure Data File) for nuclear structure and decay data
- **AME2020 / NUBASE2020** (Atomic Mass Evaluation) for atomic masses and binding energies

## Build

```
python3 build_nuclides.py
```

Downloads the full 55-column CSV from the IAEA API, selects and transforms
columns, and writes `nuclides.parquet`.

## Columns

| Column | Type | Units | Description |
|--------|------|-------|-------------|
| Z | int16 | — | Proton number |
| N | int16 | — | Neutron number |
| A | int16 | — | Mass number (Z + N) |
| symbol | string | — | Element symbol |
| binding_per_A | float32 | keV | Binding energy per nucleon |
| mass_excess | float32 | keV | Mass excess |
| atomic_mass | float32 | μu | Atomic mass in micro-u |
| half_life_log10 | float32 | log₁₀(s) | Log₁₀ of half-life in seconds |
| jp | string | — | Spin-parity (e.g. "0+", "7/2-") |
| isospin | float32 | — | Isospin quantum number |
| radius | float32 | fm | Charge radius |
| abundance | float32 | % | Natural isotopic abundance |
| Sn | float32 | keV | One-neutron separation energy |
| Sp | float32 | keV | One-proton separation energy |
| Qa | float32 | keV | Q-value for alpha decay |
| Qbm | float32 | keV | Q-value for beta-minus decay |
| Qec | float32 | keV | Q-value for electron capture |
| Qbm_n | float32 | keV | Q-value for beta-delayed neutron emission |
| mag_dipole | float32 | μ_N | Magnetic dipole moment |
| elec_quadrupole | float32 | b | Electric quadrupole moment |
| decay_1 | string | — | Primary decay mode (e.g. "B-", "A", "EC") |
| decay_1_% | float32 | % | Primary decay branching ratio |
| decay_2 | string | — | Secondary decay mode |
| decay_2_% | float32 | % | Secondary decay branching ratio |
| decay_3 | string | — | Tertiary decay mode |
| decay_3_% | float32 | % | Tertiary decay branching ratio |
| discovery | float32 | year | Year of discovery |
