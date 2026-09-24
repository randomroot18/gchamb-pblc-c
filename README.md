# Shunya germination chamber firmware

> **HOLD — DO NOT FLASH `chambers/ujjain/` YET.** PR #2 was merged while a newer, currently reported field sketch was being supplied. The Ujjain bench profile in this repository was derived from an **earlier** sketch, not the newly confirmed shared build. It also deliberately keeps the heater OFF. A successful CI compile does not establish compatibility with either physical chamber.

The user reports that **both Ujjain and Gurgaon currently run the same sketch**, which identifies itself as `0.1.0-gurgaon-bootstrap-ui` and `germination-chamber`. The newly supplied file includes live Wi-Fi credentials. Do **not** commit that raw file to this public repository.

That reported current sketch has four active-low relay outputs: GPIO26 humidifier, GPIO25 misting, GPIO19 recirculation and GPIO27 exhaust. It has **no GPIO13 heater control**. It initializes the TFT at 40 MHz, while an earlier Ujjain display needed 10 MHz to work reliably. Its compiled OTA default points to `https://randomroot18.github.io/gchamb-pblc-c/germination-chamber/manifest.json`; an OTA URL previously stored in NVS can override that default. No manifest or firmware binary has been published in this repository, and the main-branch manifest path is absent.

`shared/source_snapshot/ProvidedSharedSketch.ino` is an exact copy of the **earlier** `0.1.0-bootstrap` attachment. It is historical reference, not the currently reported flashed build. `chambers/ujjain/` is the earlier-source bench derivative; do not flash it as a replacement for the reported current code. A corrected Ujjain profile must be based on the newly supplied file, remove embedded credentials from source control, preserve the required network/UI behavior, confirm the real GPIO wiring and 10 MHz TFT behavior, and pass device tests before a release.

The repo remains shared for both chambers. Separate Ujjain and Gurgaon OTA channels and authenticated release rules are planned, **not live**. Verify each physical device's flashed version, OTA configuration and actual wiring before publishing any update. Keep independent hardware heater protection in place.
