# Shunya germination chamber firmware

Shared repository for the **Ujjain** and **Gurgaon** chamber firmware and, after commissioning, their OTA release artifacts.

The firmware sketch supplied in September 2026 has `DEFAULT_OTA_MANIFEST_URL` set to an empty string. An ESP32 can store a different manifest URL in NVS through its local configuration API, so this GitHub repository alone does not establish what URL is configured on either physical device. Verify `/api/status` and the saved system configuration on each chamber before an OTA release.

## Intended layout

- `chambers/ujjain/` — Ujjain hardware profile, source, pin map and tests.
- `chambers/gurgaon/` — Gurgaon profile after its actual firmware and wiring are verified.
- `shared/` — code shared deliberately between profiles.
- `germination-chamber/ujjain/` and `germination-chamber/gurgaon/` — distinct OTA release channels when authenticated updates and rollback are ready.

**No firmware binaries or OTA manifests are published from this branch.** Device profiles must never auto-update one another merely because they share this repository. Keep hardware safety controls local and independent of Wi-Fi.
