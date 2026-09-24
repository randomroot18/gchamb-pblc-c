# Ujjain remote monitoring and OTA staging

The reported current Ujjain firmware already serves `GET /api/status` on its LAN and can persist an HTTPS OTA manifest URL through `POST /api/config`. It is also reported on Gurgaon, so update **only Ujjain's own IP**. The firmware family is still generic (`germination-chamber`), and the GitHub Pages URL compiled into that current build is absent from this repository.

## At the Ujjain controller's current LAN

Identify the Ujjain ESP32 IP from its serial log, DHCP leases, or local dashboard. Check `http://DEVICE_IP/api/status` and confirm `fw` is `0.1.0-gurgaon-bootstrap-ui` and `device_id` is the intended board before posting anything. The code file supplied by the user is a source claim, not a remote attestation of its flashed bytes.

From PowerShell, with **only Ujjain's verified IP** substituted:

```powershell
Invoke-RestMethod -Method Post -Uri 'http://DEVICE_IP/api/config?ota_url=https%3A%2F%2Fraw.githubusercontent.com%2Frandomroot18%2Fgchamb-pblc-c%2Fmain%2Fota%2Fujjain%2Fmanifest.json&ota_min=2'
Invoke-RestMethod -Uri 'http://DEVICE_IP/api/check-update'
Start-Sleep -Seconds 12
(Invoke-RestMethod -Uri 'http://DEVICE_IP/api/status').ota
```

An OTA result saying the server offers an **older 0.0.0** version confirms the safe hold manifest was fetched and parsed. **It installs nothing.** A TLS or HTTP error means OTA has not been established. The next scheduled check may retain its old deadline (possibly up to 12 hours) until the controller reconnects or reboots; changing `ota_min` does not reschedule that already-set deadline in this version. Do not reset a running chamber merely to accelerate a test.

## On a computer that stays on the **same LAN as Ujjain**

Run the read-only poller from this repo with Python 3. It polls every 10 seconds, logs successes and failures in SQLite, and serves a password-protected dashboard locally:

```powershell
$env:UJJAIN_MONITOR_PASSWORD = 'choose-a-long-unique-password'
python tools/monitor_ujjain.py --device http://DEVICE_IP
```

To view that dashboard from home, both your home device and this computer need access to the same Tailscale network. On the always-on computer, Tailscale Serve can privately proxy local port 8765:

```powershell
tailscale serve --bg localhost:8765
tailscale serve status
```

Open the HTTPS URL printed by Tailscale from home; log in as `admin` with the password set above. Keep the Python process running. If Tailscale is not installed/configured on this computer, the dashboard will remain local; **no remote stream is live**. Do not publicly expose the chamber's unauthenticated port 80.

## Future OTA release hold

The current manifest deliberately says version `0.0.0` and points to no published binary. It cannot upgrade a reported `0.1.0` chamber. Before releasing a higher version, build and test a Ujjain-specific image, verify the exact flash partition layout and rollback/recovery path, and compute its SHA-256. The heater is not implemented in the reported current build. A repository commit by itself does **not** create a signed or validated OTA firmware release.
