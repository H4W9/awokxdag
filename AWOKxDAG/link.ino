// AWOKxDAG — Link Mode: pair two units over ESP-NOW (compiled as part of the
// sketch; see awok_common.h and docs/link-mode.md).
//
// v1 ships the shared core (pairing with a display-and-confirm 4-digit code,
// coarse clock sync, a heartbeat/partner-lost alert) plus Split Wardrive, where
// two paired units alternate-deal the dual-band channel list so each covers half
// the spectrum. Wi-Fi-only in v1: ESP-NOW shares the Wi-Fi radio and bringing
// NimBLE up alongside it would tear Wi-Fi (and the ESP-NOW session) down on the
// Mini, so Split Wardrive logs Wi-Fi APs only. Each board uses its own GPS.
//
// The ESP-NOW recv callback runs in the Wi-Fi task; it only copies the frame
// into linkPacketQueue (single-producer / single-consumer ring). All parsing and
// SD/UI work happens in updateLink() on the main loop.

// Channel the split scanner is currently dwelling on (reported in TELEM). Local
// to this tab; every link.ino function is defined after it.
static uint8_t linkScanChannel = 0;

// ---- ESP-NOW plumbing ---------------------------------------------------

void onLinkRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (len != static_cast<int>(sizeof(LinkPacket))) return;
  const int next = (linkPacketHead + 1) % kLinkPacketQueueSlots;
  if (next == linkPacketTail) return;  // queue full: drop
  LinkQueueItem& item = linkPacketQueue[linkPacketHead];
  memcpy(&item.pkt, data, sizeof(LinkPacket));
  item.rssi = (info && info->rx_ctrl) ? info->rx_ctrl->rssi : -127;
  if (info && info->src_addr) memcpy(item.pkt.srcMac, info->src_addr, 6);
  linkPacketHead = next;
}

bool linkEnsureEspNow() {
  if (linkEspNowReady && WiFi.getMode() == WIFI_STA) return true;
  // A BLE-only tool visited in between can deinit Wi-Fi (and with it ESP-NOW);
  // treat that as stale and rebuild.
  linkEspNowReady = false;
  if (!ensureWifiStation(true)) return false;  // Wi-Fi-only: free BLE memory
  WiFi.disconnect(false, false);
  esp_wifi_get_mac(WIFI_IF_STA, linkSelfMac);
  if (esp_now_init() != ESP_OK) {
    Serial.println("[link] esp_now_init failed");
    return false;
  }
  esp_now_register_recv_cb(onLinkRecv);
  esp_now_set_pmk(kLinkPmk);  // enables encrypted unicast peers added at pairing
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, kLinkBroadcastAddr, 6);
  peer.channel = 0;  // send on the current channel
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  if (!esp_now_is_peer_exist(kLinkBroadcastAddr)) {
    esp_now_add_peer(&peer);
  }
  linkPacketHead = 0;
  linkPacketTail = 0;
  linkEspNowReady = true;
  Serial.printf("[link] esp-now ready, self %02X:%02X:%02X:%02X:%02X:%02X\n",
                linkSelfMac[0], linkSelfMac[1], linkSelfMac[2], linkSelfMac[3],
                linkSelfMac[4], linkSelfMac[5]);
  return true;
}

uint16_t linkComputeCode(const uint8_t* a, const uint8_t* b) {
  // Deterministic on the unordered MAC pair, so both units show the same code.
  const uint8_t* lo = a;
  const uint8_t* hi = b;
  if (memcmp(a, b, 6) > 0) {
    lo = b;
    hi = a;
  }
  uint32_t h = 2166136261u;  // FNV-1a
  for (int i = 0; i < 6; ++i) h = (h ^ lo[i]) * 16777619u;
  for (int i = 0; i < 6; ++i) h = (h ^ hi[i]) * 16777619u;
  return static_cast<uint16_t>(h % 10000);
}

uint32_t linkNow() {
  return static_cast<uint32_t>(static_cast<int32_t>(millis()) + linkClockOffset);
}

void linkFillCommon(LinkPacket& p, uint8_t type) {
  p.magic = kLinkMagic;
  p.version = kLinkProtoVersion;
  p.type = type;
  p.role = linkRoleMaster ? 0 : 1;
  memcpy(p.srcMac, linkSelfMac, 6);
}

void linkSendHello() {
  LinkPacket p;
  linkFillCommon(p, kLinkMsgHello);
  p.flags = (linkConfirmedLocal ? kLinkFlagConfirmed : 0) |
            (AwokPins::kDualBand ? kLinkFlagDualBand : 0);
  p.code = linkCode;
  p.sessionId = linkSessionId;
  // Carry the clock so the slave can sync while both sit on ch 1 during pairing,
  // before any drive window has to line up. Only meaningful from the master.
  p.masterMillis = millis();
  esp_now_send(kLinkBroadcastAddr, reinterpret_cast<uint8_t*>(&p), sizeof(p));
}

void linkSendSync() {
  LinkPacket p;
  linkFillCommon(p, kLinkMsgSync);
  p.masterMillis = millis();
  p.sessionId = linkSessionId;
  // Unicast to the encrypted peer (only ever sent while paired).
  esp_now_send(linkPeerMac, reinterpret_cast<uint8_t*>(&p), sizeof(p));
}

void linkSendTelem() {
  LinkPacket p;
  linkFillCommon(p, kLinkMsgTelem);
  p.networks = wardriveNetworks;
  p.bleCount = wardriveBleCount;
  p.channel = linkScanChannel;
  p.sessionId = linkSessionId;
  // Unicast to the encrypted peer (only ever sent while paired).
  esp_now_send(linkPeerMac, reinterpret_cast<uint8_t*>(&p), sizeof(p));
}

// Per-pair link key: the baked base with the confirm code mixed in so each pair
// gets its own LMK. Not a secret (the code derives from broadcast MACs), just
// key separation between different unit pairs.
void linkComputeLmk(uint8_t out[16]) {
  memcpy(out, kLinkLmkBase, 16);
  out[0] ^= static_cast<uint8_t>(linkCode & 0xFF);
  out[1] ^= static_cast<uint8_t>(linkCode >> 8);
}

// Add (or refresh) the partner as an encrypted unicast peer. SYNC/TELEM then go
// out encrypted; the plaintext broadcast peer stays for HELLO discovery.
void linkAddEncryptedPeer() {
  if (!linkEspNowReady || !linkPeerValid) return;
  if (esp_now_is_peer_exist(linkPeerMac)) esp_now_del_peer(linkPeerMac);
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, linkPeerMac, 6);
  peer.channel = 0;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = true;
  linkComputeLmk(peer.lmk);
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("[link] add encrypted peer failed");
  }
}

// ---- pairing state machine ----------------------------------------------

void linkFinalizePairing(const LinkPacket& p) {
  linkState = kLinkReady;
  if (linkRoleMaster) {
    if (!linkSessionId) linkSessionId = esp_random();
    linkClockOffset = 0;
  } else if (p.sessionId) {
    linkSessionId = p.sessionId;
  }
  linkAddEncryptedPeer();  // SYNC/TELEM are encrypted unicast from here on
  linkPartnerLastSeenMs = millis();
  recordFirmwareAudit(
      "link", "paired", "success",
      "role=" + String(linkRoleMaster ? "master" : "slave") + "; session=" +
          String(static_cast<unsigned long>(linkSessionId)));
  Serial.printf("[link] paired as %s, session %lu\n",
                linkRoleMaster ? "master" : "slave",
                static_cast<unsigned long>(linkSessionId));
  if (currentView == View::kLinkWardrive) drawLinkWardrive();
}

void linkHandlePacket(const LinkQueueItem& item) {
  const LinkPacket& p = item.pkt;
  if (p.magic != kLinkMagic || p.version != kLinkProtoVersion) return;
  if (memcmp(p.srcMac, linkSelfMac, 6) == 0) return;  // ignore our own echo

  if (p.type == kLinkMsgHello) {
    if (linkState == kLinkDiscovering) {
      memcpy(linkPeerMac, p.srcMac, 6);
      linkPeerValid = true;
      linkPeerDualBand = (p.flags & kLinkFlagDualBand) != 0;
      linkRoleMaster = memcmp(linkSelfMac, linkPeerMac, 6) < 0;
      if (linkRoleMaster && !linkSessionId) linkSessionId = esp_random();
      linkCode = linkComputeCode(linkSelfMac, linkPeerMac);
      linkState = kLinkAwaitConfirm;
      if (!linkRoleMaster) {
        linkClockOffset =
            static_cast<int32_t>(p.masterMillis) - static_cast<int32_t>(millis());
      }
      if (currentView == View::kLinkWardrive) drawLinkWardrive();
    } else if (linkState == kLinkAwaitConfirm) {
      if (memcmp(p.srcMac, linkPeerMac, 6) != 0) return;  // different unit
      if (!linkRoleMaster) {
        linkClockOffset =
            static_cast<int32_t>(p.masterMillis) - static_cast<int32_t>(millis());
      }
      if (linkConfirmedLocal && (p.flags & kLinkFlagConfirmed)) {
        linkFinalizePairing(p);
      }
    } else if (linkState == kLinkReady &&
               memcmp(p.srcMac, linkPeerMac, 6) == 0) {
      if (!linkRoleMaster) {
        linkClockOffset =
            static_cast<int32_t>(p.masterMillis) - static_cast<int32_t>(millis());
      }
      linkPartnerRssi = item.rssi;
      linkPartnerLastSeenMs = millis();
      // A paired peer sends no HELLO, so receiving one means the peer is still
      // trying to finalize and went looking after we already went quiet. Answer
      // with a confirmed HELLO (throttled) so it can latch too. Self-limiting:
      // once the peer is paired it stops sending and these replies stop.
      const uint32_t nowMs = millis();
      if (nowMs - lastLinkHelloMs >= 80) {
        lastLinkHelloMs = nowMs;
        linkSendHello();
      }
    }
    return;
  }

  if (linkState != kLinkReady || memcmp(p.srcMac, linkPeerMac, 6) != 0) return;

  if (p.type == kLinkMsgSync) {
    if (!linkRoleMaster) {
      linkClockOffset =
          static_cast<int32_t>(p.masterMillis) - static_cast<int32_t>(millis());
      if (p.sessionId) linkSessionId = p.sessionId;
    }
  } else if (p.type == kLinkMsgTelem) {
    linkPartnerNetworks = p.networks;
    linkPartnerBle = p.bleCount;
    linkPartnerChannel = p.channel;
    linkPartnerRssi = item.rssi;
    linkPartnerLastSeenMs = millis();
  }
}

void linkDrainPackets() {
  while (linkPacketTail != linkPacketHead) {
    const LinkQueueItem item = linkPacketQueue[linkPacketTail];
    linkPacketTail = (linkPacketTail + 1) % kLinkPacketQueueSlots;
    linkHandlePacket(item);
  }
}

void linkStartDiscovery() {
  if (!linkEnsureEspNow()) {
    drawLinkWardrive();
    return;
  }
  linkState = kLinkDiscovering;
  linkPeerValid = false;
  linkConfirmedLocal = false;
  linkRoleMaster = true;
  linkCode = 0;
  linkSessionId = 0;
  linkClockOffset = 0;
  lastLinkHelloMs = 0;
  esp_wifi_set_channel(kLinkChannel, WIFI_SECOND_CHAN_NONE);
  recordFirmwareAudit("link", "pair_start", "success", "discovering");
  drawLinkWardrive();
}

void linkConfirm() {
  if (linkState != kLinkAwaitConfirm) return;
  linkConfirmedLocal = true;
  linkSendHello();  // advertise the confirmed flag immediately
  drawLinkWardrive();
}

void linkCancelPairing() {
  linkState = kLinkOff;
  linkPeerValid = false;
  linkConfirmedLocal = false;
  linkCode = 0;
}

void linkUnpair() {
  recordFirmwareAudit("link", "unpair", "success", "operator");
  if (linkEspNowReady && linkPeerValid && esp_now_is_peer_exist(linkPeerMac)) {
    esp_now_del_peer(linkPeerMac);
  }
  linkState = kLinkOff;
  linkPeerValid = false;
  linkConfirmedLocal = false;
  linkCode = 0;
  linkSessionId = 0;
  linkClockOffset = 0;
  linkPartnerNetworks = 0;
  linkPartnerBle = 0;
  linkPartnerRssi = -127;
}

// ---- Split Wardrive ------------------------------------------------------

int linkPlanSize(LinkPlan plan) {
  switch (plan) {
    case kLinkPlan24: return kLink24ChannelCount;
    case kLinkPlan5: return kLink5ChannelCount;
    default: return kLink24ChannelCount + kLink5ChannelCount;
  }
}

uint8_t linkPlanAt(LinkPlan plan, int idx) {
  if (plan == kLinkPlan24) return kLink24Channels[idx];
  if (plan == kLinkPlan5) return kLink5Channels[idx];
  return idx < kLink24ChannelCount ? kLink24Channels[idx]
                                   : kLink5Channels[idx - kLink24ChannelCount];
}

// Fills `plan` with this unit's channel set and returns true when that set is
// alternate-dealt by role parity (false = this unit scans the whole set):
//  - Solo: every channel the local board supports (whole set).
//  - Mixed pair (one C5 + one 2.4-only classic): partition by band so nothing
//    overlaps and nothing is dropped — the C5 takes ALL of 5 GHz, the 2.4-only
//    unit takes ALL of 2.4 GHz (whole set, no deal).
//  - Same-capability pair: alternate-deal the shared plan (two C5s split the
//    full dual-band plan; two classics split 2.4 GHz).
bool linkAssignedPlan(LinkPlan& plan) {
  const bool localDual = AwokPins::kDualBand;
  if (linkState != kLinkReady) {  // solo
    plan = localDual ? kLinkPlanFull : kLinkPlan24;
    return false;
  }
  if (localDual != linkPeerDualBand) {  // mixed pair: exclusive band, whole set
    plan = localDual ? kLinkPlan5 : kLinkPlan24;
    return false;
  }
  plan = localDual ? kLinkPlanFull : kLinkPlan24;  // same capability: deal it
  return true;
}

// Denominator shown on the Split Wardrive screen: this unit's set size.
int linkPlanCount() {
  LinkPlan plan;
  linkAssignedPlan(plan);
  return linkPlanSize(plan);
}

uint8_t linkNextAssignedChannel() {
  LinkPlan plan;
  const bool deal = linkAssignedPlan(plan);
  const int count = linkPlanSize(plan);
  const int parity = linkRoleMaster ? 0 : 1;
  for (int tries = 0; tries < count; ++tries) {
    const int idx = linkChannelCursor % count;
    linkChannelCursor = (linkChannelCursor + 1) % count;
    if (!deal || (idx % 2) == parity) return linkPlanAt(plan, idx);
  }
  return linkPlanAt(plan, 0);
}

int linkAssignedChannelCount() {
  LinkPlan plan;
  const bool deal = linkAssignedPlan(plan);
  const int count = linkPlanSize(plan);
  if (!deal) return count;  // solo or mixed: this unit scans the whole set
  int n = 0;
  const int parity = linkRoleMaster ? 0 : 1;
  for (int i = 0; i < count; ++i)
    if ((i % 2) == parity) ++n;
  return n;
}

void linkStartNextScan() {
  linkScanChannel = linkNextAssignedChannel();
  // async, show_hidden, active scan, dwell, single channel.
  WiFi.scanNetworks(true, true, false, kLinkWardriveDwellMs, linkScanChannel);
}

void linkIngestScan(int result) {
  for (int i = 0; i < result; ++i) {
    uint8_t* bssid = WiFi.BSSID(i);
    if (!bssid) continue;
    if (gpsHasFix() && !wardriveMacSeen(bssid)) {
      wardriveAddMac(bssid);
      appendWardriveRow(WiFi.BSSIDstr(i), WiFi.SSID(i), WiFi.encryptionType(i),
                        WiFi.channel(i), WiFi.RSSI(i));
      ++wardriveNetworks;
    }
  }
}

void startLinkWardrive() {
  if (wardriveActive) stopWardrive();  // never run both scanners at once
  if (!linkEnsureEspNow()) {
    drawLinkWardrive();
    return;
  }
  wardriveNetworks = 0;
  wardriveBleCount = 0;  // BLE off in Link v1
  wardriveScans = 0;
  wardriveMacCount = 0;
  wardriveStartMs = millis();
  linkChannelCursor = 0;
  linkInWindow = false;
  linkWindowScanStopped = false;
  lastLinkWardriveDrawMs = 0;
  lastLinkTelemMs = 0;
  lastLinkSyncMs = 0;
  signalMonitorActive = false;

  WiFi.disconnect(false, false);
  wardriveCsvReady = openWardriveCsv();
  linkWardriveActive = true;
  recordFirmwareAudit(
      "link", "split_wardrive_start",
      wardriveCsvReady ? "success" : "degraded",
      String(linkState == kLinkReady ? "paired" : "solo") + "; session=" +
          String(static_cast<unsigned long>(linkSessionId)) + "; channels=" +
          String(linkAssignedChannelCount()));
  Serial.printf("[link] split wardrive started (%s, %d channels)\n",
                linkState == kLinkReady ? "paired" : "solo",
                linkAssignedChannelCount());
  drawLinkWardrive();
}

void stopLinkWardrive() {
  linkWardriveActive = false;
  linkInWindow = false;
  WiFi.scanDelete();
  // Keep Wi-Fi STA resident; powering it down breaks the next radio bring-up.
  Serial.printf("[link] split wardrive stopped; %lu Wi-Fi networks\n",
                static_cast<unsigned long>(wardriveNetworks));
}

void updateLinkWardrive(uint32_t now) {
  const bool paired = linkState == kLinkReady;

  if (paired) {
    const uint32_t lnow = linkNow();
    const bool windowDue = (lnow % kLinkRendezvousMs) < kLinkWindowMs;
    if (windowDue) {
      const int r = WiFi.scanComplete();
      if (r == WIFI_SCAN_RUNNING) {
        // Let the in-flight scan finish before parking on the link channel.
      } else {
        if (r >= 0) {
          linkIngestScan(r);
          WiFi.scanDelete();
          ++wardriveScans;
        }
        if (!linkInWindow) {
          esp_wifi_set_channel(kLinkChannel, WIFI_SECOND_CHAN_NONE);
          linkInWindow = true;
          lastLinkTelemMs = 0;
          lastLinkSyncMs = 0;
        }
        if (now - lastLinkTelemMs >= 60) {
          lastLinkTelemMs = now;
          linkSendTelem();
        }
        if (linkRoleMaster && now - lastLinkSyncMs >= 90) {
          lastLinkSyncMs = now;
          linkSendSync();
        }
      }
      if (currentView == View::kLinkWardrive &&
          now - lastLinkWardriveDrawMs >= kLinkWardriveRedrawMs) {
        lastLinkWardriveDrawMs = now;
        drawLinkWardrive();
      }
      return;
    }
    if (linkInWindow) linkInWindow = false;  // window just closed
  }

  const int result = WiFi.scanComplete();
  if (result == WIFI_SCAN_RUNNING) {
    // scan in progress: nothing to do this pass
  } else if (result >= 0) {
    linkIngestScan(result);
    WiFi.scanDelete();
    ++wardriveScans;
    linkStartNextScan();
  } else {
    linkStartNextScan();
  }

  if (currentView == View::kLinkWardrive &&
      now - lastLinkWardriveDrawMs >= kLinkWardriveRedrawMs) {
    lastLinkWardriveDrawMs = now;
    drawLinkWardrive();
  }
}

void updateLink() {
  if (linkState == kLinkOff && !linkWardriveActive) return;
  linkDrainPackets();
  const uint32_t now = millis();

  // Pairing chatter: broadcast HELLO on the link channel while we look for /
  // confirm a peer (no scanning happens during pairing, so the link is solid).
  if (linkState == kLinkDiscovering || linkState == kLinkAwaitConfirm) {
    if (now - lastLinkHelloMs >= kLinkHelloIntervalMs) {
      lastLinkHelloMs = now;
      linkSendHello();
    }
  }

  if (linkWardriveActive) updateLinkWardrive(now);
}

// ---- UI ------------------------------------------------------------------

void openLinkWardrive() {
  currentView = View::kLinkWardrive;
  linkEnsureEspNow();  // state shown even if it fails
  drawLinkWardrive();
}

void drawLinkWardrive() {
  currentView = View::kLinkWardrive;
  display.fillScreen(kBackground);

  if (linkWardriveActive) {
    const bool paired = linkState == kLinkReady;
    drawHeader("SPLIT WD", paired ? "linked drive" : "solo (unpaired)");
    display.setTextSize(2);
    display.setTextColor(gpsHasFix() ? kGood : kWarn, kBackground);
    display.setCursor(6, 52);
    display.print(gpsHasFix() ? "LOGGING" : "NO FIX");

    display.setTextSize(1);
    display.setTextColor(ILI9341_WHITE, kBackground);
    display.setCursor(6, 86);
    display.printf("Mine:    %lu APs", static_cast<unsigned long>(wardriveNetworks));
    display.setCursor(6, 98);
    if (paired) {
      display.printf("Partner: %lu APs", static_cast<unsigned long>(linkPartnerNetworks));
      display.setTextColor(kAccent, kBackground);
      display.setCursor(6, 110);
      display.printf("Combined: %lu APs",
                     static_cast<unsigned long>(wardriveNetworks + linkPartnerNetworks));
    } else {
      display.print("Partner: --");
    }

    display.setTextColor(kMuted, kBackground);
    display.setCursor(6, 130);
    display.printf("My ch %d (%d of %d)  scans %lu", linkScanChannel,
                   linkAssignedChannelCount(), linkPlanCount(),
                   static_cast<unsigned long>(wardriveScans));
    if (paired) {
      display.setCursor(6, 142);
      display.printf("Role: %s  Partner ch %d",
                     linkRoleMaster ? "master" : "slave", linkPartnerChannel);
    }

    if (paired) {
      const bool lost = millis() - linkPartnerLastSeenMs > kLinkPeerTimeoutMs;
      display.setTextColor(lost ? kBad : kGood, kBackground);
      display.setCursor(6, 162);
      if (lost) {
        display.print("PARTNER LOST - out of range?");
      } else {
        display.printf("Partner link %d dBm", linkPartnerRssi);
      }
    }

    display.setTextColor(kMuted, kBackground);
    display.setCursor(6, 182);
    display.printf("Session %lu", static_cast<unsigned long>(linkSessionId));
    display.setTextColor(wardriveCsvReady ? kAccent : kWarn, kBackground);
    display.setCursor(6, 196);
    display.print(wardriveCsvReady ? "SD: wardrive.csv (WiGLE)"
                                   : "SD unavailable; not logging");
    display.setTextColor(kMuted, kBackground);
    display.setCursor(6, 216);
    display.print("Wi-Fi only in Link mode (no BLE).");
    drawFooter("Stop", "Home");
    return;
  }

  // Not driving: pairing / idle screens.
  if (linkState == kLinkAwaitConfirm) {
    drawHeader("LINK", "confirm this code matches");
    display.setTextColor(kMuted, kBackground);
    display.setCursor(6, 60);
    display.print("Both units should show the same");
    display.setCursor(6, 72);
    display.print("4-digit code. Confirm on each.");
    display.setTextSize(4);
    display.setTextColor(kAccent, kBackground);
    char code[8];
    snprintf(code, sizeof(code), "%04u", linkCode);
    display.setCursor(64, 110);
    display.print(code);
    display.setTextSize(1);
    display.setTextColor(linkConfirmedLocal ? kGood : kWarn, kBackground);
    display.setCursor(6, 168);
    display.print(linkConfirmedLocal ? "Confirmed here; waiting for partner."
                                     : "Press Confirm when codes match.");
    display.setTextColor(kMuted, kBackground);
    display.setCursor(6, 188);
    display.printf("You are the %s (lower MAC wins).",
                   linkRoleMaster ? "master" : "slave");
    drawFooter("Cancel", "Confirm");
    return;
  }

  if (linkState == kLinkDiscovering) {
    drawHeader("LINK", "searching for a partner");
    display.setTextSize(2);
    display.setTextColor(kWarn, kBackground);
    display.setCursor(6, 60);
    display.print("PAIRING...");
    display.setTextSize(1);
    display.setTextColor(ILI9341_WHITE, kBackground);
    display.setCursor(6, 96);
    display.print("Put the other unit into Pair too.");
    display.setCursor(6, 110);
    display.print("Both broadcast on channel 1.");
    display.setTextColor(kMuted, kBackground);
    display.setCursor(6, 134);
    display.print("A 4-digit code appears once they");
    display.setCursor(6, 146);
    display.print("find each other.");
    drawFooter("Cancel", "Cancel");
    return;
  }

  if (linkState == kLinkReady) {
    drawHeader("LINK", "paired");
    display.setTextSize(2);
    display.setTextColor(kGood, kBackground);
    display.setCursor(6, 56);
    display.print("PAIRED");
    display.setTextSize(1);
    display.setTextColor(ILI9341_WHITE, kBackground);
    display.setCursor(6, 92);
    display.printf("Role: %s", linkRoleMaster ? "master" : "slave");
    display.setCursor(6, 106);
    display.printf("Session: %lu", static_cast<unsigned long>(linkSessionId));
    display.setCursor(6, 120);
    display.printf("My set: %d of %d channels", linkAssignedChannelCount(),
                   linkPlanCount());
    display.setTextColor(kMuted, kBackground);
    display.setCursor(6, 144);
    display.print("Start launches the split wardrive.");
    display.setCursor(6, 156);
    display.print("Each unit logs its own WiGLE CSV.");
    drawThreeButtonFooter("Back", "Unpair", "Start");
    return;
  }

  // kLinkOff — unpaired idle.
  drawHeader("LINK", linkEspNowReady ? "two-unit mode" : "ESP-NOW unavailable");
  display.setTextSize(2);
  display.setTextColor(kAccent, kBackground);
  display.setCursor(6, 54);
  display.print("LINK MODE");
  display.setTextSize(1);
  display.setTextColor(ILI9341_WHITE, kBackground);
  display.setCursor(6, 86);
  display.print("Pair with a second AWOKxDAG to");
  display.setCursor(6, 98);
  display.print("split-channel wardrive together.");
  display.setTextColor(kMuted, kBackground);
  display.setCursor(6, 122);
  display.print("Pair  - find + confirm a partner");
  display.setCursor(6, 134);
  display.print("Solo  - wardrive all channels now");
  display.setCursor(6, 158);
  display.print("Paired, each unit scans half the");
  display.setCursor(6, 170);
  display.print("band so you cover it twice as fast.");
  drawThreeButtonFooter("Back", "Pair", "Solo");
}
