#!/usr/bin/env python3
"""
Download MLB Statcast pitch-by-pitch data for a full season and convert
to Parquet for Viewpoints.

Source: Baseball Savant / MLB Statcast via pybaseball.

Install dependencies:
    pip install pybaseball pandas pyarrow

Run:
    python data/statcast/download_statcast.py
"""

import os
import numpy as np
import pandas as pd
import pybaseball

OUTPUT_FILE = "data/statcast/statcast_2024.parquet"

# 2024 MLB regular season
SEASON_START = "2024-03-28"
SEASON_END = "2024-09-29"

# Columns to keep, grouped by category
KEEP_COLUMNS = [
    # ── Pitch identification ──────────────────────────────────────────
    "pitch_type",           # two-letter code (FF, SL, CH, CU, etc.)
    "pitch_name",           # full name (4-Seam Fastball, Slider, etc.)
    "game_date",            # date of game

    # ── Pitch physics ────────────────────────────────────────────────
    "release_speed",        # pitch velocity at release (mph)
    "release_pos_x",       # horizontal release position (ft, catcher view)
    "release_pos_y",       # release distance from home plate (ft)
    "release_pos_z",       # vertical release position (ft)
    "release_spin_rate",    # spin rate (rpm)
    "spin_axis",            # spin axis (degrees)
    "release_extension",    # extension toward home plate (ft)
    "effective_speed",      # perceived velocity at plate (mph)
    "arm_angle",            # arm slot angle (degrees)

    # ── Pitch movement ───────────────────────────────────────────────
    "pfx_x",               # horizontal movement (inches, catcher POV)
    "pfx_z",               # vertical movement (inches)
    "api_break_z_with_gravity",  # vertical break with gravity (inches)
    "api_break_x_arm",          # horizontal break (arm-side, inches)
    "api_break_x_batter_in",    # horizontal break (batter-side, inches)

    # ── Pitch trajectory (initial conditions at y=50ft) ──────────────
    "vx0", "vy0", "vz0",   # velocity components (ft/s)
    "ax", "ay", "az",       # acceleration components (ft/s^2)

    # ── Pitch location at plate ──────────────────────────────────────
    "plate_x",              # horizontal position at plate (ft)
    "plate_z",              # vertical position at plate (ft)
    "zone",                 # strike zone region (1-9 strike, 11-14 ball)
    "sz_top",               # top of batter's strike zone (ft)
    "sz_bot",               # bottom of batter's strike zone (ft)

    # ── Batted ball ──────────────────────────────────────────────────
    "launch_speed",         # exit velocity (mph)
    "launch_angle",         # launch angle (degrees)
    "hit_distance_sc",      # projected hit distance (ft)
    "hc_x",                 # hit coordinate X (spray chart)
    "hc_y",                 # hit coordinate Y (spray chart)
    "bb_type",              # batted ball type (ground_ball, line_drive, etc.)
    "hyper_speed",          # bat-to-ball speed ratio

    # ── Expected stats ───────────────────────────────────────────────
    "estimated_ba_using_speedangle",    # xBA
    "estimated_woba_using_speedangle",  # xwOBA
    "estimated_slg_using_speedangle",   # xSLG
    "launch_speed_angle",               # launch speed/angle bucket

    # ── Outcome ──────────────────────────────────────────────────────
    "type",                 # B=ball, S=strike, X=in play
    "events",               # outcome (single, strikeout, home_run, etc.)
    "description",          # pitch-level description
    "woba_value",           # wOBA value of the event
    "babip_value",          # BABIP value
    "iso_value",            # ISO value
    "delta_run_exp",        # change in run expectancy
    "delta_home_win_exp",   # change in win expectancy

    # ── Game context ─────────────────────────────────────────────────
    "balls", "strikes",     # count
    "outs_when_up",         # outs in the inning
    "inning",               # inning number
    "inning_topbot",        # Top/Bot
    "stand",                # batter handedness (L/R)
    "p_throws",             # pitcher handedness (L/R)
    "home_team", "away_team",
    "bat_score", "fld_score",
    "home_win_exp",         # pre-pitch win expectancy
    "bat_win_exp",          # batting team win expectancy
    "pitch_number",         # pitch number in the at-bat
    "at_bat_number",        # at-bat number in the game
    "n_thruorder_pitcher",  # times through the order

    # ── Player info ──────────────────────────────────────────────────
    "player_name",          # pitcher name
    "batter",               # batter MLB ID
    "pitcher",              # pitcher MLB ID
    "age_bat",              # batter age
    "age_pit",              # pitcher age
]


def add_derived_columns(df):
    """Add computed columns useful for visualization."""

    # ── Count state as single integer (e.g. 32 = 3-2 count) ─────────
    if "balls" in df.columns and "strikes" in df.columns:
        df["count_state"] = (df["balls"] * 10 + df["strikes"]).astype(np.int8)

    # ── Pitch location relative to strike zone ──────────────────────
    if all(c in df.columns for c in ["plate_x", "plate_z", "sz_top", "sz_bot"]):
        sz_mid_z = (df["sz_top"] + df["sz_bot"]) / 2
        sz_height = df["sz_top"] - df["sz_bot"]
        # Normalized: 0 = center of zone, 1 = edge of zone
        df["plate_z_norm"] = ((df["plate_z"] - sz_mid_z) / (sz_height / 2)).astype(np.float32)
        # Zone width is ~17 inches = 1.417 ft, half-width = 0.708 ft
        df["plate_x_norm"] = (df["plate_x"] / 0.708).astype(np.float32)
        # Distance from zone center (normalized)
        df["dist_from_zone_center"] = np.sqrt(
            df["plate_x_norm"]**2 + df["plate_z_norm"]**2
        ).astype(np.float32)
        # In-zone flag
        df["in_zone"] = (
            (df["plate_x"].abs() <= 0.708) &
            (df["plate_z"] >= df["sz_bot"]) &
            (df["plate_z"] <= df["sz_top"])
        ).astype(np.int8)

    # ── Total movement magnitude ────────────────────────────────────
    if "pfx_x" in df.columns and "pfx_z" in df.columns:
        df["total_movement"] = np.sqrt(
            df["pfx_x"]**2 + df["pfx_z"]**2
        ).astype(np.float32)

    # ── Velocity drop (release to plate) ────────────────────────────
    if "release_speed" in df.columns and "effective_speed" in df.columns:
        df["velo_drop"] = (df["release_speed"] - df["effective_speed"]).astype(np.float32)

    # ── Spray angle from hit coordinates ────────────────────────────
    # hc_x/hc_y are in the Baseball Savant coordinate system
    # Home plate is at roughly (125.42, 198.27)
    if "hc_x" in df.columns and "hc_y" in df.columns:
        hp_x, hp_y = 125.42, 198.27
        dx = df["hc_x"] - hp_x
        dy = hp_y - df["hc_y"]  # flip Y (field goes "up")
        df["spray_angle"] = np.degrees(np.arctan2(dx, dy)).astype(np.float32)

    # ── Score differential ──────────────────────────────────────────
    if "bat_score" in df.columns and "fld_score" in df.columns:
        df["score_diff"] = (df["bat_score"] - df["fld_score"]).astype(np.int8)

    # ── Leverage (absolute win expectancy change) ───────────────────
    if "delta_home_win_exp" in df.columns:
        df["abs_delta_win_exp"] = df["delta_home_win_exp"].abs().astype(np.float32)

    # ── Pitch category (simplified) ─────────────────────────────────
    if "pitch_type" in df.columns:
        category_map = {
            "FF": "Fastball", "SI": "Fastball", "FC": "Fastball",
            "FA": "Fastball",
            "SL": "Breaking", "CU": "Breaking", "KC": "Breaking",
            "SV": "Breaking", "ST": "Breaking",
            "CH": "Offspeed", "FS": "Offspeed", "FO": "Offspeed",
            "SC": "Offspeed", "KN": "Offspeed",
            "CS": "Breaking",
        }
        df["pitch_category"] = df["pitch_type"].map(
            lambda x: category_map.get(x, "Other") if pd.notna(x) else "Other"
        )

    # ── Whiff flag (swinging strike) ────────────────────────────────
    if "description" in df.columns:
        whiff_descs = {"swinging_strike", "swinging_strike_blocked",
                       "foul_tip", "missed_bunt"}
        df["is_whiff"] = df["description"].isin(whiff_descs).astype(np.int8)

    # ── Swing flag ──────────────────────────────────────────────────
    if "description" in df.columns:
        swing_descs = {"swinging_strike", "swinging_strike_blocked",
                       "foul", "foul_tip", "foul_bunt", "missed_bunt",
                       "hit_into_play"}
        df["is_swing"] = df["description"].isin(swing_descs).astype(np.int8)

    # ── Called strike flag ──────────────────────────────────────────
    if "description" in df.columns:
        df["is_called_strike"] = (df["description"] == "called_strike").astype(np.int8)

    # ── Contact quality buckets from launch speed/angle ─────────────
    if "launch_speed" in df.columns and "launch_angle" in df.columns:
        ls = df["launch_speed"]
        la = df["launch_angle"]
        # "Barrel" definition per MLB: exit velo >= 98 mph and
        # launch angle in sweet spot (roughly 26-30 degrees at 98,
        # widening with higher velo). Simplified version:
        df["is_barrel"] = (
            (ls >= 98) & (la >= 26) & (la <= 30 + (ls - 98) * 0.5)
        ).astype(np.int8)
        # Hard hit: >= 95 mph
        df["is_hard_hit"] = (ls >= 95).astype(np.int8)

    return df


def main():
    os.makedirs(os.path.dirname(OUTPUT_FILE) or ".", exist_ok=True)

    # Enable caching to avoid re-downloading on subsequent runs
    pybaseball.cache.enable()

    # Download the full 2024 regular season
    print(f"=== Downloading Statcast data: {SEASON_START} to {SEASON_END} ===")
    print("This downloads in weekly chunks and may take a while...")
    df = pybaseball.statcast(SEASON_START, SEASON_END)
    print(f"\nRaw download: {len(df):,} pitches x {len(df.columns)} columns")

    # Keep only selected columns (skip any that don't exist)
    present = [c for c in KEEP_COLUMNS if c in df.columns]
    missing = [c for c in KEEP_COLUMNS if c not in df.columns]
    if missing:
        print(f"Note: {len(missing)} requested columns not found: {missing}")
    df = df[present].copy()
    print(f"Kept: {len(df.columns)} columns")

    # Convert nullable integer types to standard numpy types for Parquet
    for col in df.columns:
        if df[col].dtype == "Int64":
            if col in ("batter", "pitcher", "game_pk"):
                df[col] = df[col].astype("int32")
            else:
                df[col] = df[col].astype(np.float32)
        elif df[col].dtype == "Float64":
            df[col] = df[col].astype(np.float32)

    # Convert game_date to string for Parquet compatibility
    if "game_date" in df.columns:
        df["game_date"] = df["game_date"].dt.strftime("%Y-%m-%d")

    # Add derived columns
    print("\n=== Computing derived columns ===")
    n_before = len(df.columns)
    df = add_derived_columns(df)
    n_derived = len(df.columns) - n_before
    print(f"Added {n_derived} derived columns")

    # Report completeness
    print(f"\n=== Column completeness ===")
    total = len(df)
    for col in sorted(df.columns):
        non_null = df[col].notna().sum()
        pct = 100.0 * non_null / total
        dtype_str = str(df[col].dtype)[:8]
        fill = "***" if pct < 50 else ""
        print(f"  {col:45s}  {dtype_str:8s}  {non_null:7d}/{total}  ({pct:5.1f}%) {fill}")

    # Write Parquet
    print(f"\n=== Writing Parquet ===")
    df.to_parquet(OUTPUT_FILE, engine="pyarrow", index=False)

    mb = os.path.getsize(OUTPUT_FILE) / 1048576
    print(f"\nFile:    {OUTPUT_FILE}")
    print(f"Shape:   {df.shape[0]:,} rows x {df.shape[1]} columns")
    print(f"Size:    {mb:.1f} MB")

    # Column breakdown
    pitch_cols = [c for c in df.columns if any(
        c.startswith(p) for p in ("release_", "pfx_", "plate_", "spin_", "vx", "vy", "vz", "ax", "ay", "az", "api_break", "arm_", "effective_")
    )]
    batted_cols = [c for c in df.columns if any(
        c.startswith(p) for p in ("launch_", "hit_", "hc_", "bb_", "estimated_", "hyper", "spray", "is_barrel", "is_hard")
    )]
    context_cols = [c for c in df.columns if any(
        c.startswith(p) for p in ("balls", "strikes", "outs", "inning", "score", "bat_", "fld_", "home_", "away_", "n_thru", "count_", "pitch_number", "at_bat")
    )]
    print(f"\nPitch physics:  {len(pitch_cols)} columns")
    print(f"Batted ball:    {len(batted_cols)} columns")
    print(f"Game context:   {len(context_cols)} columns")


if __name__ == "__main__":
    main()
