# Changelog

All notable changes to AWOKxDAG are documented here. This project follows
[Semantic Versioning](https://semver.org/).

## [Unreleased]

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

[Unreleased]: https://github.com/dagnazty/awokxdag/compare/v1.3.0...HEAD
[1.3.0]: https://github.com/dagnazty/awokxdag/compare/v1.2.0...v1.3.0
[1.2.0]: https://github.com/dagnazty/awokxdag/compare/v1.1.4...v1.2.0
[1.1.4]: https://github.com/dagnazty/awokxdag/compare/v1.1.3...v1.1.4
[1.1.3]: https://github.com/dagnazty/awokxdag/compare/v1.1.2...v1.1.3
[1.1.2]: https://github.com/dagnazty/awokxdag/compare/v1.1.1...v1.1.2
[1.1.1]: https://github.com/dagnazty/awokxdag/compare/v1.1.0...v1.1.1
[1.1.0]: https://github.com/dagnazty/awokxdag/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/dagnazty/awokxdag/releases/tag/v1.0.0
