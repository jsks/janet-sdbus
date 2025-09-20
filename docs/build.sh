#!/usr/bin/env bash
#
# Converts index.md into a standalone html file for deployment to
# github pages.
###

set -eu

git_commit=$(git rev-parse --short HEAD)
build_date=$(date -u '+%F')

pandoc -c assets/site.css --embed-resources -s index.md \
       -V gitcommit="$git_commit" -V builddate="$build_date" \
       --template assets/template.html \
       --filter lib/highlight-filter.mjs \
       -o index.html
