# Changelog

All notable changes to AWOKxDAG are documented here. This project follows
[Semantic Versioning](https://semver.org/).

## [Unreleased]

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

[Unreleased]: https://github.com/dagnazty/awokxdag/compare/v1.1.3...HEAD
[1.1.3]: https://github.com/dagnazty/awokxdag/compare/v1.1.2...v1.1.3
[1.1.2]: https://github.com/dagnazty/awokxdag/compare/v1.1.1...v1.1.2
[1.1.1]: https://github.com/dagnazty/awokxdag/compare/v1.1.0...v1.1.1
[1.1.0]: https://github.com/dagnazty/awokxdag/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/dagnazty/awokxdag/releases/tag/v1.0.0
