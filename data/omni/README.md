# OMNI Hourly Solar Wind Data

Hourly near-Earth solar wind magnetic field and plasma parameters,
geomagnetic activity indices, and energetic proton fluxes, from 1963 to
present.

## Source

**NASA/GSFC Space Physics Data Facility (SPDF)**
- OMNIWeb interface: https://omniweb.gsfc.nasa.gov/ow.html
- Data documentation: https://omniweb.gsfc.nasa.gov/html/ow_data.html
- Download: https://spdf.gsfc.nasa.gov/pub/data/omni/low_res_omni/

The OMNI dataset merges data from multiple spacecraft (IMP-8, ACE, Wind,
DSCOVR, etc.) and time-shifts measurements to Earth's bow shock nose (BSN).
IMF and plasma data come from the solar wind monitor closest to the
Sun-Earth line at each time. Geomagnetic indices are from IAGA/WDC Kyoto.

## Build

```
python3 build_omni.py
```

Downloads `omni2_all_years.dat` (~184 MB), parses the fixed-width format,
replaces fill values with NaN/null, and writes `omni_hourly.parquet`.

## Columns

### Time
| Column | Type | Description |
|--------|------|-------------|
| year | int16 | Year |
| doy | int16 | Day of year (1-366) |
| hour | int8 | Hour (0-23) |

### Interplanetary magnetic field
| Column | Type | Units | Description |
|--------|------|-------|-------------|
| B_avg | float32 | nT | Scalar field magnitude average |
| B_mag | float32 | nT | Magnitude of vector-averaged field |
| Bx_gse | float32 | nT | IMF Bx in GSE coordinates |
| By_gse | float32 | nT | IMF By in GSE coordinates |
| Bz_gse | float32 | nT | IMF Bz in GSE coordinates |
| By_gsm | float32 | nT | IMF By in GSM coordinates |
| Bz_gsm | float32 | nT | IMF Bz in GSM coordinates |

### Solar wind plasma
| Column | Type | Units | Description |
|--------|------|-------|-------------|
| T_proton | float32 | K | Proton temperature |
| n_proton | float32 | #/cc | Proton number density |
| v_bulk | float32 | km/s | Bulk flow speed |
| flow_lon | float32 | deg | Flow longitude angle |
| flow_lat | float32 | deg | Flow latitude angle |
| alpha_ratio | float32 | — | Alpha-to-proton density ratio |
| P_flow | float32 | nPa | Ram pressure |

### Derived quantities
| Column | Type | Units | Description |
|--------|------|-------|-------------|
| E_field | float32 | mV/m | Motional electric field (-V×Bz) |
| plasma_beta | float32 | — | Ratio of plasma to magnetic pressure |
| alfven_mach | float32 | — | Alfvén Mach number |
| mach_ms | float32 | — | Magnetosonic Mach number |

### Geomagnetic activity indices
| Column | Type | Units | Description |
|--------|------|-------|-------------|
| Kp | float32 | — | Kp index (0.0–9.0) |
| sunspot_num | int16 | — | Daily sunspot number |
| Dst | int16 | nT | Disturbance storm time index |
| AE | int16 | nT | Auroral electrojet index |
| AL | int32 | nT | AL index (westward electrojet) |
| AU | int32 | nT | AU index (eastward electrojet) |
| ap | int16 | nT | ap index |
| PC_N | float32 | — | Polar cap (north) index |

### Energetic particles
| Column | Type | Units | Description |
|--------|------|-------|-------------|
| pflux_1MeV | float32 | 1/(cm² s sr) | Proton flux >1 MeV |
| pflux_10MeV | float32 | 1/(cm² s sr) | Proton flux >10 MeV |

### Solar activity
| Column | Type | Units | Description |
|--------|------|-------|-------------|
| f10_7 | float32 | sfu | F10.7 cm solar radio flux |

## Notes

- Data gaps vary by parameter; magnetic field is ~24% NaN, plasma ~27–42%,
  indices ~1–12%. Early years (1960s) have sparser coverage.
- Coordinate systems: GSE (Geocentric Solar Ecliptic) and GSM (Geocentric
  Solar Magnetospheric). Bz_gsm is the most geophysically important
  component — sustained negative Bz_gsm drives geomagnetic storms.
- During major storms, look for: negative Bz_gsm, high v_bulk, large
  negative Dst, elevated AE/Kp, and proton flux enhancements.
