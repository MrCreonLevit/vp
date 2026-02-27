#!/usr/bin/env python3
"""
Download a Sentinel-2 L2A scene from Microsoft Planetary Computer,
extract all spectral bands + ancillary layers + spectral indices,
and save as a Parquet file with per-pixel XY coordinates.

Install dependencies:
    pip install pystac-client planetary-computer rasterio rioxarray pandas pyarrow

Run:
    python data/sentinel2/download_sentinel2.py
"""

import os
import numpy as np
import pandas as pd
import planetary_computer
import pystac_client
import rasterio
from pyproj import Transformer
from rasterio.enums import Resampling
from rasterio.windows import Window

# ── Configuration ──────────────────────────────────────────────────────────

BBOX = [-122.65, 37.60, -122.25, 37.90]   # SF Bay Area (search region)
TIME_RANGE = "2024-06-01/2024-09-30"       # summer for clear skies
MAX_CLOUD_COVER = 5                         # percent

CROP_SIZE = 3156         # pixels per side (~9.96 Mpx), divisible by 6
OUTPUT_FILE = "data/sentinel2/sentinel2_sf_bay.parquet"

# Band definitions by native resolution
BANDS_10M = ["B02", "B03", "B04", "B08"]
BANDS_20M = ["B05", "B06", "B07", "B8A", "B11", "B12"]
BANDS_60M = ["B01", "B09"]
ANCILLARY_20M = ["SCL"]
ANCILLARY_10M = ["AOT", "WVP"]

BAND_LABELS = {
    "B01": "coastal_aerosol",
    "B02": "blue",
    "B03": "green",
    "B04": "red",
    "B05": "red_edge_1",
    "B06": "red_edge_2",
    "B07": "red_edge_3",
    "B08": "nir",
    "B8A": "nir_narrow",
    "B09": "water_vapor_band",
    "B11": "swir_1",
    "B12": "swir_2",
    "SCL": "scene_classification",
    "AOT": "aerosol_thickness",
    "WVP": "water_vapor",
}


# ── Functions ──────────────────────────────────────────────────────────────

def find_scene():
    """Search Planetary Computer for the least-cloudy scene covering BBOX center."""
    from shapely.geometry import shape, Point

    catalog = pystac_client.Client.open(
        "https://planetarycomputer.microsoft.com/api/stac/v1",
        modifier=planetary_computer.sign_inplace,
    )
    results = catalog.search(
        collections=["sentinel-2-l2a"],
        bbox=BBOX,
        datetime=TIME_RANGE,
        query={"eo:cloud_cover": {"lt": MAX_CLOUD_COVER}},
        max_items=30,
    )

    # Filter to scenes whose footprint actually contains the BBOX center
    center = Point((BBOX[0] + BBOX[2]) / 2, (BBOX[1] + BBOX[3]) / 2)
    candidates = []
    for item in results.items():
        if shape(item.geometry).contains(center):
            candidates.append(item)

    if not candidates:
        raise RuntimeError(
            "No scenes found covering BBOX center. "
            "Try widening TIME_RANGE or MAX_CLOUD_COVER."
        )

    # Pick the least cloudy
    candidates.sort(key=lambda it: it.properties["eo:cloud_cover"])
    item = candidates[0]
    props = item.properties
    tile = props.get("s2:mgrs_tile", "?")
    print(f"Scene:  {item.id}")
    print(f"Tile:   {tile}")
    print(f"Date:   {props['datetime']}")
    print(f"Cloud:  {props['eo:cloud_cover']:.1f}%")
    print(f"Assets: {', '.join(sorted(item.assets.keys()))}")
    print(f"Candidates checked: {len(candidates)} scenes covering BBOX center")
    return item


def compute_crop_window(item):
    """Compute a crop window centered on BBOX, aligned to all resolution grids."""
    with rasterio.open(item.assets["B04"].href) as src:
        ref_h, ref_w = src.height, src.width
        transform_full = src.transform
        crs = src.crs

    # Convert BBOX center (lon/lat) to the tile's projected CRS (UTM)
    bbox_cx = (BBOX[0] + BBOX[2]) / 2
    bbox_cy = (BBOX[1] + BBOX[3]) / 2
    proj = Transformer.from_crs("EPSG:4326", crs, always_xy=True)
    utm_cx, utm_cy = proj.transform(bbox_cx, bbox_cy)

    # Convert UTM center to pixel coordinates in the tile
    inv_transform = ~transform_full
    px_cx, px_cy = inv_transform * (utm_cx, utm_cy)

    size = min(CROP_SIZE, ref_h, ref_w)
    size = (size // 6) * 6  # align to 60m grid (LCM of 1, 2, 6 in 10m pixels)

    # Center the crop on the BBOX center, clamped to tile bounds, aligned to 6px
    x_off = int(px_cx - size / 2)
    y_off = int(px_cy - size / 2)
    x_off = max(0, min(x_off, ref_w - size))
    y_off = max(0, min(y_off, ref_h - size))
    x_off = (x_off // 6) * 6
    y_off = (y_off // 6) * 6

    window = Window(x_off, y_off, size, size)
    crop_transform = rasterio.windows.transform(window, transform_full)

    print(f"Tile:   {ref_w}x{ref_h} px")
    print(f"BBOX center: ({bbox_cx:.3f}, {bbox_cy:.3f}) -> UTM ({utm_cx:.0f}, {utm_cy:.0f})")
    print(f"Crop:   {size}x{size} px ({size * size / 1e6:.2f} Mpx) at offset ({x_off}, {y_off})")
    print(f"CRS:    {crs}")

    return window, size, crop_transform, crs, (ref_h, ref_w)


def read_band(item, key, window_10m, crop_size, ref_shape):
    """Read a single band, resampling to the 10m crop grid if needed."""
    if key not in item.assets:
        print(f"  {key}: not available, skipping")
        return None

    href = item.assets[key].href
    is_categorical = key == "SCL"

    with rasterio.open(href) as src:
        native_h, native_w = src.height, src.width

    # Determine resolution ratio relative to 10m reference
    ref_h, ref_w = ref_shape
    ratio_w = ref_w // native_w if native_w > 0 else 1
    ratio_h = ref_h // native_h if native_h > 0 else 1
    ratio = ratio_w if ratio_w == ratio_h and ratio_w in (1, 2, 6) else 1

    if ratio == 1:
        window = window_10m
    else:
        window = Window(
            window_10m.col_off // ratio,
            window_10m.row_off // ratio,
            window_10m.width // ratio,
            window_10m.height // ratio,
        )

    with rasterio.open(href) as src:
        data = src.read(
            1,
            window=window,
            out_shape=(crop_size, crop_size),
            resampling=Resampling.nearest if is_categorical else Resampling.bilinear,
        )

    label = BAND_LABELS.get(key, key)
    print(f"  {key:4s} -> {label:22s}  [{native_w}x{native_h}, ratio={ratio}]")
    return data


def compute_indices(b):
    """Compute spectral indices from uint16 surface-reflectance bands (scale=10000)."""
    nir = b["B08"].astype(np.float32)
    red = b["B04"].astype(np.float32)
    grn = b["B03"].astype(np.float32)
    blu = b["B02"].astype(np.float32)
    sw1 = b["B11"].astype(np.float32)
    sw2 = b["B12"].astype(np.float32)
    re1 = b["B05"].astype(np.float32)
    re2 = b["B06"].astype(np.float32)
    re3 = b["B07"].astype(np.float32)
    nna = b["B8A"].astype(np.float32)
    eps = 1e-10

    def ndi(a, b):
        """Normalized difference: (a - b) / (a + b)."""
        return (a - b) / (a + b + eps)

    idx = {}

    # ── Vegetation ─────────────────────────────────────────────────────
    idx["ndvi"]    = ndi(nir, red)
    idx["re_ndvi"] = ndi(re3, re1)                         # red-edge NDVI
    idx["evi"]     = np.clip(                               # enhanced veg index
        2.5 * (nir - red) / (nir + 6.0 * red - 7.5 * blu + 10000.0), -1, 1)
    idx["savi"]    = np.clip(                               # soil-adjusted veg
        1.5 * (nir - red) / (nir + red + 5000.0), -1, 1)

    # ── Water ──────────────────────────────────────────────────────────
    idx["ndwi"]    = ndi(grn, nir)                          # McFeeters
    idx["mndwi"]   = ndi(grn, sw1)                          # modified NDWI

    # ── Moisture & burn ────────────────────────────────────────────────
    idx["ndmi"]    = ndi(nir, sw1)                          # moisture
    idx["nbr"]     = ndi(nir, sw2)                          # burn ratio

    # ── Built-up & soil ────────────────────────────────────────────────
    idx["ndbi"]    = ndi(sw1, nir)                          # built-up
    idx["bsi"]     = (                                      # bare soil
        ((sw1 + red) - (nir + blu)) / ((sw1 + red) + (nir + blu) + eps))

    # ── Chlorophyll / red-edge ─────────────────────────────────────────
    idx["mcari"]   = (                                      # chlorophyll absorption
        ((re1 - red) - 0.2 * (re1 - grn)) * (re1 / (red + eps)) / 1e8)
    idx["cri"]     = np.where(                              # carotenoid reflectance
        (blu > 0) & (re1 > 0), 1.0 / blu - 1.0 / re1, 0.0).astype(np.float32)

    # ── Band ratios (simple, sometimes useful) ─────────────────────────
    idx["nir_red_ratio"]   = nir / (red + eps)
    idx["swir_nir_ratio"]  = sw1 / (nir + eps)
    idx["red_edge_slope"]  = (re3 - re1) / (20.0 + eps)    # Δrefl over ~40nm

    return idx


# ── Main ───────────────────────────────────────────────────────────────────

def main():
    os.makedirs(os.path.dirname(OUTPUT_FILE) or ".", exist_ok=True)

    # 1) Find a good scene
    print("─── Searching ───")
    item = find_scene()

    # 2) Compute crop window
    print("\n─── Crop window ───")
    window, size, crop_transform, crs, ref_shape = compute_crop_window(item)

    # 3) Read all bands
    print("\n─── Reading bands ───")
    all_keys = BANDS_10M + BANDS_20M + BANDS_60M + ANCILLARY_20M + ANCILLARY_10M
    bands = {}
    for key in all_keys:
        data = read_band(item, key, window, size, ref_shape)
        if data is not None:
            bands[key] = data

    # 4) Pixel coordinates (UTM, at pixel centers)
    print("\n─── Coordinates ───")
    rows_ix, cols_ix = np.mgrid[0:size, 0:size]
    t = crop_transform
    xs = t.c + (cols_ix + 0.5) * t.a    # easting
    ys = t.f + (rows_ix + 0.5) * t.e    # northing
    print(f"X (easting):  {xs.min():.1f} .. {xs.max():.1f}")
    print(f"Y (northing): {ys.min():.1f} .. {ys.max():.1f}")

    # 5) Spectral indices
    print("\n─── Computing indices ───")
    indices = compute_indices(bands)
    print(f"Computed {len(indices)} indices")

    # 6) Assemble dataframe
    print("\n─── Assembling Parquet ───")
    data = {
        "x_utm": xs.ravel().astype(np.float64),
        "y_utm": ys.ravel().astype(np.float64),
        "row":   rows_ix.ravel().astype(np.int16),
        "col":   cols_ix.ravel().astype(np.int16),
    }

    # Spectral bands (uint16 surface reflectance, scale factor 10000)
    for key in BANDS_10M + BANDS_20M + BANDS_60M:
        if key in bands:
            data[BAND_LABELS[key]] = bands[key].ravel()

    # Ancillary layers
    for key in ANCILLARY_20M + ANCILLARY_10M:
        if key in bands:
            arr = bands[key].ravel()
            data[BAND_LABELS[key]] = arr.astype(np.uint8) if key == "SCL" else arr

    # Spectral indices (float32)
    for name, arr in indices.items():
        data[name] = arr.ravel().astype(np.float32)

    df = pd.DataFrame(data)
    df.to_parquet(OUTPUT_FILE, engine="pyarrow", index=False)

    mb = os.path.getsize(OUTPUT_FILE) / 1048576
    print(f"\n─── Done ───")
    print(f"File:    {OUTPUT_FILE}")
    print(f"Shape:   {df.shape[0]:,} rows x {df.shape[1]} columns")
    print(f"Size:    {mb:.1f} MB")
    print(f"Columns: {', '.join(df.columns)}")


if __name__ == "__main__":
    main()
