#!/usr/bin/env python3
"""
Download and process a PBMC single-cell RNA-seq dataset, then export
embeddings, marker genes, and QC metrics to Parquet for Viewpoints.

Install dependencies:
    pip install scanpy leidenalg pandas pyarrow

Run:
    python data/scrna/download_scrna.py
"""

import os
import warnings
import numpy as np
import pandas as pd
import scanpy as sc

warnings.filterwarnings("ignore", category=FutureWarning)

OUTPUT_FILE = "data/scrna/pbmc_scrna.parquet"

# Number of PCA components to export
N_PCS = 50

# Well-known PBMC marker genes (will keep whichever are present)
MARKER_GENES = [
    # T cells
    "CD3D", "CD3E", "CD3G",          # pan T cell
    "CD4",                            # CD4 T helper
    "CD8A", "CD8B",                   # CD8 cytotoxic T
    "IL7R",                           # naive/memory CD4 T
    "CCR7",                           # naive T / central memory
    "LEF1", "TCF7",                   # naive T
    "FOXP3", "IL2RA",                 # regulatory T
    "SELL",                           # naive lymphocytes (CD62L)
    "CD44",                           # activated/memory T
    "GZMB", "PRF1", "GZMA", "GZMK",  # cytotoxic effectors
    "IFNG", "TNF",                    # cytokines

    # B cells
    "MS4A1", "CD19", "CD79A", "CD79B",  # B cell markers
    "MZB1", "JCHAIN",                   # plasma cells

    # NK cells
    "NKG7", "GNLY", "KLRD1", "KLRB1",  # NK markers
    "NCAM1",                            # CD56

    # Monocytes / Macrophages
    "CD14", "LYZ", "S100A8", "S100A9",  # classical monocytes
    "FCGR3A", "MS4A7",                  # non-classical monocytes (CD16+)
    "CD68",                              # macrophage

    # Dendritic cells
    "FCER1A", "CST3", "CLEC10A",        # conventional DC
    "IRF7", "IRF8", "LILRA4",           # plasmacytoid DC

    # Megakaryocytes / Platelets
    "PPBP", "PF4", "GP1BB",

    # Erythrocytes
    "HBA1", "HBA2", "HBB",

    # Proliferation
    "MKI67", "TOP2A", "STMN1",

    # Housekeeping / QC-adjacent
    "MALAT1", "ACTB", "B2M",
]

# Cell type annotation based on canonical markers
CELL_TYPE_MARKERS = {
    "CD14+ Monocyte":   ["CD14", "LYZ", "S100A8", "S100A9"],
    "CD16+ Monocyte":   ["FCGR3A", "MS4A7"],
    "Dendritic cell":   ["FCER1A", "CST3"],
    "B cell":           ["MS4A1", "CD79A"],
    "CD4+ T cell":      ["CD3D", "IL7R", "CD4"],
    "CD8+ T cell":      ["CD3D", "CD8A", "CD8B"],
    "NK cell":          ["NKG7", "GNLY", "KLRD1"],
    "Megakaryocyte":    ["PPBP", "PF4"],
    "Plasma cell":      ["MZB1", "JCHAIN"],
    "Erythrocyte":      ["HBA1", "HBB"],
}


def download_pbmc():
    """Download PBMC data. Tries 10k first, falls back to 3k."""
    cache_dir = "data/scrna/cache"
    os.makedirs(cache_dir, exist_ok=True)

    # Try the 10x PBMC 10k v3 dataset
    url_10k = (
        "https://cf.10xgenomics.com/samples/cell-exp/3.0.0/"
        "pbmc_10k_v3/pbmc_10k_v3_filtered_feature_bc_matrix.h5"
    )
    h5_path = os.path.join(cache_dir, "pbmc_10k_v3_filtered_feature_bc_matrix.h5")

    if not os.path.exists(h5_path):
        print(f"Downloading PBMC 10k v3...")
        import urllib.request
        req = urllib.request.Request(url_10k, headers={"User-Agent": "Mozilla/5.0"})
        try:
            with urllib.request.urlopen(req) as resp, open(h5_path, "wb") as f:
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
                print(f"\n  Saved to {h5_path}")
        except Exception as e:
            if os.path.exists(h5_path):
                os.remove(h5_path)
            print(f"  10k download failed ({e}), falling back to 3k...")
            adata = sc.datasets.pbmc3k()
            print(f"  Loaded pbmc3k: {adata.shape}")
            return adata

    adata = sc.read_10x_h5(h5_path)
    adata.var_names_make_unique()
    print(f"Loaded: {adata.shape[0]:,} cells x {adata.shape[1]:,} genes")
    return adata


def preprocess(adata):
    """Standard scanpy preprocessing pipeline."""
    print("\n--- QC filtering ---")
    # Compute QC metrics
    adata.var["mt"] = adata.var_names.str.startswith("MT-")
    adata.var["ribo"] = adata.var_names.str.startswith(("RPS", "RPL"))
    sc.pp.calculate_qc_metrics(
        adata, qc_vars=["mt", "ribo"], percent_top=None, log1p=False, inplace=True
    )

    n_before = adata.n_obs
    sc.pp.filter_cells(adata, min_genes=200)
    sc.pp.filter_cells(adata, max_genes=5000)
    adata = adata[adata.obs["pct_counts_mt"] < 20].copy()
    sc.pp.filter_genes(adata, min_cells=3)
    print(f"  {n_before:,} -> {adata.n_obs:,} cells after QC")

    print("\n--- Normalization ---")
    # Save raw counts for later
    adata.layers["raw_counts"] = adata.X.copy()
    sc.pp.normalize_total(adata, target_sum=1e4)
    sc.pp.log1p(adata)
    adata.raw = adata  # freeze log-normalized data

    print("\n--- Feature selection ---")
    sc.pp.highly_variable_genes(adata, n_top_genes=2000, flavor="seurat_v3",
                                 layer="raw_counts")
    n_hvg = adata.var["highly_variable"].sum()
    print(f"  {n_hvg} highly variable genes selected")

    return adata


def compute_embeddings(adata):
    """Compute PCA, neighbors, UMAP, t-SNE, and Leiden clustering."""
    print("\n--- PCA ---")
    sc.pp.scale(adata, max_value=10)
    sc.tl.pca(adata, n_comps=N_PCS, svd_solver="arpack")
    print(f"  {N_PCS} components, variance explained: "
          f"{adata.uns['pca']['variance_ratio'][:5].sum():.1%} (first 5)")

    print("\n--- Neighbors + UMAP ---")
    sc.pp.neighbors(adata, n_neighbors=15, n_pcs=40)
    sc.tl.umap(adata)

    print("\n--- t-SNE ---")
    sc.tl.tsne(adata, n_pcs=40)

    print("\n--- Leiden clustering ---")
    for res in [0.3, 0.6, 1.0]:
        key = f"leiden_{str(res).replace('.', '_')}"
        sc.tl.leiden(adata, resolution=res, key_added=key, flavor="igraph")
        n_clusters = adata.obs[key].nunique()
        print(f"  resolution={res}: {n_clusters} clusters")

    return adata


def annotate_cell_types(adata):
    """Annotate cell types based on marker gene expression scores."""
    print("\n--- Cell type annotation ---")

    # Use the log-normalized data (stored in .raw)
    raw_df = pd.DataFrame(
        adata.raw.X.toarray() if hasattr(adata.raw.X, "toarray") else adata.raw.X,
        index=adata.obs_names,
        columns=adata.raw.var_names,
    )

    scores = {}
    for ct, markers in CELL_TYPE_MARKERS.items():
        present = [g for g in markers if g in raw_df.columns]
        if present:
            scores[ct] = raw_df[present].mean(axis=1)

    if scores:
        score_df = pd.DataFrame(scores, index=adata.obs_names)
        adata.obs["cell_type"] = score_df.idxmax(axis=1)
        adata.obs["cell_type_score"] = score_df.max(axis=1)

        # Assign "Unknown" where the best score is too low
        low_score = adata.obs["cell_type_score"] < 0.5
        adata.obs.loc[low_score, "cell_type"] = "Unknown"

        counts = adata.obs["cell_type"].value_counts()
        for ct, n in counts.items():
            print(f"  {ct:20s}: {n:5d}")
    else:
        adata.obs["cell_type"] = "Unknown"
        print("  No marker genes found, all cells labeled Unknown")

    return adata


def find_top_markers(adata):
    """Run differential expression to find top marker genes per cluster."""
    print("\n--- Differential expression (leiden_0_6) ---")
    sc.tl.rank_genes_groups(adata, groupby="leiden_0_6", method="wilcoxon",
                            use_raw=True)
    # Collect top 5 markers per cluster
    result = adata.uns["rank_genes_groups"]
    groups = result["names"].dtype.names
    top_genes = set()
    for g in groups:
        for gene in result["names"][g][:5]:
            top_genes.add(gene)
    print(f"  {len(top_genes)} top DE genes across {len(groups)} clusters")
    return top_genes


def build_parquet(adata, extra_markers):
    """Extract embeddings, markers, and metadata into a flat DataFrame."""
    print("\n--- Building Parquet ---")
    data = {}

    # UMAP
    data["UMAP_1"] = adata.obsm["X_umap"][:, 0].astype(np.float32)
    data["UMAP_2"] = adata.obsm["X_umap"][:, 1].astype(np.float32)

    # t-SNE
    data["tSNE_1"] = adata.obsm["X_tsne"][:, 0].astype(np.float32)
    data["tSNE_2"] = adata.obsm["X_tsne"][:, 1].astype(np.float32)

    # PCA components
    for i in range(min(N_PCS, adata.obsm["X_pca"].shape[1])):
        data[f"PC_{i+1}"] = adata.obsm["X_pca"][:, i].astype(np.float32)

    # QC metrics
    for col in ["n_genes_by_counts", "total_counts", "pct_counts_mt", "pct_counts_ribo"]:
        if col in adata.obs.columns:
            data[col] = adata.obs[col].values.astype(np.float32)

    # Cluster assignments
    for col in adata.obs.columns:
        if col.startswith("leiden_"):
            data[col] = adata.obs[col].astype(int).values

    # Cell type
    data["cell_type"] = adata.obs["cell_type"].values
    if "cell_type_score" in adata.obs.columns:
        data["cell_type_score"] = adata.obs["cell_type_score"].values.astype(np.float32)

    # Marker gene expression (log-normalized from .raw)
    raw_var_names = list(adata.raw.var_names)
    all_markers = set(MARKER_GENES) | extra_markers
    present_markers = sorted([g for g in all_markers if g in raw_var_names])

    raw_X = adata.raw.X
    if hasattr(raw_X, "toarray"):
        # Sparse matrix - extract columns by index for efficiency
        for gene in present_markers:
            idx = raw_var_names.index(gene)
            data[gene] = np.asarray(raw_X[:, idx].todense()).ravel().astype(np.float32)
    else:
        for gene in present_markers:
            idx = raw_var_names.index(gene)
            data[gene] = raw_X[:, idx].astype(np.float32)

    print(f"  Marker genes included: {len(present_markers)}")

    df = pd.DataFrame(data)
    return df


def main():
    os.makedirs(os.path.dirname(OUTPUT_FILE) or ".", exist_ok=True)
    sc.settings.verbosity = 1

    # 1) Download
    print("=== Download ===")
    adata = download_pbmc()

    # 2) Preprocess
    print("\n=== Preprocess ===")
    adata = preprocess(adata)

    # 3) Embeddings & clustering
    print("\n=== Embeddings ===")
    adata = compute_embeddings(adata)

    # 4) Cell type annotation
    print("\n=== Annotation ===")
    adata = annotate_cell_types(adata)

    # 5) DE markers
    extra_markers = find_top_markers(adata)

    # 6) Export
    print("\n=== Export ===")
    df = build_parquet(adata, extra_markers)

    df.to_parquet(OUTPUT_FILE, engine="pyarrow", index=False)
    mb = os.path.getsize(OUTPUT_FILE) / 1048576

    print(f"\nFile:    {OUTPUT_FILE}")
    print(f"Shape:   {df.shape[0]:,} rows x {df.shape[1]} columns")
    print(f"Size:    {mb:.1f} MB")

    # Column summary
    embed_cols = [c for c in df.columns if c.startswith(("UMAP", "tSNE", "PC_"))]
    meta_cols = [c for c in df.columns if c.startswith(("n_genes", "total_", "pct_", "leiden_", "cell_type"))]
    gene_cols = [c for c in df.columns if c not in embed_cols and c not in meta_cols]
    print(f"\nEmbeddings:  {len(embed_cols)} ({len([c for c in embed_cols if c.startswith('PC_')])} PCs + UMAP + t-SNE)")
    print(f"Metadata:    {len(meta_cols)} (QC + clusters + cell type)")
    print(f"Genes:       {len(gene_cols)} marker genes")


if __name__ == "__main__":
    main()
