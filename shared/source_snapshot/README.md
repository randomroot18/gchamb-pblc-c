# Supplied field bootstrap sketch

`ProvidedSharedSketch.ino` is a byte-for-byte snapshot of the `Pasted text(2).txt` firmware supplied for review. It declares `FW_DEVICE_FAMILY` as `germination-chamber` and has an **empty** `DEFAULT_OTA_MANIFEST_URL`. Its live OTA URL, if configured, would be stored in each device's NVS. The snapshot does **not** establish which exact binary is flashed on Gurgaon or Ujjain.

This file is reference material. It is not a deployable Gurgaon profile and is not used by the Ujjain build. Verify the actual Gurgaon board, relay wiring, flash version, and `/api/status` before creating a Gurgaon release channel.
