#!/usr/bin/env bash
# rename_runs.sh — one-time archival rename of existing run-sets to a consistent
# <batch>__<lmver>[__<validity>] scheme (see runs/README.md for the full manifest).
#
# Marks the three INVALID v1 sets (SF7 firmware bug), fixes the mislabeled
# formation_v1 (which is actually v2), and tags the valid v2-only sets. Uses
# `git mv` (runs/ is tracked) and is IDEMPOTENT: a mapping is skipped if the
# source is gone or the destination already exists, so it is safe to re-run.
#
# Review this list before running. Dry-run first:  DRY_RUN=1 bash rename_runs.sh
set -uo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/runs"
DRY_RUN="${DRY_RUN:-0}"

# src  ->  dst
MAP=(
  "sim_load_compare_prev       sim_load_compare__lmv1__INVALID_sf7bug"
  "sim_load_compare_v2         sim_load_compare__lmv2"
  "sim_load_compare_full_v1    sim_load_compare_full__lmv1__INVALID_sf7bug"
  "sim_load_compare_full_v2    sim_load_compare_full__lmv2"
  "sim_size_compare_v1         sim_size_compare__lmv1__INVALID_sf7bug"
  "sim_size_compare_v2         sim_size_compare__lmv2"
  "formation_v1                formation__lmv2__old0605"
  "formation                   formation__lmv2"
  "main_cluster_pdr            main_cluster_pdr__lmv2"
  "dataslots_check             dataslots_check__lmv2"
  "long_link                   long_link__lmv2"
)

moved=0 skipped=0
for row in "${MAP[@]}"; do
  read -r src dst <<< "$row"
  if [[ ! -d "$src" ]]; then echo "skip (no src):   $src"; skipped=$((skipped+1)); continue; fi
  if [[ -e "$dst" ]]; then echo "skip (dst exists): $dst"; skipped=$((skipped+1)); continue; fi
  echo "rename: $src  ->  $dst"
  if [[ "$DRY_RUN" == "1" ]]; then continue; fi
  git mv "$src" "$dst" 2>/dev/null || mv "$src" "$dst"
  moved=$((moved+1))
done
echo "done: $moved renamed, $skipped skipped$( [[ "$DRY_RUN" == "1" ]] && echo ' (dry-run)')"
