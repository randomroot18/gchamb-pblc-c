# Shunya germination chamber firmware

This is the shared repository for the **Ujjain** and **Gurgaon** chambers. The `shared/source_snapshot/` folder preserves the exact sketch supplied for review; it is not proof of what is running on either physical device.

- `chambers/ujjain/` contains the separate Ujjain bench profile and build, with four used relays, no recirculation fan, and a heater locked OFF pending thermal tests.
- `chambers/gurgaon/` is intentionally not assigned a release profile yet. Obtain its actual firmware build, `/api/status` result, pin map and OTA URL first.
- `germination-chamber/ujjain/` and `germination-chamber/gurgaon/` are **future** OTA release paths. No manifests or firmware binaries are published here.

The supplied sketch's `DEFAULT_OTA_MANIFEST_URL` is empty. Each device may instead have an OTA manifest URL stored in NVS, so neither this source nor the repository name proves the live URL. An update manifest must identify the correct hardware profile, and release procedures must prevent Ujjain firmware from reaching Gurgaon and vice versa. Keep the heater safety loop local, independently protected and functional without the server or Wi-Fi.
