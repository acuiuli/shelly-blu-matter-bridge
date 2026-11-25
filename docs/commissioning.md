# Hardware and commissioning

## Before flashing

Identify the exact Main Board revision, flash and PSRAM capacities, S3/H2 USB ports
and power supply. Save factory version information privately. The reference UART
is S3 RX GPIO17 / TX GPIO18 at 460800, with H2 boot/reset GPIOs 8/7 on the S3 side.
Verify against the [official board documentation](https://docs.espressif.com/projects/esp-thread-br/en/latest/hardware_platforms.html)
and the received schematic. Do not use the Ethernet Sub Board or factory BR host app.
Flash only after the two serial devices have been identified without ambiguity.

## Join the existing network

1. Confirm the existing border router and HA Matter integration work with a known
   Thread device. Confirm local IPv6 and multicast discovery between HA and the BR.
2. Build and flash the [prototype](prototype.md), preserving identity and credentials
   during upgrades. Configure [development certificate trust](#development-certificate-trust)
   on the controller before pairing this SDK-test-credential prototype.
3. Run `commissioning status` on the USB console. For initial pairing, expect
   `window_open=1 fabrics=0 ble_enabled=1 ble_advertising=1`. If the window has
   expired, run `commissioning open` to open it for 15 minutes, then check status
   again. An existing BLE connection can temporarily stop advertisements during
   pairing; do not interrupt an active exchange. The command preserves NVS and identity.
   Run `onboarding` to explicitly display the private development
   commissioning code. Keep that output out of shared logs. In the Home Assistant companion
   app, open Settings > Connectivity > Matter > Add device, choose a new device and
   enter the code manually. See the [current HA steps](https://www.home-assistant.io/integrations/matter/#adding-a-matter-device-to-home-assistant)
   for Android/iOS options and test-accessory prompts. Use the existing preferred Thread network.
   Do not create a second dataset/network to make an attach failure disappear.
   For this development board, accept an uncertified-device warning using Continue
   or Set up anyway when offered. The prototype uses SDK test attestation and is
   not presented as certified.
4. Check `diag`: router eligibility must be true. OpenThread role values are 0 disabled,
   1 detached, 2 child, 3 router, 4 leader. A child may be a router-eligible end device.
   Router role is elected; do not force it as a permanent condition.
5. Verify four climates, four percentage-capable valves, battery readings and grouping.
   Capture the Descriptor tree and sanitized diagnostics in a validation report.
6. Verify BLE central/scan operation after commissioning and during Thread traffic.
   Use `ble_scan 10` and `ble_scan status` to record passive advertisements. GATT,
   bonding and continued scanning after commissioning remain explicit transport gates.

No Thread dataset or bonding secret should be entered into an issue or committed.
The normal path provisions Thread credentials over Matter commissioning. USB is the
local path for flashing, configuration and diagnostics. Experimental direct BLE
enrollment and explicit valve-only setup are described in [one-valve bring-up](real-trv-bringup.md).
They require physical pairing mode and real-device validation; the Matter endpoints
remain simulated. The intended new-valve flow requires no Shelly app/account or
gateway and no second Matter commissioning of each downstream BLE valve.

## Development certificate trust

The prototype uses SDK test attestation credentials. Android's Continue / Set up
anyway prompt and Home Assistant Matter Server's attestation policy are separate
checks. If Matter Server reports that test/development certificates were rejected,
open the Matter Server app's Configuration in HA OS, enable **Enable test-net DCL
usage** (`enable_test_net_dcl: true`), save and restart that app before retrying.
This enables development/test trust on that controller; see the
[official app configuration](https://github.com/home-assistant/app-matter-server/blob/main/matter_server/DOCS.md#configuration).
Do not claim that this makes the firmware certified. Record the actual server
implementation/version and setting in validation results.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| Host starts but RCP does not respond | H2 image/SDK match, UART pins/baud, power, correct USB port |
| Phone says device not found | `commissioning status`, then `commissioning open` if expired; verify active BLE advertising, phone Bluetooth/permissions and short range |
| Thread remains detached | Existing network availability, commissioning credentials, radio range; inspect privately |
| Joins Thread but phone cannot reach it | Platform DNS-SD/SRP backend, service registration, IPv6 routing through the existing border router and local multicast discovery; attachment alone does not complete commissioning |
| Android accepts the warning but HA reports a generic error | Matter Server logs; test-certificate rejection requires the controller's development trust option above |
| HA cannot discover commissioned node | IPv6 routing/mDNS, controller/BR state, network isolation |
| Always child despite eligibility | Mesh election/topology; child role alone does not disprove eligibility |
| Valve appears separately or is missing | Descriptor parent links, Water Valve type, Level feature and HA version; record as a failed grouping gate |
| A valve becomes unavailable | Simulator fault, telemetry age, later BLE reachability; never infer physical closure from unavailable |
| Restart loses identity | Inspect `bridge_store` preservation and schema; do not erase NVS automatically |

Use warning-level logs for normal development; SDK debug logs can contain commissioning
material. Redact local captures before sharing. Keep the original Shelly gateway available
for physical recovery and test one real TRV only after the transport gate passes.
