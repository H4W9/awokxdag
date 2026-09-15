# Changelog

All notable changes to AxD are documented here. This project follows
[Semantic Versioning](https://semver.org/).

## [Unreleased]

## [1.3.5] - 2026-09-15

### Added

- Flicker-free Touch UI. Live views (Status, Packet Monitor, Client Sniffer,
  Probe Lure, Wardrive, Cameras, and the rest) redraw on 0.5-1 s timers, and
  each starts by clearing the whole screen; drawing straight to the ILI9341
  made that clear-then-repaint visible as a flash every refresh. The Touch now
  renders into a ~150 KB off-screen back buffer in PSRAM (`touch_display.h`,
  `AwokTouchDisplay`) and blits it to the panel once per frame, dirty-gated, so
  a refresh appears atomically. Every existing `display.*` call is unchanged
  (the wrapper is a drop-in `Adafruit_GFX`); a single `present()` at the end of
  `loop()` pushes the frame, with explicit pushes where a screen is drawn right
  before a blocking operation (boot splash, "Scanning..."). Boards without
  PSRAM (classic ESP32 Touch, experimental) fall back to direct-to-panel
  drawing, exactly as before.

### Fixed

- Added a 0.75 s settle gap between Wi-Fi and BLE when the wardrive/cameras/
  advanced-watch scheduler switches radios. The controller/driver deinit
  returns before the hardware and its DMA are fully released; bringing the
  other radio up immediately overlapped that teardown and could re-trigger the
  "Memory Capacity Exceeded" fault. The scheduler now waits for one radio to
  fully release before the other claims the DMA.

## [1.3.4] - 2026-09-13

### Fixed

- BLE scanning failed every time on Dual C5 Touch in wardrive (and every other
  Wi-Fi+BLE view) with `NimBLEScan: Error starting scan; rc=519` — invisible
  until now because NimBLE-Arduino's own error logging is compiled out at the
  default `CORE_DEBUG_LEVEL=0`. `rc=519` decodes to `BLE_HS_HCI_ERR(7)`,
  Bluetooth HCI error `0x07` "Memory Capacity Exceeded". Per-capability heap
  instrumentation traced it to **DMA-capable SRAM exhaustion during Wi-Fi+BLE
  coexistence**, not the general heap: the C5 has a ~70 KB DMA pool, Wi-Fi's
  driver takes ~44 KB of it (fixed overhead — shrinking the buffer counts moved
  it <2 KB), and the BLE controller takes the remaining ~26 KB at init, leaving
  24 bytes — so scan-enable, which needs its own DMA buffer, is refused. The
  two radios simply do not fit in DMA at the same time, and neither pool is
  relocatable: Wi-Fi's is fixed driver/coex memory, and the BLE controller's
  bulk is the MSYS mbuf pool, which on the C5 is allocated inside the closed
  controller blob (`CONFIG_BT_LE_MSYS_INIT_IN_CONTROLLER=1`) and cannot be
  moved to PSRAM or shrunk by config. (This is why the 1.3.2/1.3.3 bring-up
  reordering and buffer trimming, and an attempted PSRAM offload of the NimBLE
  host heap, could not fix it — they tuned the wrong pool.)
- **Fix: dual-radio views now time-multiplex the radios** instead of running
  them at once. wardrive, Cameras, and Advanced Watch alternate Wi-Fi and BLE
  windows (~8 s / ~6 s) through a shared `RadioScheduler`; only one radio is
  DMA-resident per window, each brought up into a clean heap — the same
  single-radio condition that already works reliably. GPS logs continuously
  throughout (UART, unaffected), and both Wi-Fi and BLE totals accumulate
  across windows. Classic ESP32 boards stay a permanent Wi-Fi-only session.
- Supporting cleanup: NimBLE is compiled Observer-only
  (`CONFIG_BT_NIMBLE_ROLE_CENTRAL/PERIPHERAL/BROADCASTER_DISABLED`) since the
  sketch never connects or advertises; the BLE controller's 100-entry hardware
  duplicate lists drop to 16 (the app dedups scan hits itself);
  `build.code_debug=1` (Error-level) is set in every build path so the next
  radio failure prints its real error instead of being silently swallowed.

## [1.3.3] - 2026-09-13

### Fixed

- Dual C5 Touch wardrive no longer turns BLE off when NimBLE fails to start.
  Arduino's default STA pool is ~49 KB internal (`dynamic_rx/tx` = 32), which
  left no room for BLE after the display and SD. `wifi_init_wrap.c` is linked
  with `--wrap=esp_wifi_init` so dynamic RX/TX drop to 8, CSI/AMPDU are off,
  and static RX is 3. `--wrap=esp_bt_controller_init` drops the C5 controller
  reservation so NimBLE still fits beside STA. Arduino's SPIRAM cache TX
  count of 4 is left as-is. Both radios come up after the display and before
  SD. A failed BLE bring-up retries, then shows a radio error instead of a
  Wi-Fi-only session.

## [1.3.2] - 2026-09-12

### Added

- **Settings** under Status: compact rows that fit the button (Sleep Off / 15s /
  30s / 1m / 2m / 5m, Bright 20–100%, GPS baud, boot splash, two-tap confirm
  for active tests, NMEA echo, screen test). Values live in NVS on a blob
  separate from saved networks. Baud changes from the GPS screen or serial `u`
  persist across reboots. Serial `t` opens Settings. Footer **Defaults** restores
  factory prefs without touching saved networks. A dimmed screen wakes on the
  first tap or Mini button without firing that control.
- **Screen test** from Settings: solid colors, SMPTE-style bars, checkerboard,
  corner orientation marks, backlight PWM sweep, and a touch (corners + center,
  raw vs mapped) or Mini button probe. Mini writes the ST7735 directly, then
  blits the same checker through the firmware canvas so panel faults are not
  confused with MiniLayout bugs.

### Fixed

- Arduino IDE Verify failed with a multiple definition of
  `ieee80211_raw_frame_sanity_check` against ESP32 core 3.3.x. CI already
  passed `-Wl,-z,muldefs`; the IDE does not. `scripts/setup_arduino_ide.py`
  writes that flag into the core's `platform.local.txt`.
- Wardrive on Dual C5 Touch started BLE first, then `WiFi.mode(STA)` tried to
  deinit a driver NimBLE already claimed (`wifi_init` 0x3001) and aborted the
  session. Wi-Fi is brought up first; BLE failure now falls back to Wi-Fi-only
  logging instead of a radio-error screen. `shutdownWifi()` no longer double
  deinits after `WiFi.mode(WIFI_OFF)`.
- Touch Wi-Fi init after the SD mount left only a ~34 KB heap block, so
  `esp_wifi_init` failed and IDF logged 0x3001 on cleanup. STA now starts
  right after the display, before SD. Default full brightness uses GPIO
  instead of LEDC so PWM does not split that block.

### Validation

- Dual C5 Touch and Dual C5 Mini compiled and packaged.

## [1.3.1] - 2026-09-12

### Added

- Board and Link Mode notes that the README already pointed at:
  [dual-esp32-touch.md](docs/dual-esp32-touch.md),
  [dual-esp32-mini.md](docs/dual-esp32-mini.md), and
  [link-mode.md](docs/link-mode.md). The website now emits pages for them.
- Host-side `NetworkParse` tests under `tests/host/` (IPv4, subnet range, HTTP
  headers, XML/UPnP blocks, URLs, Content-Length and chunked bodies, Flipper
  UUID hints, malformed input) run with AddressSanitizer and
  UndefinedBehaviorSanitizer. `bash tests/host/run.sh` or the **Host tests**
  workflow.

### Fixed

- Deauth Watch read addr2/addr3 without a 24-byte frame-length check, the same
  class of bug Packet Monitor already guarded against.
- Serial **h** left the Locator running and kept Link Mode HELLO broadcasts
  going after the UI returned Home.
- Evil Portal CSV concatenated raw form fields, so a comma or quote in a
  submitted value broke `portal_creds.csv`. Fields are now a single escaped
  column; Serial logs the client and field count, not the values.
- README said the Arduino IDE sketch default was Mini; the code selects
  Dual C5 Touch. Original Mini button prose also swapped Left (GPIO13,
  `INPUT_PULLUP`) with Center (GPIO34, input-only).
- `scripts/build_firmware.py` omitted `-Wl,-z,muldefs`, so a local package
  could lose the raw-frame sanity-check override that CI always links.
- `scripts/gen_mini_boot.py` opened the source PNG relative to the working
  directory; it now resolves paths from the script location.

### Changed

- `LinkPacket` is `static_assert`ed at 36 bytes so a C5 / classic ESP32 ABI
  mismatch cannot silently drop every pairing frame.
- Channel-count constants use `sizeof array / sizeof element`.
- Packaging metadata marks original ESP32 profiles experimental and records
  C5 Touch/Mini as the hardware-tested maps.
- `.gitignore` covers `.DS_Store` and website build output.

### Validation

- Dual C5 Touch and Dual C5 Mini compiled and packaged. Host parser tests
  passed under AddressSanitizer and UndefinedBehaviorSanitizer. Website tests
  passed.

## [1.3.0] - 2026-09-11

### Added

- **Network Tools** under Recon: RAM-only Wi-Fi connection setup with a masked
  Touch/Mini character picker, subnet-aware ARP host discovery, common TCP port
  checks, RTSP/ONVIF camera-service discovery, printer-port candidates, SIP OPTIONS
  discovery, and a read-only UPnP gateway mapping viewer.
- Bounded, cancellable LAN scans with paginated host/service details, automatic
  CSV exports on completion/cancellation, manual snapshots, separate host/service
  summaries, and explicit scope/result limits and inconclusive timeout counts.
  Serial **h** also saves partial scan results before disconnecting and releasing
  the Network Tools resources.
- Flipper-like advertised-service hints in BLE results, details, and CSV exports.
  These hints do not authenticate device identity.
- README and third-party acknowledgement of **7h30th3r0n3 / Evil-M5Project**,
  whose scanning features informed these additions.
- Host-side subnet/URL/HTTP/XML/BLE parser tests, including malformed-input checks
  under AddressSanitizer and UndefinedBehaviorSanitizer, plus socket-state tests
  for scan progression, timeouts, oversized responses, and cleanup on exit.

- Original **Dual ESP32 Mini v1/v2/v3** screen-side profiles: shared native Mini
  UI, ST7735 and five-button wiring, SD/GPS pins and input-only GPIO handling.
  Original Mini GPS defaults to 9600 baud; Touch defaults to 115200.
  C5 and original Mini display selection now share `AWOK_MINI_DISPLAY` while
  retaining their different radio and memory policies.

- Experimental original **Dual ESP32 Touch v1/v2/v3** white-port build profiles,
  with revision-specific SD pins, ILI9341/XPT2046 and GPS wiring, and guards
  against selecting the wrong chip or conflicting profiles.
- Classic ESP32 uses 2.4 GHz channels, 32-entry result tables and 128 Wardrive
  deduplication addresses. BLE-only tools remain available; combined views use
  Wi-Fi only. C5 profiles retain their existing capacities and radio policies.
- **Link Mode now works across board families.** A C5 (dual-band) and a classic
  2.4 GHz unit pair and run Split Wardrive together: HELLO advertises each unit's
  band capability, and the pair splits by band — the C5 takes all of 5 GHz and
  the 2.4-only unit takes all of 2.4 GHz, for full coverage with no overlap. Two
  C5s still alternate-deal the full dual-band plan; two classics split 2.4 GHz.
- **Credits & license** section in the README crediting ESP32 Marauder
  (justcallmekoko), FZEasyMarauderFlash (SkeletonMan03), and the upstream
  libraries, and noting that AWOKxDAG's own code is MIT while referenced projects
  keep their own licenses (Marauder is GPL-3.0).

### Fixed

- Link Mode could not pair a C5 with a 2.4 GHz board: the ESP-NOW protocol
  version was band-dependent (C5 sent 1, classic sent 2) and the receive path
  drops version mismatches, so every cross-board frame was rejected. The version
  is now uniform across boards, and the Split Wardrive channel plan is shared and
  band-aware instead of each unit dealing from its own (differently sized) list.
  Reflash both units — an older 1.2.0 C5 (version 1) will not pair with a 1.3.0
  unit.

### Validation

- Builds passed for C5 Mini, C5 Touch, original ESP32 Touch v1, and original
  ESP32 Mini v3. Parser, socket-state, and existing result-memory tests passed.
  The new features have not yet been flashed or validated on hardware.

## [1.2.0] - 2026-09-10

### Added

- **Link Mode** — pairs two AWOKxDAG units (any mix of Touch and Mini) over an
  ESP-NOW back-channel, using a display-and-confirm 4-digit code derived from the
  two MAC addresses so neither board has to type anything. The lower MAC becomes
  master and owns the shared session id and clock. Discovery is plaintext
  broadcast (ESP-NOW cannot encrypt broadcast); once paired, telemetry moves to an
  encrypted unicast peer (PMK + per-pair LMK baked into the firmware).
- **Split Wardrive** over a link: the paired units alternate-deal the dual-band
  channel list (master even indices, slave odd), so each scans half the spectrum
  and halves its per-channel revisit interval. A one-second, time-synced
  rendezvous on channel 1 exchanges telemetry; each screen shows its own, the
  partner's, and the combined AP count, the partner's link RSSI, and a
  partner-lost alert. Each board logs its own WiGLE `wardrive.csv` and geotags
  from its own GPS. Starting it unpaired wardrives every channel solo. Reached
  from the GPS screen (`Home | Baud | Drive | Link`) or serial `n`. Wi-Fi-only in
  this release.
- **Mini boot logo** — the Dual C5 Mini now shows the AWOK logo at startup
  instead of a text placeholder. The 240×320 Touch splash is downscaled to a
  96×128 1-bit image (`scripts/gen_mini_boot.py`) and blitted straight to the
  ST7735 through a new `splash()` path, since the Mini's retained-mode renderer
  cannot draw arbitrary bitmaps through its normal layout document.
- **Firmware website** — responsive documentation generated from `README.md`,
  `CHANGELOG.md`, and Markdown files in `docs/`, with section navigation, a latest
  release summary, and links to [ESPTerminator](https://espterminator.com/) for
  web flashing. Includes dag's logo in the header and footer, browser
  favicons, and a mobile home-screen icon.
- **Website publishing workflow** — prepared GitHub Pages automation to rebuild
  and publish when documentation changes are pushed to `main`.

### Changed

- The WiGLE CSV `release=` metadata now derives from `kVersion` instead of a
  hardcoded string, so wardrive logs track the firmware version automatically.
- The 240×320 Touch boot bitmap is compiled only into Touch builds now (~9.6 KB
  of flash reclaimed on the Mini, which carries its own 96×128 splash instead).
- Bumped firmware, WiGLE metadata, documentation, and release-workflow defaults
  to 1.2.0 for both Mini and Touch builds.

### Validation

- Link Mode confirmed on hardware: two units pair via the display-and-confirm
  4-digit code and run the split-channel wardrive with encrypted telemetry,
  logging to WiGLE once each unit has a GPS fix (the AP count is fix-gated, as
  with solo Wardrive). A confirm-handshake bug found on-device — the first unit
  to pair went silent and starved the second, hanging it on the confirm screen —
  was fixed by having a paired unit answer the peer's lingering HELLOs so both
  latch.
- The Mini boot logo and split wardrive were exercised on-device; the Touch
  profile shares the same code paths but was not re-flashed this round.

## [1.1.4] - 2026-09-09

### Fixed

- Mini dual-radio views can now attempt Wi-Fi + BLE with the stock core by
  placing five application result tables (47 KiB) in PSRAM, retaining their
  128-entry capacities. Driver queues and DMA buffers stay internal; Touch
  retains its static tables.
- Replaced the Mini's permanent BLE disable with per-session heap admission,
  BLE-first startup, and Wi-Fi-only fallback on insufficient memory or reported
  radio/scan startup failure. Logs report actual table placement and heap use.
  Allocation failure is checked before tools can run. Hardware validation of
  coexistence and repeated tool switching is still pending.

### Changed

- Bumped firmware, WiGLE metadata, documentation, and release-workflow defaults
  to 1.1.4 for both Mini and Touch builds.

### Validation

- Both board profiles compile. Sanitized host checks cover result-table object
  lifetime, allocation fallback, memory admission, and radio-startup recovery.
  On-device testing of Mini coexistence and repeated tool switching is pending.

## [1.1.3] - 2026-09-09

### Added

- Dual-board release workflow: the Actions build now produces both Dual C5
  Touch (`awokxdag-touch-*`) and Dual C5 Mini (`awokxdag-mini-*`) firmware
  images from the same source in one release, with combined SHA-256 checksums.
- Mini dual-radio views degrade to Wi-Fi-only instead of failing: Wardrive
  logs Wi-Fi APs to WiGLE with GPS, Cameras flags camera-like Wi-Fi devices,
  and Advanced Watch monitors deauth/EAPOL/CSA/RF. Each notes on screen that
  BLE is off because the Mini has room for only one radio at a time.

### Changed

- Reworked the radio lifecycle to a reliable one-radio-at-a-time model on the
  Mini, where Wi-Fi (~49 KB) and BLE (~33 KB) cannot coexist in ~75 KB of free
  internal RAM: each radio is fully torn down before the other starts. Wi-Fi
  shutdown now fully deinitializes the driver (`esp_wifi_stop` +
  `esp_wifi_deinit`) and BLE frees its controller between uses. Wi-Fi-only and
  BLE-only tools switch cleanly; the Touch board still runs both radios.
- Bumped firmware, WiGLE metadata, and release-workflow defaults to 1.1.3.

### Fixed

- Fixed a Guru Meditation (load access fault) when tearing down a BLE scan. The
  release path called `NimBLEDevice::deinit(false)` then `deinit(true)`, and
  deleting the scan object ran its scan-response-timer callout deinit after the
  NimBLE port had already been freed. BLE now stops the scan, drains pending
  callbacks, and calls a single `NimBLEDevice::deinit()`.
- Fixed Wi-Fi failing to reinitialize (`ESP_ERR_WIFI_NOT_INIT`, 0x3001) after a
  radio switch; `WiFi.mode(WIFI_OFF)` alone left the driver half-initialized.
- Fixed "ble ll env init error code:-13" (out of memory) when opening a
  dual-radio view on the Mini after Wi-Fi was already up.

## [1.1.2] - 2026-09-09

### Added

- Experimental Dual C5 Mini build profile with a native 128 × 128 ST7735
  interface: highlighted joystick selection, wrapped details, compact charts,
  and quick access to screen actions. Physical validation is pending.
- Board-specific firmware packaging with SHA-256 checksums, plus a sanitized
  host regression test for Mini layout bounds, wrapping, selection, live
  redraws, and content capacity.
- BLE result paging with Prev/Next controls and detail inspection for every
  retained advertiser.
- Six host regression groups covering saved-network persistence, BLE scan
  transitions and paging, touchscreen contacts, GPS freshness, and packet
  monitor buffer handling, with address and undefined-behavior sanitizers.

### Changed

- Packaging and the Touch release workflow now select their board profile
  explicitly, independent of the sketch's Arduino IDE Mini selection.
- Increased BLE results from 24 to 128 and Wi-Fi results from 72 to 128.
- Increased Clients, Security Audit, BLE Trackers, Cameras, WPS, Hidden SSID,
  Harvester, Probe Intel, and Karma Watch tables from 24 to 128 entries, and
  Harvester beacon deduplication from 64 to 128 BSSIDs.
- Saved networks now use a checked, versioned NVS snapshot. Existing per-field
  records remain readable and migrate on the next successful save.
- Bumped firmware, WiGLE metadata, documentation, and release-workflow defaults
  to version 1.1.2.

### Fixed

- Reduced Mini display RAM from a 32 KB RGB565 canvas to an 8 KB indexed-color
  canvas, compacted layout records, and deferred BLE initialization until a BLE
  tool starts. BLE shutdown releases its allocations for subsequent Wi-Fi use.
- Wi-Fi initialization and scan errors now cancel the scan, preserve previous
  results, and report available internal RAM, largest free block, and PSRAM.
- An absent saved-network namespace now reports an empty first-run list;
  actual NVS failures include the Espressif error name and code.
- Packet Monitor no longer reads frame payloads from metadata-only Wi-Fi
  notifications, preventing an out-of-bounds read.
- Held touchscreen contacts no longer trigger repeated actions or bypass the
  file manager's two-tap delete confirmation.
- Failed saved-network writes no longer report success or erase the previous
  list before writing its replacement; failed edits restore the in-memory list.
- Continuous BLE scans now receive repeat advertisements without inheriting a
  previous scan's result limit. Regular scans reset the previous monitor's
  callback and restore bounded result retention.
- GPS timestamps now fall back to uptime when date or time updates are stale;
  the timestamp buffer also accommodates the full formatted value.

## [1.1.1] - 2026-08-24

### Added

- Rotating `firmware_audit.csv` operational trail with per-boot session IDs,
  firmware version, event outcomes, optional GPS context, and one retained
  256 KB previous segment.
- Audit events for active-test sessions, security-audit runs, saved-network
  changes, file deletion, portal-log clearing, and SD recovery.
- Paged Wi-Fi results with Home, Prev, Next, Deauth, and Scan controls for up to
  72 stored access points.
- Advanced Watch, a shared passive Wi-Fi/BLE monitoring pipeline for:
  - Beacon, RSN, PMF, WPS, channel, and beacon-interval integrity changes.
  - Saved-network security downgrades.
  - Grouped deauthentication/disassociation storms and reason codes.
  - Channel-switch announcement abuse.
  - EAPOL and authentication/association storms.
  - RF noise-floor anomalies against a learned baseline.
  - Rapid BLE random-address churn with a stable payload structure.
- GPS-tagged `advanced_watch.csv` alerts and matching firmware-audit events.

### Changed

- Bumped firmware, WiGLE metadata, documentation, and release-workflow defaults
  to version 1.1.1.
- Handshake captures now use readable `<ESSID>_<BSSID>.pcap` filenames, falling
  back to `<BSSID>.pcap` for hidden networks, instead of overwriting one
  `latest_handshake.pcap` across every target.
- Advanced Watch tables now evict stale entries in crowded environments instead
  of becoming permanently saturated.

### Privacy

- The firmware audit trail intentionally excludes captured credentials and
  packet payloads.

## [1.1.0] - 2026-08-22

### Added

- Passive Security Audit with encryption, PMF, WPS, and risk reporting.
- BLE tracker monitoring for AirTag/Find My, Tile, and Samsung SmartTag devices.
- Passive handshake and PMKID Harvester.
- Probe Intel directed-probe aggregation.
- Karma Watch, Beacon Watch, and authentication/association flood detection.
- CSV/pcap exports and menu integration for the new recon and monitor tools.

## [1.0.0] - 2026-08-19

### Added

- Initial ESP32-C5 firmware release.
- Wi-Fi and BLE reconnaissance, client and packet monitoring, WPS and hidden
  SSID tools, camera detection, GPS wardriving, and saved networks.
- Deauthentication testing, handshake/PMKID capture, beacon flooding, captive
  portal/evil-twin testing, and Probe Lure.
- Deauth, rogue-AP, and BLE-spam defensive monitors.
- Touchscreen UI, SD capture manager, status screens, serial controls, build
  workflow, and recovery documentation.

[Unreleased]: https://github.com/dagnazty/awokxdag/compare/v1.3.5...HEAD
[1.3.5]: https://github.com/dagnazty/awokxdag/compare/v1.3.4...v1.3.5
[1.3.4]: https://github.com/dagnazty/awokxdag/compare/v1.3.3...v1.3.4
[1.3.3]: https://github.com/dagnazty/awokxdag/compare/v1.3.2...v1.3.3
[1.3.2]: https://github.com/dagnazty/awokxdag/compare/v1.3.1...v1.3.2
[1.3.1]: https://github.com/dagnazty/awokxdag/compare/v1.3.0...v1.3.1
[1.3.0]: https://github.com/dagnazty/awokxdag/compare/v1.2.0...v1.3.0
[1.2.0]: https://github.com/dagnazty/awokxdag/compare/v1.1.4...v1.2.0
[1.1.4]: https://github.com/dagnazty/awokxdag/compare/v1.1.3...v1.1.4
[1.1.3]: https://github.com/dagnazty/awokxdag/compare/v1.1.2...v1.1.3
[1.1.2]: https://github.com/dagnazty/awokxdag/compare/v1.1.1...v1.1.2
[1.1.1]: https://github.com/dagnazty/awokxdag/compare/v1.1.0...v1.1.1
[1.1.0]: https://github.com/dagnazty/awokxdag/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/dagnazty/awokxdag/releases/tag/v1.0.0
