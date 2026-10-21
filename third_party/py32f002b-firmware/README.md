# PY32F002B firmware dependencies

Unmodified driver files from Puya's official firmware repository:

- Source: https://github.com/OpenPuya/PY32F002B_Firmware
- Release tag: `1.2.1`
- Commit: `394627d99085c5eae72142503b0405b84428bb20`

This directory contains the BSP, CMSIS headers, HAL headers, and HAL source
files needed by `MDK-ARM/Project.uvprojx`. Upstream examples and unrelated
drivers are omitted. Copyright and license notices are retained in the files;
the upstream repository license is included as `LICENSE`.

All 59 included upstream files were checked against the Git blob hashes at
the pinned commit. Their SHA-256 hashes are listed in `UPSTREAM-SHA256.txt`.

The Keil project uses paths relative to its own directory. It no longer
requires a sibling `PY32F002B_Firmware_V1.2.1` SDK directory.
