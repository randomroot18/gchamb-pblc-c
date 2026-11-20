# Five-relay chamber updates (stable)

Chambers on the five-relay board running Shunya 1.2.x read `manifest.json` in this folder:

```
https://raw.githubusercontent.com/randomroot18/gchamb-pblc-c/main/firmware/five-relay/stable/manifest.json
```

## Rules
- Only signed releases from `tools/make_release.sh` go here. A chamber rejects anything not signed by the release key, built for another product family (`pgc-five-relay-chamber` only), meant for another channel, or not newer than what it runs.
- The private key never goes in this repository or anywhere online.
- Upload the `.bin` first, then `manifest.json`. A changed file can take a few minutes to show at the URL above.
- Nothing installs on its own. Someone at the chamber confirms with the code on its screen.
- Firmware images here are public. Do not build them with an ntfy token or any other secret in `secrets.h`.

## Which folder serves which firmware
| Folder | Read by |
|---|---|
| `firmware/five-relay/stable/` | Five-relay board, Shunya 1.2.x (this folder) |
| `firmware/stable/` | Four-relay board, Shunya 1.0.5. Keep empty unless a 1.0.x release is planned. |
| `firmware/ujjain/`, `ota/ujjain/` | Older 1.0.4 and bootstrap builds. Left in place because firmware already holds these paths. |
| `firmware/ujjain-five-relay/` | Five-relay board on 1.1.0 (unsigned updates). Keep empty: units move to 1.2.0 over USB. |
