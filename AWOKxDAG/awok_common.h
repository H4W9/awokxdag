// Shared declarations for the AWOKxDAG firmware. Included once by the main
// sketch; the feature tabs (deauth/handshake/sniffer/beacon/portal/gps/input)
// are concatenated into the same translation unit, so all globals defined in
// the main sketch and the tabs are visible everywhere without extern churn.
#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SD.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <SPI.h>
#include <WiFi.h>
#include <XPT2046_Touchscreen.h>
#include <TinyGPSPlus.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <nvs.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <string>
#include "network_parse.h"
#include <WiFiUdp.h>
#include <lwip/sockets.h>
#include <lwip/etharp.h>
#include <lwip/priv/tcpip_priv.h>
#include <esp_netif.h>
#include <esp_netif_net_stack.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#include "board_pins.h"
#ifdef AWOK_MINI_DISPLAY
#include "mini_display.h"
#include "mini_boot_screen_data.h"
#include "result_memory.h"
// True when BLE participates in a dual-radio session (it time-shares the radio
// with Wi-Fi via RadioScheduler; it is never resident at the same time).
// False keeps a view Wi-Fi-only. Set by radioSchedulerBegin.
bool radiosCoexist = false;
#else
#ifdef AWOK_CLASSIC_ESP32
bool radiosCoexist = false;
#else
bool radiosCoexist = true;
#endif
#include "touch_display.h"     // buffered ILI9341 wrapper (kills refresh flicker)
#include "boot_screen_data.h"  // 240x320 Touch splash; unused on the Mini
#endif

constexpr int kScreenWidth = 240;
constexpr int kScreenHeight = 320;
constexpr int kHeaderHeight = 42;
constexpr int kFooterTop = 278;
// Classic ESP32 has a much smaller statically addressable DRAM segment and no
// verified PSRAM on these display pins. Keep bounded tables within its budget.
constexpr int kResultCapacity = AwokPins::kDualBand ? 128 : 32;
constexpr int kMaxWifiResults = kResultCapacity;
constexpr int kMaxBleResults = kResultCapacity;
// NimBLE reserves 255 for unlimited retention; keep snapshot scans bounded.
static_assert(kMaxBleResults > 0 && kMaxBleResults < 255, "Invalid BLE result limit");
constexpr int kVisibleRows = 10;
constexpr int kMaxSaved = 10;
constexpr uint32_t kBleScanMs = 5000;
constexpr uint32_t kBootScreenMs = 1800;
constexpr uint32_t kSdClockHz = 10000000;
constexpr char kSdDirectory[] = "/awokxdag";
constexpr char kFirmwareAuditCsvPath[] = "/awokxdag/firmware_audit.csv";
constexpr char kFirmwareAuditPreviousCsvPath[] =
    "/awokxdag/firmware_audit.previous.csv";
constexpr uint32_t kFirmwareAuditMaxBytes = 256 * 1024;
constexpr char kSavedCsvPath[] = "/awokxdag/saved_networks.csv";
constexpr char kScanCsvPath[] = "/awokxdag/latest_wifi_scan.csv";
constexpr char kBleScanCsvPath[] = "/awokxdag/latest_ble_scan.csv";
constexpr char kSignalCsvPath[] = "/awokxdag/latest_wifi_signal.csv";
constexpr int kSignalSampleCount = 30;
constexpr uint32_t kSignalSampleIntervalMs = 1500;
constexpr uint32_t kSignalPassiveDwellMs = 350;
constexpr char kDeauthLogCsvPath[] = "/awokxdag/latest_deauth_log.csv";
constexpr uint32_t kDeauthHopIntervalMs = 300;
constexpr uint32_t kDeauthMonitorRedrawMs = 500;
constexpr uint32_t kDeauthAttackBurstIntervalMs = 20;
constexpr uint32_t kDeauthAttackRedrawMs = 500;
// Detection hops every 2.4 GHz channel (1-13) and every standard 5 GHz 20 MHz
// channel, including the DFS block (52-64, 100-144) and 165 -- listening on DFS
// is allowed, only transmitting is restricted. On the C5, esp_wifi_set_channel
// switches band automatically from the channel number; if the region disallows
// a channel the call fails and the hop simply moves on.
constexpr uint8_t kDeauthHopChannels[] = {
    1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  13,
#ifndef AWOK_CLASSIC_ESP32
    36,  40,  44,  48,  52,  56,  60,  64,  100, 104, 108, 112, 116,
    120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165
#endif
};
constexpr int kDeauthHopChannelCount =
    static_cast<int>(sizeof(kDeauthHopChannels) / sizeof(kDeauthHopChannels[0]));
constexpr int kMaxDeauthTargets = 8;
constexpr char kVersion[] = "1.3.5";
constexpr char kAuthor[] = "dag nazty";
constexpr uint32_t kHandshakeRedrawMs = 500;
constexpr uint32_t kHandshakePulseMs = 2000;
constexpr int kCaptureSlotBytes = 256;
constexpr int kCaptureQueueSlots = 24;
constexpr char kClientCsvPath[] = "/awokxdag/latest_clients.csv";
constexpr int kMaxClients = kResultCapacity;
constexpr int kSnifferQueueSlots = 32;
constexpr uint32_t kClientHopIntervalMs = 300;
constexpr uint32_t kClientRedrawMs = 700;
constexpr uint32_t kBeaconBurstIntervalMs = 20;
constexpr uint32_t kBeaconRedrawMs = 500;
constexpr int kBeaconsPerBurst = 5;
constexpr uint8_t kBeaconChannels[] = {1, 6, 11};
constexpr int kBeaconChannelCount =
    static_cast<int>(sizeof(kBeaconChannels) / sizeof(kBeaconChannels[0]));
constexpr char kPortalSsid[] = "Free_WiFi";
constexpr char kPortalCredsPath[] = "/awokxdag/portal_creds.csv";
constexpr uint32_t kPortalRedrawMs = 1000;
constexpr char kWardriveCsvPath[] = "/awokxdag/wardrive.csv";
constexpr int kMaxWardriveMacs = AwokPins::kDualBand ? 512 : 128;
constexpr uint32_t kWardriveRedrawMs = 800;
constexpr int kBleHitQueueSlots = 24;

// Security Audit: passive beacon-IE posture report (encryption tier, PMF, WPS).
constexpr char kSecurityAuditCsvPath[] = "/awokxdag/security_audit.csv";
constexpr int kMaxAudit = kResultCapacity;
constexpr int kAuditHitQueueSlots = 24;
constexpr uint32_t kAuditHopIntervalMs = 300;
constexpr uint32_t kAuditRedrawMs = 700;

// BLE Trackers: passive AirTag/Find My, Tile, Samsung SmartTag detection.
constexpr char kTrackerCsvPath[] = "/awokxdag/ble_trackers.csv";
constexpr int kMaxTrackers = kResultCapacity;
constexpr int kTrackerHitQueueSlots = 32;
constexpr uint32_t kTrackerRedrawMs = 700;
// A tracker seen over a span longer than this (with repeat sightings) while you
// move is flagged as potentially following you.
constexpr uint32_t kTrackerFollowMs = 45000;
constexpr uint32_t kTrackerMinSightings = 4;

// Harvester: all-channel passive EAPOL/PMKID collection (no deauth).
constexpr char kHarvestPcapPath[] = "/awokxdag/harvest.pcap";
constexpr char kHarvestPmkidPath[] = "/awokxdag/harvest_pmkid.txt";
constexpr int kMaxHarvestAp = kResultCapacity;
constexpr int kMaxHarvestSeen = kResultCapacity;  // beacons written once per BSSID
constexpr uint32_t kHarvestHopIntervalMs = 300;
constexpr uint32_t kHarvestRedrawMs = 700;

// Probe Intel: directed probe-request SSID aggregation.
constexpr char kProbeIntelCsvPath[] = "/awokxdag/probe_intel.csv";
constexpr int kMaxProbeSsids = kResultCapacity;
constexpr int kProbeMacsPerSsid = 8;
constexpr int kProbeHitQueueSlots = 32;
constexpr uint32_t kProbeHopIntervalMs = 300;
constexpr uint32_t kProbeRedrawMs = 700;

// Karma Watch: one BSSID answering many SSIDs (WiFi Pineapple / Karma / MANA).
constexpr char kKarmaLogCsvPath[] = "/awokxdag/karma_log.csv";
constexpr int kMaxKarmaAps = kResultCapacity;
constexpr int kKarmaSsidsPerAp = 6;
constexpr int kKarmaHitQueueSlots = 24;
constexpr int kKarmaSsidThreshold = 3;  // distinct SSIDs => suspicious
constexpr uint32_t kKarmaHopIntervalMs = 300;
constexpr uint32_t kKarmaRedrawMs = 700;

// Beacon Watch: beacon-flood / fake-AP detection by BSSID diversity per window.
constexpr char kBeaconWatchLogCsvPath[] = "/awokxdag/beacon_flood_log.csv";
constexpr int kBeaconWatchWindowSet = 64;   // distinct BSSIDs counted per window
constexpr int kBeaconWatchHitQueueSlots = 32;
constexpr uint32_t kBeaconWatchWindowMs = 2000;
constexpr uint32_t kBeaconWatchRedrawMs = 500;
constexpr uint32_t kBeaconWatchHopIntervalMs = 250;
constexpr uint32_t kBeaconFloodThreshold = 25;  // distinct BSSIDs / window

// Auth Flood Watch: authentication / association flood DoS against an AP.
constexpr char kAuthFloodLogCsvPath[] = "/awokxdag/auth_flood_log.csv";
constexpr uint32_t kAuthFloodWindowMs = 2000;
constexpr uint32_t kAuthFloodRedrawMs = 500;
constexpr uint32_t kAuthFloodHopIntervalMs = 250;
constexpr uint32_t kAuthFloodThreshold = 30;  // auth/assoc frames / window

// Advanced Watch: shared passive Wi-Fi/BLE anomaly detector.
constexpr char kAdvancedWatchLogCsvPath[] = "/awokxdag/advanced_watch.csv";
constexpr int kAdvancedHitQueueSlots = 64;
constexpr int kAdvancedBleQueueSlots = 32;
constexpr int kAdvancedMaxAps = 32;
constexpr int kAdvancedMaxDisconnectGroups = 8;
constexpr int kAdvancedMaxBleFingerprints = 16;
constexpr uint32_t kAdvancedWindowMs = 2000;
constexpr uint32_t kAdvancedRedrawMs = 500;
constexpr uint32_t kAdvancedHopIntervalMs = 300;
constexpr uint32_t kAdvancedDisconnectThreshold = 20;
constexpr uint32_t kAdvancedEapolThreshold = 16;
constexpr uint32_t kAdvancedAssocThreshold = 30;
constexpr uint32_t kAdvancedCsaThreshold = 8;
constexpr int kAdvancedNoiseRiseDb = 15;
constexpr int kAdvancedNoiseFloorMinDbm = -80;
constexpr uint32_t kAdvancedBleChurnWindowMs = 15000;
constexpr int kAdvancedBleChurnAddresses = 5;

enum AdvancedHitKind : uint8_t {
  kAdvancedBeaconHit = 1,
  kAdvancedDisconnectHit = 2,
  kAdvancedEapolHit = 3,
  kAdvancedAssocHit = 4
};

struct AdvancedHit {
  uint8_t kind = 0;
  uint8_t subtype = 0;
  uint8_t source[6] = {0};
  uint8_t target[6] = {0};
  uint8_t channel = 0;
  int8_t rssi = -127;
  int8_t noise = -127;
  uint16_t reason = 0;
  uint16_t beaconInterval = 0;
  uint32_t fingerprint = 0;
  uint8_t security = 0;
  uint8_t pmf = 0;
  uint8_t wps = 0;
  uint8_t csaChannel = 0;
  uint8_t ssidLen = 0;
  char ssid[33] = {0};
};

struct AdvancedApEntry {
  uint8_t bssid[6] = {0};
  String ssid;
  uint8_t channel = 0;
  int8_t rssi = -127;
  uint16_t beaconInterval = 0;
  uint32_t fingerprint = 0;
  uint8_t security = 0;
  uint8_t pmf = 0;
  uint8_t wps = 0;
  uint32_t lastSeenMs = 0;
  bool savedDowngradeLogged = false;
  uint32_t pendingKey = 0;
  uint8_t pendingCount = 0;
  uint8_t pendingChannel = 0;
  uint16_t pendingBeaconInterval = 0;
  uint32_t pendingFingerprint = 0;
  uint8_t pendingSecurity = 0;
  uint8_t pendingPmf = 0;
  uint8_t pendingWps = 0;
  char pendingSsid[33] = {0};
};

struct AdvancedDisconnectGroup {
  uint8_t source[6] = {0};
  uint8_t target[6] = {0};
  uint16_t reason = 0;
  uint32_t count = 0;
};

struct AdvancedBleHit {
  char address[18] = {0};
  uint32_t fingerprint = 0;
  int8_t rssi = -127;
};

struct AdvancedBleFingerprint {
  uint32_t fingerprint = 0;
  char addresses[kAdvancedBleChurnAddresses + 2][18] = {{0}};
  uint8_t addressCount = 0;
  int8_t lastRssi = -127;
  uint32_t firstSeenMs = 0;
  uint32_t lastSeenMs = 0;
  bool alerted = false;
};

constexpr uint16_t kBackground = ILI9341_BLACK;
constexpr uint16_t kPanel = 0x1082;
constexpr uint16_t kAccent = ILI9341_CYAN;
constexpr uint16_t kMuted = 0x7BEF;
constexpr uint16_t kGood = ILI9341_GREEN;
constexpr uint16_t kWarn = ILI9341_YELLOW;
constexpr uint16_t kBad = ILI9341_RED;

// Persistent operational audit trail. Details are CSV-escaped by the writer;
// callers must not put captured passwords or other secrets in this log.
bool initializeFirmwareAudit();
bool recordFirmwareAudit(const char* category, const char* action,
                         const char* outcome, const String& details);

enum class View {
  kHome,
  kWifi,
  kChannels,
  kBle,
  kBleDetail,
  kSaved,
  kWifiAudit,
  kWifiMonitor,
  kDeauthMonitor,
  kDeauthSelect,
  kDeauthAttack,
  kHandshake,
  kClientSniffer,
  kRecon,
  kMonitor,
  kAttacks,
  kBeaconFlood,
  kEvilPortal,
  kGps,
  kWardrive,
  kPacketMon,
  kLocator,
  kWpsScan,
  kStatus,
  kSettings,
  kScreenTest,
  kFiles,
  kBleSpamWatch,
  kProbeLure,
  kRogueWatch,
  kHiddenReveal,
  kCameraScan,
  kSecurityAudit,
  kTrackerScan,
  kHarvester,
  kProbeIntel,
  kKarmaWatch,
  kBeaconWatch,
  kAuthFlood,
  kAdvancedWatch,
  kLinkWardrive,
  kNetworkMenu,
  kNetworkSetup,
  kNetworkEdit,
  kNetworkAps,
  kNetworkResults,
  kNetworkHost,
  kNetworkDetail
};

// ---- Link Mode (ESP-NOW pairing of two AWOKxDAG units) ------------------
// See docs/link-mode.md. v1 = shared core + Split Wardrive. All ESP-NOW frames
// are the single POD LinkPacket below, tagged by `type`. The recv callback runs
// in the Wi-Fi task and only enqueues into linkPacketQueue; updateLink() parses.
constexpr uint32_t kLinkMagic = 0x41574B4C;  // "AWKL"
// Distinct channel plans cannot alternate-deal the same spectrum. Keep classic
// pairs isolated from existing C5 protocol-v1 peers until negotiation exists.
// One value across ALL boards — the wire format is identical, so a band-specific
// version only made the C5 and 2.4 GHz boards reject each other's frames. Band
// capability is negotiated in the HELLO flags instead (kLinkFlagDualBand).
constexpr uint8_t kLinkProtoVersion = 2;
constexpr uint8_t kLinkChannel = 1;          // rendezvous + pairing channel
constexpr uint32_t kLinkRendezvousMs = 1000;  // beat period (live feel)
constexpr uint32_t kLinkWindowMs = 300;       // link-channel dwell per beat
constexpr uint32_t kLinkHelloIntervalMs = 250;
constexpr uint32_t kLinkPeerTimeoutMs = 4000;  // partner-lost threshold
constexpr uint32_t kLinkWardriveDwellMs = 200;  // per-channel async scan dwell
constexpr uint32_t kLinkWardriveRedrawMs = 700;
constexpr int kLinkPacketQueueSlots = 16;
constexpr uint8_t kLinkBroadcastAddr[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ESP-NOW encryption keys for the post-pairing unicast link. Broadcast discovery
// (HELLO) stays in the clear because ESP-NOW cannot encrypt broadcast; once
// paired, SYNC/TELEM move to an encrypted unicast peer. NOTE: these keys ship in
// the firmware binary, so encryption stops casual sniffing but is NOT secret from
// anyone who has the build. The per-pair LMK also mixes in the confirm code (from
// both MACs) for key separation between pairs, not as an added secret.
constexpr uint8_t kLinkPmk[16] = {0x9E, 0x1C, 0x74, 0xB3, 0x2A, 0xF5, 0x60, 0xD8,
                                  0x4B, 0x07, 0xC9, 0x3E, 0xA1, 0x52, 0x8F, 0x66};
constexpr uint8_t kLinkLmkBase[16] = {0x51, 0xE4, 0x0B, 0x9A, 0x7D, 0x38, 0xC2,
                                      0x6F, 0x14, 0xBE, 0x05, 0xA7, 0x3C, 0xD0,
                                      0x89, 0x22};

enum LinkMsgType : uint8_t {
  kLinkMsgHello = 1,  // identity + confirm code + confirmed flag
  kLinkMsgSync = 2,   // master millis for clock sync
  kLinkMsgTelem = 3   // running counts + channel + session id
};

enum LinkState : uint8_t {
  kLinkOff = 0,         // ESP-NOW down / not on the Link screen
  kLinkDiscovering = 1,  // broadcasting HELLO, waiting to hear a peer
  kLinkAwaitConfirm = 2,  // peer found, 4-digit code shown, waiting for Confirm
  kLinkReady = 3         // paired; role + session settled
};

// This unit's Split Wardrive channel set (declared here so functions taking it
// get valid auto-prototypes). See link.ino linkAssignedPlan.
enum LinkPlan : uint8_t {
  kLinkPlan24 = 0,   // 2.4 GHz only
  kLinkPlan5 = 1,    // 5 GHz only
  kLinkPlanFull = 2  // 2.4 GHz then 5 GHz
};

// One ESP-NOW frame. POD, 36 bytes on every supported ABI, copied verbatim.
struct LinkPacket {
  uint32_t magic = kLinkMagic;
  uint8_t version = kLinkProtoVersion;
  uint8_t type = 0;      // LinkMsgType
  uint8_t flags = 0;     // bit0 = confirmed
  uint8_t role = 0;      // sender's computed role: 0 master, 1 slave
  uint16_t code = 0;     // 4-digit confirm code
  uint16_t reserved = 0;
  uint32_t sessionId = 0;
  uint32_t masterMillis = 0;  // SYNC: master clock
  uint32_t networks = 0;      // TELEM: sender Wi-Fi count
  uint32_t bleCount = 0;      // TELEM: sender BLE count
  uint8_t channel = 0;        // TELEM: sender's current assigned channel
  uint8_t srcMac[6] = {0};    // sender MAC (also in recv info; handy in queue)
};
// 35 payload bytes plus 1 tail pad on both Xtensa ESP32 and RISC-V C5. Do not
// pack this: changing the on-air size would break pairing with 1.3.0 units.
static_assert(sizeof(LinkPacket) == 36,
              "LinkPacket must stay 36 bytes on every board ABI");

constexpr uint8_t kLinkFlagConfirmed = 0x01;
constexpr uint8_t kLinkFlagDualBand = 0x02;  // sender's radio covers 5 GHz too

// Canonical Split Wardrive channel plan — identical on every board so a mixed
// dual-band + 2.4-only pair deals from the same list. The 5 GHz block is only
// used when BOTH units advertise dual-band (see link.ino linkPlanDualBand);
// defining it on a 2.4-only build is harmless (it is simply never indexed).
constexpr uint8_t kLink24Channels[] = {1, 2, 3,  4,  5,  6, 7,
                                       8, 9, 10, 11, 12, 13};
constexpr int kLink24ChannelCount =
    static_cast<int>(sizeof(kLink24Channels) / sizeof(kLink24Channels[0]));
constexpr uint8_t kLink5Channels[] = {
    36,  40,  44,  48,  52,  56,  60,  64,  100, 104, 108, 112, 116,
    120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165};
constexpr int kLink5ChannelCount =
    static_cast<int>(sizeof(kLink5Channels) / sizeof(kLink5Channels[0]));

// A received frame plus the RSSI the radio reported, queued for the main loop.
struct LinkQueueItem {
  LinkPacket pkt;
  int8_t rssi = -127;
};

// A suspected surveillance camera found by the camera scan.
struct CameraEntry {
  String label;   // SSID or BLE name
  String mac;     // BSSID or BLE address
  String vendor;  // matched vendor
  String reason;  // why it flagged (OUI / SSID / BLE)
  int32_t rssi = -127;
  int16_t channel = 0;  // 0 for BLE
  bool ble = false;
};

// POD observation handed from the Wi-Fi task to the camera-scan loop.
struct CameraHit {
  uint8_t mac[6];
  int8_t rssi;
  uint8_t channel;
  uint8_t nameLen;
  char name[33];
};

// One WPS-capable AP found by the WPS scan (POD handed from the Wi-Fi task to
// the main loop, which merges it into the table).
struct WpsHit {
  uint8_t bssid[6];
  uint8_t channel;
  int8_t rssi;
  bool locked;
  uint8_t ssidLen;
  char ssid[33];
};

struct WpsEntry {
  String ssid;
  uint8_t bssid[6] = {0};
  uint8_t channel = 0;
  int32_t rssi = -127;
  bool locked = false;
};

// Rogue-AP / evil-twin detection.
struct RogueHit {
  uint8_t bssid[6];
  uint8_t channel;
  int8_t rssi;
  uint8_t ssidLen;
  char ssid[33];
};

struct ApSighting {
  String ssid;
  uint8_t bssid[6] = {0};
  uint8_t channel = 0;
  int32_t rssi = -127;
  bool suspicious = false;
  bool savedTwin = false;  // matches a saved SSID on a different BSSID
  bool logged = false;
};

// Hidden-SSID reveal.
struct HiddenHit {
  uint8_t bssid[6];
  uint8_t channel;
  int8_t rssi;
  uint8_t kind;  // 0 = hidden beacon, 1 = SSID reveal
  uint8_t ssidLen;
  char ssid[33];
};

struct HiddenEntry {
  uint8_t bssid[6] = {0};
  uint8_t channel = 0;
  int32_t rssi = -127;
  String ssid;
  bool revealed = false;
};

struct WifiEntry {
  String ssid;
  String bssid;
  int32_t rssi = -127;
  int32_t channel = 0;
  wifi_auth_mode_t auth = WIFI_AUTH_OPEN;
};

// Fixed-width NVS format: the whole list is replaced as one blob, so a failed
// update does not erase the previously saved networks. Never persist String.
struct SavedNetworkRecord {
  char ssid[33];
  char bssid[18];
  uint8_t auth;
  int32_t rssi;
  int32_t channel;
};

struct SavedNetworkSnapshot {
  uint32_t version;
  uint32_t count;
  SavedNetworkRecord entries[kMaxSaved];
};
static_assert(sizeof(SavedNetworkRecord) == 60, "NVS record layout changed");
static_assert(sizeof(SavedNetworkSnapshot) == 608, "NVS snapshot layout changed");

// Device preferences (GPS baud, backlight). Separate NVS blob from saved
// networks so a failed settings write cannot clobber the AP list.
struct DeviceSettingsRecord {
  uint32_t version;
  uint32_t gpsBaud;
  uint32_t backlightTimeoutMs;  // 0 = always on
  uint8_t brightnessPercent;    // 20–100
  uint8_t flags;                // kSetting*
  uint8_t reserved[6];
};
static_assert(sizeof(DeviceSettingsRecord) == 20, "NVS settings layout changed");
constexpr uint32_t kDeviceSettingsVersion = 1;
constexpr uint8_t kSettingConfirmAttacks = 0x01;
constexpr uint8_t kSettingSkipSplash = 0x02;
constexpr uint8_t kSettingNmeaEcho = 0x04;

struct DeauthTarget {
  uint8_t bssid[6] = {0};
  uint8_t channel = 0;
  String ssid;
};

// One captured 802.11 frame queued from the Wi-Fi task for the main loop to
// write into the pcap file (single-producer / single-consumer ring buffer).
struct CaptureFrame {
  uint16_t len = 0;      // bytes stored in data (capped at kCaptureSlotBytes)
  uint16_t origLen = 0;  // on-air length before capping
  uint32_t tsSec = 0;
  uint32_t tsUsec = 0;
  bool isEapol = false;
  uint8_t data[kCaptureSlotBytes] = {0};
};

// A discovered client station tracked by the probe-request sniffer.
struct ClientEntry {
  uint8_t mac[6] = {0};
  uint8_t bssid[6] = {0};
  bool hasBssid = false;
  String lastSsid;
  int32_t rssi = -127;
  uint32_t packets = 0;
  uint8_t channel = 0;
};

// A minimal fixed-size observation the sniffer callback hands to the main loop
// (POD only — no String — so it is safe to copy from the Wi-Fi task).
struct SnifferHit {
  uint8_t mac[6];
  uint8_t bssid[6];
  bool hasBssid;
  bool isProbe;
  int8_t rssi;
  uint8_t channel;
  uint8_t ssidLen;
  char ssid[33];
};

// A BLE advertisement handed from the NimBLE task to the wardrive loop.
struct BleHit {
  char addr[18];
  int8_t rssi;
  char name[24];
};

// Time-multiplexed dual-radio scheduling. The C5 cannot keep Wi-Fi and the
// BLE controller DMA-resident at once, so dual-radio views alternate windows;
// each view fills a RadioScheduler and drives it from its update loop.
enum class RadioPhase : uint8_t { kWifi, kBle };

struct RadioScheduler {
  bool active = false;
  bool bleAvailable = false;    // false => permanent Wi-Fi-only session
  RadioPhase phase = RadioPhase::kWifi;
  uint32_t phaseStartMs = 0;
  uint32_t wifiWindowMs = 8000;   // Wi-Fi window length / safety cap
  uint32_t bleWindowMs = 6000;
  // Views that do a discrete Wi-Fi scan (wardrive) set this true when the scan
  // completes so the Wi-Fi window ends as soon as there are results, instead
  // of being cut off mid-scan. Promiscuous views leave it false (timer only).
  bool wifiScanDone = false;
  void (*enterWifi)() = nullptr;  // STA resident; (re)configure Wi-Fi capture
  void (*exitWifi)() = nullptr;   // stop Wi-Fi capture before STA teardown
  NimBLEScanCallbacks* bleCallbacks = nullptr;
  bool bleActiveScan = false;
  uint16_t bleInterval = 160;
  uint16_t bleWindow = 80;
};

// Security Audit encryption tiers (worst -> best), used for scoring/coloring.
enum AuditEnc {
  kAuditOpen = 0,   // no privacy bit
  kAuditWep = 1,    // privacy, no RSN/WPA IE
  kAuditWpa = 2,    // WPA1 vendor IE only (TKIP)
  kAuditWpa2 = 3,   // RSN PSK/CCMP
  kAuditWpa2Tkip = 4,
  kAuditWpa23 = 5,  // RSN PSK+SAE transition
  kAuditWpa3 = 6,   // RSN SAE only
  kAuditOwe = 7,    // Enhanced Open
  kAuditEnterprise = 8
};

// POD beacon observation handed from the Wi-Fi task to the security audit.
struct AuditHit {
  uint8_t bssid[6];
  uint8_t channel;
  int8_t rssi;
  uint8_t enc;  // AuditEnc
  uint8_t pmf;  // 0 none, 1 capable, 2 required
  uint8_t wps;  // 0 none, 1 open, 2 locked
  uint8_t ssidLen;
  char ssid[33];
};

struct AuditEntry {
  uint8_t bssid[6] = {0};
  uint8_t channel = 0;
  int32_t rssi = -127;
  uint8_t enc = kAuditOpen;
  uint8_t pmf = 0;
  uint8_t wps = 0;
  int risk = 0;
  bool logged = false;
  String ssid;
};

// POD BLE tracker sighting handed from the NimBLE task to the tracker scan.
struct TrackerHit {
  char addr[18];
  int8_t rssi;
  uint8_t kind;  // 1 Apple Find My, 2 Tile, 3 Samsung SmartTag
};

struct TrackerEntry {
  String addr;
  int32_t rssi = -127;
  uint8_t kind = 0;
  uint32_t firstSeenMs = 0;
  uint32_t lastSeenMs = 0;
  uint32_t sightings = 0;
  bool following = false;
  bool logged = false;
};

// One AP tracked by the handshake harvester (SSID learned from beacons, EAPOL
// progress and PMKID learned from captured key frames).
struct HarvestAp {
  uint8_t bssid[6] = {0};
  char ssid[33] = {0};
  uint8_t msgMask = 0;  // EAPOL messages 1..4
  bool pmkid = false;
};

// POD probe-request observation handed from the Wi-Fi task to Probe Intel.
struct ProbeHit {
  uint8_t mac[6];
  int8_t rssi;
  uint8_t ssidLen;
  char ssid[33];
};

struct ProbeSsidEntry {
  String ssid;
  uint32_t probes = 0;
  int32_t rssi = -127;
  uint8_t lastMac[6] = {0};
  uint8_t macs[kProbeMacsPerSsid][6] = {{0}};
  int macCount = 0;
  bool macOverflow = false;
};

// One AP tracked by Karma Watch: the distinct SSIDs a single BSSID claims.
struct KarmaEntry {
  uint8_t bssid[6] = {0};
  uint8_t channel = 0;
  int32_t rssi = -127;
  String ssids[kKarmaSsidsPerAp];
  int ssidCount = 0;
  bool overflow = false;
  bool suspicious = false;
  bool logged = false;
};

// Minimal POD beacon observation (BSSID only) for Beacon Watch.
struct BssidHit {
  uint8_t bssid[6];
  uint8_t channel;
  int8_t rssi;
};

struct BleEntry {
  String name;
  String address;
  String serviceUuids;
  String manufacturerDataHex;
  int32_t rssi = -127;
  int32_t txPower = 0;
  int32_t manufacturerId = -1;
  uint16_t advertisementBytes = 0;
  uint8_t addressType = 0;
  bool hasTxPower = false;
  bool connectable = false;
  bool scannable = false;
  bool flipperLike = false;
};

// Declarations so feature tabs can use the short forms. Definitions that take
// default arguments omit those defaults in the .ino body.
void copyMac(uint8_t* dest, const volatile uint8_t* src);
String macToString(const uint8_t* mac);
String macToString(const volatile uint8_t* mac);
String bytesToHex(const std::string& data, size_t maximumBytes = 16);
void drawButton(int x, int y, int w, int h, const String& label,
                uint16_t outline = kAccent);
void drawHeader(const String& title, const String& detail = "");
void logMemory(const char* stage);
void showRadioError(const char* message);
void shutdownWifi();
bool ensureWifiStation(bool releaseBle = true);
bool ensureBleReady(bool needsWifi);
void releaseBleMemory();

void configureBleScan(NimBLEScan* scan, NimBLEScanCallbacks* callbacks,
                      bool active, uint16_t interval, uint16_t window,
                      uint8_t maxResults);
bool radioSchedulerBegin(RadioScheduler& s);
void radioSchedulerTick(RadioScheduler& s);
void radioSchedulerEnd(RadioScheduler& s);

// Network Tools types precede Arduino-generated function prototypes.
enum class NetJob { None, Join, Hosts, Ports, Cameras, Printers, Sip, Upnp };
enum class NetStage { Idle, ArpSend, ArpWait, Probe, Connecting, Sending, Reading, SipWait, SsdpWait };
struct NetHost { uint32_t ip; uint8_t mac[6]; };
struct NetResult { uint32_t ip; uint16_t port; char kind[16]; char detail[160]; };
struct NetArpCall {
  tcpip_api_call_data call;
  uint32_t ip; bool send; bool found; uint8_t mac[6];
};

struct NetSummary {
  NetJob job = NetJob::None;
  char status[96] = {}, ssid[33] = {};
  uint32_t ip = 0, mask = 0, checked = 0, timeouts = 0, refused = 0, errors = 0;
  bool limited = false, subnetLimited = false, hostsLimited = false;
};
