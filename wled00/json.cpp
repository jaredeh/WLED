#include "wled.h"

#include "palettes.h"

/*
 * JSON API (De)serialization
 */

bool getVal(JsonVariant elem, byte* val, byte vmin=0, byte vmax=255) {
  if (elem.is<int>()) {
    *val = elem;
    return true;
  } else if (elem.is<const char*>()) {
    const char* str = elem;
    size_t len = strnlen(str, 12);
    if (len == 0 || len > 10) return false;
    parseNumber(str, val, vmin, vmax);
    return true;
  }
  return false; //key does not exist
}

void deserializeSegment(JsonObject elem, byte it, byte presetId)
{
  byte id = elem["id"] | it;
  if (id >= strip.getMaxSegments()) return;

  WS2812FX::Segment& seg = strip.getSegment(id);
  //WS2812FX::Segment prev;
  //prev = seg; //make a backup so we can tell if something changed

  uint16_t start = elem["start"] | seg.start;
  int stop = elem["stop"] | -1;
  if (stop < 0) {
    uint16_t len = elem[F("len")];
    stop = (len > 0) ? start + len : seg.stop;
  }

  if (elem["n"]) {
    // name field exists
    if (seg.name) { //clear old name
      delete[] seg.name;
      seg.name = nullptr;
    }

    const char * name = elem["n"].as<const char*>();
    size_t len = 0;
    if (name != nullptr) len = strlen(name);
    if (len > 0 && len < 33) {
      seg.name = new char[len+1];
      if (seg.name) strlcpy(seg.name, name, 33);
    } else {
      // but is empty (already deleted above)
      elem.remove("n");
    }
  } else if (start != seg.start || stop != seg.stop) {
    // clearing or setting segment without name field
    if (seg.name) {
      delete[] seg.name;
      seg.name = nullptr;
    }
  }

  uint16_t grp = elem["grp"] | seg.grouping;
  uint16_t spc = elem[F("spc")] | seg.spacing;
  strip.setSegment(id, start, stop, grp, spc);

  uint16_t len = 1;
  if (stop > start) len = stop - start;
  int offset = elem[F("of")] | INT32_MAX;
  if (offset != INT32_MAX) {
    int offsetAbs = abs(offset);
    if (offsetAbs > len - 1) offsetAbs %= len;
    if (offset < 0) offsetAbs = len - offsetAbs;
    seg.offset = offsetAbs;
  }
  if (stop > start && seg.offset > len -1) seg.offset = len -1;

  byte segbri = 0;
  if (getVal(elem["bri"], &segbri)) {
    if (segbri > 0) seg.setOpacity(segbri, id);
    seg.setOption(SEG_OPTION_ON, segbri, id);
  }

  bool on = elem["on"] | seg.getOption(SEG_OPTION_ON);
  if (elem["on"].is<const char*>() && elem["on"].as<const char*>()[0] == 't') on = !on;
  seg.setOption(SEG_OPTION_ON, on, id);
  bool frz = elem["frz"] | seg.getOption(SEG_OPTION_FREEZE);
  if (elem["frz"].is<const char*>() && elem["frz"].as<const char*>()[0] == 't') frz = !seg.getOption(SEG_OPTION_FREEZE);
  seg.setOption(SEG_OPTION_FREEZE, frz, id);

  seg.cct = elem["cct"] | seg.cct;

  JsonArray colarr = elem["col"];
  if (!colarr.isNull())
  {
    for (uint8_t i = 0; i < 3; i++)
    {
      int rgbw[] = {0,0,0,0};
      bool colValid = false;
      JsonArray colX = colarr[i];
      if (colX.isNull()) {
        byte brgbw[] = {0,0,0,0};
        const char* hexCol = colarr[i];
        if (hexCol == nullptr) { //Kelvin color temperature (or invalid), e.g 2400
          int kelvin = colarr[i] | -1;
          if (kelvin <  0) continue;
          if (kelvin == 0) seg.setColor(i, 0, id);
          if (kelvin >  0) colorKtoRGB(kelvin, brgbw);
          colValid = true;
        } else { //HEX string, e.g. "FFAA00"
          colValid = colorFromHexString(brgbw, hexCol);
        }
        for (uint8_t c = 0; c < 4; c++) rgbw[c] = brgbw[c];
      } else { //Array of ints (RGB or RGBW color), e.g. [255,160,0]
        byte sz = colX.size();
        if (sz == 0) continue; //do nothing on empty array

        byte cp = copyArray(colX, rgbw, 4);      
        if (cp == 1 && rgbw[0] == 0) 
          seg.setColor(i, 0, id);
        colValid = true;
      }

      if (!colValid) continue;
      if (id == strip.getMainSegmentId() && i < 2) //temporary, to make transition work on main segment
      {
        if (i == 0) {col[0] = rgbw[0]; col[1] = rgbw[1]; col[2] = rgbw[2]; col[3] = rgbw[3];}
        if (i == 1) {colSec[0] = rgbw[0]; colSec[1] = rgbw[1]; colSec[2] = rgbw[2]; colSec[3] = rgbw[3];}
      } else { //normal case, apply directly to segment
        seg.setColor(i, ((rgbw[3] << 24) | ((rgbw[0]&0xFF) << 16) | ((rgbw[1]&0xFF) << 8) | ((rgbw[2]&0xFF))), id);
        if (seg.mode == FX_MODE_STATIC) strip.trigger(); //instant refresh
      }
    }
  }

  // lx parser
  #ifdef WLED_ENABLE_LOXONE
  int lx = elem[F("lx")] | -1;
  if (lx > 0) {
    parseLxJson(lx, id, false);
  }
  int ly = elem[F("ly")] | -1;
  if (ly > 0) {
    parseLxJson(ly, id, true);
  }
  #endif

  //if (pal != seg.palette && pal < strip.getPaletteCount()) strip.setPalette(pal);
  seg.setOption(SEG_OPTION_SELECTED, elem[F("sel")] | seg.getOption(SEG_OPTION_SELECTED));
  seg.setOption(SEG_OPTION_REVERSED, elem["rev"]    | seg.getOption(SEG_OPTION_REVERSED));
  seg.setOption(SEG_OPTION_MIRROR  , elem[F("mi")]  | seg.getOption(SEG_OPTION_MIRROR  ));

  //temporary, strip object gets updated via colorUpdated()
  if (id == strip.getMainSegmentId()) {
    byte effectPrev = effectCurrent;
    effectCurrent = elem["fx"] | effectCurrent;
    if (!presetId && effectCurrent != effectPrev) unloadPlaylist(); //stop playlist if active and FX changed manually
    effectSpeed = elem[F("sx")] | effectSpeed;
    effectIntensity = elem[F("ix")] | effectIntensity;
    effectPalette = elem["pal"] | effectPalette;
  } else { //permanent
    byte fx = elem["fx"] | seg.mode;
    if (fx != seg.mode && fx < strip.getModeCount()) {
      strip.setMode(id, fx);
      if (!presetId) unloadPlaylist(); //stop playlist if active and FX changed manually
    }
    seg.speed = elem[F("sx")] | seg.speed;
    seg.intensity = elem[F("ix")] | seg.intensity;
    seg.palette = elem["pal"] | seg.palette;
  }

  JsonArray iarr = elem[F("i")]; //set individual LEDs
  if (!iarr.isNull()) {
    strip.setPixelSegment(id);

    //freeze and init to black
    if (!seg.getOption(SEG_OPTION_FREEZE)) {
      seg.setOption(SEG_OPTION_FREEZE, true);
      strip.fill(0);
    }

    uint16_t start = 0, stop = 0;
    byte set = 0; //0 nothing set, 1 start set, 2 range set

    for (uint16_t i = 0; i < iarr.size(); i++) {
      if(iarr[i].is<JsonInteger>()) {
        if (!set) {
          start = iarr[i];
          set = 1;
        } else {
          stop = iarr[i];
          set = 2;
        }
      } else { //color
        int rgbw[] = {0,0,0,0};
        JsonArray icol = iarr[i];
        if (!icol.isNull()) { //array, e.g. [255,0,0]
          byte sz = icol.size();
          if (sz > 0 || sz < 5) copyArray(icol, rgbw);
        } else { //hex string, e.g. "FF0000"
          byte brgbw[] = {0,0,0,0};
          const char* hexCol = iarr[i];
          if (colorFromHexString(brgbw, hexCol)) {
            for (uint8_t c = 0; c < 4; c++) rgbw[c] = brgbw[c];
          }
        }

        if (set < 2) stop = start + 1;
        for (uint16_t i = start; i < stop; i++) {
          if (strip.gammaCorrectCol) {
            strip.setPixelColor(i, strip.gamma8(rgbw[0]), strip.gamma8(rgbw[1]), strip.gamma8(rgbw[2]), strip.gamma8(rgbw[3]));
          } else {
            strip.setPixelColor(i, rgbw[0], rgbw[1], rgbw[2], rgbw[3]);
          }
        }
        if (!set) start++;
        set = 0;
      }
    }
    strip.setPixelSegment(255);
    strip.trigger();
  } else if (!elem["frz"] && iarr.isNull()) { //return to regular effect
    seg.setOption(SEG_OPTION_FREEZE, false);
  }
  return; // seg.differs(prev);
}

// deserializes WLED state (fileDoc points to doc object if called from web server)
bool deserializeState(JsonObject root, byte callMode, byte presetId)
{
  DEBUG_PRINTLN(F("Deserializing state"));

  strip.applyToAllSelected = false;
  bool stateResponse = root[F("v")] | false;

  getVal(root["bri"], &bri);

  bool on = root["on"] | (bri > 0);
  if (!on != !bri) toggleOnOff();

  if (root["on"].is<const char*>() && root["on"].as<const char*>()[0] == 't') toggleOnOff();

  int tr = -1;
  if (!presetId || currentPlaylist < 0) { //do not apply transition time from preset if playlist active, as it would override playlist transition times
    tr = root[F("transition")] | -1;
    if (tr >= 0)
    {
      transitionDelay = tr;
      transitionDelay *= 100;
      transitionDelayTemp = transitionDelay;
    }
  }

  tr = root[F("tt")] | -1;
  if (tr >= 0)
  {
    transitionDelayTemp = tr;
    transitionDelayTemp *= 100;
    jsonTransitionOnce = true;
  }
  strip.setTransition(transitionDelayTemp);

  tr = root[F("tb")] | -1;
  if (tr >= 0) strip.timebase = ((uint32_t)tr) - millis();

  JsonObject nl       = root["nl"];
  nightlightActive    = nl["on"]      | nightlightActive;
  nightlightDelayMins = nl[F("dur")]  | nightlightDelayMins;
  nightlightMode      = nl[F("mode")] | nightlightMode;
  nightlightTargetBri = nl[F("tbri")] | nightlightTargetBri;

  JsonObject udpn      = root["udpn"];
  notifyDirect         = udpn["send"] | notifyDirect;
  receiveNotifications = udpn["recv"] | receiveNotifications;
  if ((bool)udpn[F("nn")]) callMode = CALL_MODE_NO_NOTIFY; //send no notification just for this request

  unsigned long timein = root[F("time")] | UINT32_MAX; //backup time source if NTP not synced
  if (timein != UINT32_MAX) {
    setTimeFromAPI(timein);
    if (presetsModifiedTime == 0) presetsModifiedTime = timein;
  }

  doReboot = root[F("rb")] | doReboot;

  realtimeOverride = root[F("lor")] | realtimeOverride;
  if (realtimeOverride > 2) realtimeOverride = REALTIME_OVERRIDE_ALWAYS;

  if (root.containsKey("live")) {
    bool lv = root["live"];
    if (lv) realtimeLock(65000); //enter realtime without timeout
    else    realtimeTimeout = 0; //cancel realtime mode immediately
  }

  byte prevMain = strip.getMainSegmentId();
  strip.mainSegment = root[F("mainseg")] | prevMain;
  if (strip.getMainSegmentId() != prevMain) setValuesFromMainSeg();

  int it = 0;
  JsonVariant segVar = root["seg"];
  if (segVar.is<JsonObject>())
  {
    int id = segVar["id"] | -1;
    
    if (id < 0) { //set all selected segments
      bool didSet = false;
      byte lowestActive = 99;
      for (byte s = 0; s < strip.getMaxSegments(); s++)
      {
        WS2812FX::Segment sg = strip.getSegment(s);
        if (sg.isActive())
        {
          if (lowestActive == 99) lowestActive = s;
          if (sg.isSelected()) {
            deserializeSegment(segVar, s, presetId);
            didSet = true;
          }
        }
      }
      if (!didSet && lowestActive < strip.getMaxSegments()) deserializeSegment(segVar, lowestActive, presetId);
    } else { //set only the segment with the specified ID
      deserializeSegment(segVar, it, presetId);
    }
  } else {
    JsonArray segs = segVar.as<JsonArray>();
    for (JsonObject elem : segs)
    {
      deserializeSegment(elem, it, presetId);
      it++;
    }
  }

  #ifndef WLED_DISABLE_CRONIXIE
    if (root["nx"].is<const char*>()) {
      strncpy(cronixieDisplay, root["nx"], 6);
    }
  #endif

  usermods.readFromJsonState(root);

  int8_t ledmap = root[F("ledmap")] | -1;
  if (ledmap >= 0) {
    //strip.deserializeMap(ledmap); // requires separate JSON buffer
    loadLedmap = ledmap;
  }

  byte ps = root[F("psave")];
  if (ps > 0) {
    DEBUG_PRINTLN(F("Saving preset"));
    savePreset(ps, true, nullptr, root);
  } else {
    ps = root[F("pdel")]; //deletion
    if (ps > 0) {
      DEBUG_PRINTLN(F("Deleting preset"));
      deletePreset(ps);
    }

    if (getVal(root["ps"], &presetCycCurr, 1, 5)) { //load preset (clears state request!)
      DEBUG_PRINTLN(F("Applying preset"));
      if (!presetId) unloadPlaylist(); //stop playlist if preset changed manually
      applyPreset(presetCycCurr, callMode);
      return stateResponse;
    }

    //HTTP API commands
    const char* httpwin = root["win"];
    if (httpwin) {
      String apireq = "win&";
      apireq += httpwin;
      handleSet(nullptr, apireq, false);
    }
  }

  JsonObject playlist = root[F("playlist")];
  if (!playlist.isNull() && loadPlaylist(playlist, presetId)) {
    //do not notify here, because the first playlist entry will do
    if (root["on"].isNull()) callMode = CALL_MODE_NO_NOTIFY;
    else callMode = CALL_MODE_DIRECT_CHANGE;  // possible bugfix for playlist only containing HTTP API preset FX=~
  } else {
    interfaceUpdateCallMode = CALL_MODE_WS_SEND;
  }

  colorUpdated(callMode);

  return stateResponse;
}

void serializeSegment(JsonObject& root, WS2812FX::Segment& seg, byte id, bool forPreset, bool segmentBounds)
{
	root["id"] = id;
  if (segmentBounds) {
    root["start"] = seg.start;
    root["stop"] = seg.stop;
  }
	if (!forPreset) root[F("len")] = seg.stop - seg.start;
  root["grp"] = seg.grouping;
  root[F("spc")] = seg.spacing;
  root[F("of")] = seg.offset;
  root["on"] = seg.getOption(SEG_OPTION_ON);
  root["frz"] = seg.getOption(SEG_OPTION_FREEZE);
  byte segbri = seg.opacity;
  root["bri"] = (segbri) ? segbri : 255;
  root["cct"] = seg.cct;

  if (segmentBounds && seg.name != nullptr) root["n"] = reinterpret_cast<const char *>(seg.name);

  // to conserve RAM we will serialize the col array manually
  // this will reduce RAM footprint from ~300 bytes to 84 bytes per segment
  char colstr[70]; colstr[0] = '['; colstr[1] = '\0';  //max len 68 (5 chan, all 255)
  const char *format = strip.isRgbw ? PSTR("[%u,%u,%u,%u]") : PSTR("[%u,%u,%u]");
  for (uint8_t i = 0; i < 3; i++)
  {
    byte segcol[4]; byte* c = segcol;
    if (id == strip.getMainSegmentId() && i < 2) //temporary, to make transition work on main segment
    {
      c = (i == 0)? col:colSec;
    } else {
      segcol[0] = R(seg.colors[i]);
      segcol[1] = G(seg.colors[i]);
      segcol[2] = B(seg.colors[i]);
      segcol[3] = W(seg.colors[i]);
    }
    char tmpcol[22];
    sprintf_P(tmpcol, format, (unsigned)c[0], (unsigned)c[1], (unsigned)c[2], (unsigned)c[3]);
    strcat(colstr, i<2 ? strcat_P(tmpcol, PSTR(",")) : tmpcol);
  }
  strcat_P(colstr, PSTR("]"));
  root["col"] = serialized(colstr);

	root["fx"]  = seg.mode;
	root[F("sx")]  = seg.speed;
	root[F("ix")]  = seg.intensity;
	root["pal"] = seg.palette;
	root[F("sel")] = seg.isSelected();
	root["rev"] = seg.getOption(SEG_OPTION_REVERSED);
  root[F("mi")]  = seg.getOption(SEG_OPTION_MIRROR);
}

void serializeState(JsonObject root, bool forPreset, bool includeBri, bool segmentBounds)
{
  if (includeBri) {
    root["on"] = (bri > 0);
    root["bri"] = briLast;
    root[F("transition")] = transitionDelay/100; //in 100ms
    root[F("tdd")] = transitionDelayDefault/100; //in 100ms
  }

  if (!forPreset) {
    if (errorFlag) {root[F("error")] = errorFlag; errorFlag = ERR_NONE;}

    root[F("ps")] = currentPreset>0 ? currentPreset : -1;
    root[F("pl")] = currentPlaylist;

    usermods.addToJsonState(root);

    JsonObject nl = root.createNestedObject("nl");
    nl["on"] = nightlightActive;
    nl[F("dur")] = nightlightDelayMins;
    nl[F("mode")] = nightlightMode;
    nl[F("tbri")] = nightlightTargetBri;
    if (nightlightActive) {
      nl[F("rem")] = (nightlightDelayMs - (millis() - nightlightStartTime)) / 1000; // seconds remaining
    } else {
      nl[F("rem")] = -1;
    }

    JsonObject udpn = root.createNestedObject("udpn");
    udpn["send"] = notifyDirect;
    udpn["recv"] = receiveNotifications;

    root[F("lor")] = realtimeOverride;
  }

  root[F("mainseg")] = strip.getMainSegmentId();

  JsonArray seg = root.createNestedArray("seg");
  for (byte s = 0; s < strip.getMaxSegments(); s++)
  {
    WS2812FX::Segment sg = strip.getSegment(s);
    if (sg.isActive())
    {
      JsonObject seg0 = seg.createNestedObject();
      serializeSegment(seg0, sg, s, forPreset, segmentBounds);
    } else if (forPreset && segmentBounds) { //disable segments not part of preset
      JsonObject seg0 = seg.createNestedObject();
      seg0["stop"] = 0;
    }
  }
}

void serializeInfo(JsonObject root)
{
  root[F("ver")] = versionString;
  root[F("vid")] = VERSION;
  //root[F("cn")] = WLED_CODENAME;

  JsonObject leds = root.createNestedObject("leds");
  leds[F("count")] = strip.getLengthTotal();
  leds[F("rgbw")] = strip.isRgbw;
  leds[F("wv")] = false;
  leds["cct"] = allowCCT;
  for (uint8_t s = 0; s < busses.getNumBusses(); s++) {
    Bus *bus = busses.getBus(s);
    if (bus == nullptr || bus->getLength()==0) break;
    switch (bus->getType()) {
      case TYPE_ANALOG_5CH:
      case TYPE_ANALOG_2CH:
        leds["cct"] = true;
        break;
    }
    switch (bus->getAutoWhiteMode()) {
      case RGBW_MODE_MANUAL_ONLY:
      case RGBW_MODE_DUAL:
        if (bus->isRgbw()) leds[F("wv")] = true;
        break;
    }
  }

  JsonArray leds_pin = leds.createNestedArray("pin");
  for (uint8_t s=0; s<busses.getNumBusses(); s++) {
    Bus *bus = busses.getBus(s);
    uint8_t pins[5];
    bus->getPins(pins);
    leds_pin.add(pins[0]); // need to elaborate this for multipin strips
  }
  
  leds[F("pwr")] = strip.currentMilliamps;
  leds[F("fps")] = strip.getFps();
  leds[F("maxpwr")] = (strip.currentMilliamps)? strip.ablMilliampsMax : 0;
  leds[F("maxseg")] = strip.getMaxSegments();
  leds[F("seglock")] = false; //will be used in the future to prevent modifications to segment config

  root[F("str")] = syncToggleReceive;

  root[F("name")] = serverDescription;
  root[F("udpport")] = udpPort;
  root["live"] = (bool)realtimeMode;

  switch (realtimeMode) {
    case REALTIME_MODE_INACTIVE: root["lm"] = ""; break;
    case REALTIME_MODE_GENERIC:  root["lm"] = ""; break;
    case REALTIME_MODE_UDP:      root["lm"] = F("UDP"); break;
    case REALTIME_MODE_HYPERION: root["lm"] = F("Hyperion"); break;
    case REALTIME_MODE_E131:     root["lm"] = F("E1.31"); break;
    case REALTIME_MODE_ADALIGHT: root["lm"] = F("USB Adalight/TPM2"); break;
    case REALTIME_MODE_ARTNET:   root["lm"] = F("Art-Net"); break;
    case REALTIME_MODE_TPM2NET:  root["lm"] = F("tpm2.net"); break;
    case REALTIME_MODE_DDP:      root["lm"] = F("DDP"); break;
  }

  if (realtimeIP[0] == 0)
  {
    root[F("lip")] = "";
  } else {
    root[F("lip")] = realtimeIP.toString();
  }

  #ifdef WLED_ENABLE_WEBSOCKETS
  root[F("ws")] = ws.count();
  #else
  root[F("ws")] = -1;
  #endif

  root[F("fxcount")] = strip.getModeCount();
  root[F("palcount")] = strip.getPaletteCount();

  JsonObject wifi_info = root.createNestedObject("wifi");
  wifi_info[F("bssid")] = WiFi.BSSIDstr();
  int qrssi = WiFi.RSSI();
  wifi_info[F("rssi")] = qrssi;
  wifi_info[F("signal")] = getSignalQuality(qrssi);
  wifi_info[F("channel")] = WiFi.channel();

  JsonObject fs_info = root.createNestedObject("fs");
  fs_info["u"] = fsBytesUsed / 1000;
  fs_info["t"] = fsBytesTotal / 1000;
  fs_info[F("pmt")] = presetsModifiedTime;

  root[F("ndc")] = nodeListEnabled ? (int)Nodes.size() : -1;
  
  #ifdef ARDUINO_ARCH_ESP32
  #ifdef WLED_DEBUG
    wifi_info[F("txPower")] = (int) WiFi.getTxPower();
    wifi_info[F("sleep")] = (bool) WiFi.getSleep();
  #endif
  root[F("arch")] = "esp32";
  root[F("core")] = ESP.getSdkVersion();
  //root[F("maxalloc")] = ESP.getMaxAllocHeap();
  #ifdef WLED_DEBUG
    root[F("resetReason0")] = (int)rtc_get_reset_reason(0);
    root[F("resetReason1")] = (int)rtc_get_reset_reason(1);
  #endif
  root[F("lwip")] = 0;
  #else
  root[F("arch")] = "esp8266";
  root[F("core")] = ESP.getCoreVersion();
  //root[F("maxalloc")] = ESP.getMaxFreeBlockSize();
  #ifdef WLED_DEBUG
    root[F("resetReason")] = (int)ESP.getResetInfoPtr()->reason;
  #endif
  root[F("lwip")] = LWIP_VERSION_MAJOR;
  #endif

  root[F("freeheap")] = ESP.getFreeHeap();
  #if defined(ARDUINO_ARCH_ESP32) && defined(WLED_USE_PSRAM)
  if (psramFound()) root[F("psram")] = ESP.getFreePsram();
  #endif
  root[F("uptime")] = millis()/1000 + rolloverMillis*4294967;

  usermods.addToJsonInfo(root);

  byte os = 0;
  #ifdef WLED_DEBUG
  os  = 0x80;
  #endif
  #ifndef WLED_DISABLE_ALEXA
  os += 0x40;
  #endif
  #ifndef WLED_DISABLE_BLYNK
  os += 0x20;
  #endif
  #ifndef WLED_DISABLE_CRONIXIE
  os += 0x10;
  #endif
  #ifndef WLED_DISABLE_FILESYSTEM
  os += 0x08;
  #endif
  #ifndef WLED_DISABLE_HUESYNC
  os += 0x04;
  #endif
  #ifdef WLED_ENABLE_ADALIGHT
  os += 0x02;
  #endif
  #ifndef WLED_DISABLE_OTA
  os += 0x01;
  #endif
  root[F("opt")] = os;

  root[F("brand")] = "WLED";
  root[F("product")] = F("FOSS");
  root["mac"] = escapedMac;
  char s[16] = "";
  if (Network.isConnected())
  {
    IPAddress localIP = Network.localIP();
    sprintf(s, "%d.%d.%d.%d", localIP[0], localIP[1], localIP[2], localIP[3]);
  }
  root["ip"] = s;
}

void setPaletteColors(JsonArray json, CRGBPalette16 palette)
{
    for (int i = 0; i < 16; i++) {
      JsonArray colors =  json.createNestedArray();
      CRGB color = palette[i];
      colors.add((((float)i / (float)16) * 255));
      colors.add(color.red);
      colors.add(color.green);
      colors.add(color.blue);
    }
}

void setPaletteColors(JsonArray json, byte* tcp)
{
    TRGBGradientPaletteEntryUnion* ent = (TRGBGradientPaletteEntryUnion*)(tcp);
    TRGBGradientPaletteEntryUnion u;

    // Count entries
    uint16_t count = 0;
    do {
        u = *(ent + count);
        count++;
    } while ( u.index != 255);

    u = *ent;
    int indexstart = 0;
    while( indexstart < 255) {
      indexstart = u.index;

      JsonArray colors =  json.createNestedArray();
      colors.add(u.index);
      colors.add(u.r);
      colors.add(u.g);
      colors.add(u.b);

      ent++;
      u = *ent;
    }
}

void serializePalettes(JsonObject root, AsyncWebServerRequest* request)
{
  #ifdef ESP8266
  int itemPerPage = 5;
  #else
  int itemPerPage = 8;
  #endif

  int page = 0;
  if (request->hasParam("page")) {
    page = request->getParam("page")->value().toInt();
  }

  int palettesCount = strip.getPaletteCount();

  int maxPage = (palettesCount -1) / itemPerPage;
  if (page > maxPage) page = maxPage;

  int start = itemPerPage * page;
  int end = start + itemPerPage;
  if (end >= palettesCount) end = palettesCount;

  root[F("m")] = maxPage;
  JsonObject palettes  = root.createNestedObject("p");

  for (int i = start; i < end; i++) {
    JsonArray curPalette = palettes.createNestedArray(String(i));
    CRGB prim;
    CRGB sec;
    CRGB ter;
    switch (i) {
      case 0: //default palette
        setPaletteColors(curPalette, PartyColors_p); 
        break;
      case 1: //random
          curPalette.add("r");
          curPalette.add("r");
          curPalette.add("r");
          curPalette.add("r");
        break;
      case 2: //primary color only
        curPalette.add("c1");
        break;
      case 3: //primary + secondary
        curPalette.add("c1");
        curPalette.add("c1");
        curPalette.add("c2");
        curPalette.add("c2");
        break;
      case 4: //primary + secondary + tertiary
        curPalette.add("c3");
        curPalette.add("c2");
        curPalette.add("c1");
        break;
      case 5: {//primary + secondary (+tert if not off), more distinct
      
        curPalette.add("c1");
        curPalette.add("c1");
        curPalette.add("c1");
        curPalette.add("c1");
        curPalette.add("c1");
        curPalette.add("c2");
        curPalette.add("c2");
        curPalette.add("c2");
        curPalette.add("c2");
        curPalette.add("c2");
        curPalette.add("c3");
        curPalette.add("c3");
        curPalette.add("c3");
        curPalette.add("c3");
        curPalette.add("c3");
        curPalette.add("c1");
        break;}
      case 6: //Party colors
        setPaletteColors(curPalette, PartyColors_p);
        break;
      case 7: //Cloud colors
        setPaletteColors(curPalette, CloudColors_p);
        break;
      case 8: //Lava colors
        setPaletteColors(curPalette, LavaColors_p);
        break;
      case 9: //Ocean colors
        setPaletteColors(curPalette, OceanColors_p);
        break;
      case 10: //Forest colors
        setPaletteColors(curPalette, ForestColors_p);
        break;
      case 11: //Rainbow colors
        setPaletteColors(curPalette, RainbowColors_p);
        break;
      case 12: //Rainbow stripe colors
        setPaletteColors(curPalette, RainbowStripeColors_p);
        break;

      default:
        if (i < 13) {
          break;
        }
        byte tcp[72];
        memcpy_P(tcp, (byte*)pgm_read_dword(&(gGradientPalettes[i - 13])), 72);
        setPaletteColors(curPalette, tcp);
        break;
    }
  }
}

void serializeNodes(JsonObject root)
{
  JsonArray nodes = root.createNestedArray("nodes");

  for (NodesMap::iterator it = Nodes.begin(); it != Nodes.end(); ++it)
  {
    if (it->second.ip[0] != 0)
    {
      JsonObject node = nodes.createNestedObject();
      node[F("name")] = it->second.nodeName;
      node["type"]    = it->second.nodeType;
      node["ip"]      = it->second.ip.toString();
      node[F("age")]  = it->second.age;
      node[F("vid")]  = it->second.build;
    }
  }
}

// deserializes mode names string into JsonArray
// also removes WLED-SR extensions (@...) from deserialised names
void deserializeModeNames(JsonArray arr, const char *qstring) {
  String lineBuffer;
  bool insideQuotes = false;
  char singleJsonSymbol;

  // Find the mode name in JSON
  for (size_t i = 0; i < strlen_P(qstring); i++) {
    singleJsonSymbol = pgm_read_byte_near(qstring + i);
    if (singleJsonSymbol == '\0') break;
    switch (singleJsonSymbol) {
      case '"':
        insideQuotes = !insideQuotes;
        break;
      case '[':
        break;
      case ']':
      case ',':
        if (lineBuffer.length() > 0) {
          uint8_t endPos = lineBuffer.indexOf('@');
          if (endPos>0) arr.add(lineBuffer.substring(0,endPos));
          else          arr.add(lineBuffer);
          lineBuffer.clear();
        }
        break;
      default:
        if (!insideQuotes) break;
        lineBuffer += singleJsonSymbol;
    }
  }
}

// extracts effect mode (or palette) name from names serialized string
// caller must provide large enough buffer!
uint8_t extractModeName(uint8_t mode, const char *src, char *dest, uint8_t maxLen)
{
  uint8_t qComma = 0;
  bool insideQuotes = false;
  uint8_t printedChars = 0;
  char singleJsonSymbol;

  // Find the mode name in JSON
  for (size_t i = 0; i < strlen_P(src); i++) {
    singleJsonSymbol = pgm_read_byte_near(src + i);
    if (singleJsonSymbol == '\0') break;
    switch (singleJsonSymbol) {
      case '"':
        insideQuotes = !insideQuotes;
        break;
      case '[':
      case ']':
        break;
      case ',':
        if (!insideQuotes) qComma++;
      default:
        if (!insideQuotes || (qComma != mode)) break;
        dest[printedChars++] = singleJsonSymbol;
    }
    if ((qComma > mode) || (printedChars >= maxLen)) break;
  }
  dest[printedChars] = '\0';
  char *pos = strchr(dest,'@');
  if (pos) *pos = '\0';
  return strlen(dest);
}

void serveJson(AsyncWebServerRequest* request)
{
  byte subJson = 0;
  const String& url = request->url();
  if      (url.indexOf("state") > 0) subJson = 1;
  else if (url.indexOf("info")  > 0) subJson = 2;
  else if (url.indexOf("si")    > 0) subJson = 3;
  else if (url.indexOf("nodes") > 0) subJson = 4;
  else if (url.indexOf("palx")  > 0) subJson = 5;
  else if (url.indexOf("live")  > 0) {
    serveLiveLeds(request);
    return;
  }
  else if (url.indexOf(F("eff")) > 0) {
    // this is going to serve raw effect names which will include WLED-SR extensions in names
    request->send_P(200, "application/json", JSON_mode_names);
    // if we want parsed effect names use this (warning, this will prevent UI from receiving this extension making it useless)
    //AsyncJsonResponse* response = new AsyncJsonResponse(JSON_BUFFER_SIZE, true);  // array document
    //JsonArray doc = response->getRoot();
    //deserializeModeNames(doc, JSON_mode_names); // remove WLED-SR extensions from effect names
    //response->setLength();
    //request->send(response);
    //delete response;
    return;
  }
  else if (url.indexOf("pal") > 0) {
    request->send_P(200, "application/json", JSON_palette_names);
    return;
  }
  else if (url.indexOf("cfg") > 0 && handleFileRead(request, "/cfg.json")) {
    return;
  }
  else if (url.length() > 6) { //not just /json
    request->send(  501, "application/json", F("{\"error\":\"Not implemented\"}"));
    return;
  }

#ifdef WLED_USE_DYNAMIC_JSON
  AsyncJsonResponse* response = new AsyncJsonResponse(JSON_BUFFER_SIZE);
#else
  if (!requestJSONBufferLock()) {
    DEBUG_PRINTLN(F("ERROR: Locking JSON buffer failed!"));
    return;
  }
  AsyncJsonResponse *response = new AsyncJsonResponse(&doc);
#endif

  JsonObject lDoc = response->getRoot();

  switch (subJson)
  {
    case 1: //state
      serializeState(lDoc); break;
    case 2: //info
      serializeInfo(lDoc); break;
    case 4: //node list
      serializeNodes(lDoc); break;
    case 5: //palettes
      serializePalettes(lDoc, request); break;
    default: //all
      JsonObject state = lDoc.createNestedObject("state");
      serializeState(state);
      JsonObject info = lDoc.createNestedObject("info");
      serializeInfo(info);
      if (subJson != 3)
      {
        //lDoc[F("effects")]  = serialized((const __FlashStringHelper*)JSON_mode_names);
        JsonArray effects = lDoc.createNestedArray(F("effects"));
        deserializeModeNames(effects, JSON_mode_names); // remove WLED-SR extensions from effect names
        lDoc[F("palettes")] = serialized((const __FlashStringHelper*)JSON_palette_names);
      }
  }

  DEBUG_PRINTF("JSON buffer size: %u for request: %d\n", lDoc.memoryUsage(), subJson);

  response->setLength();
  request->send(response);
  releaseJSONBufferLock();
}

#define MAX_LIVE_LEDS 180

bool serveLiveLeds(AsyncWebServerRequest* request, uint32_t wsClient)
{
  #ifdef WLED_ENABLE_WEBSOCKETS
  AsyncWebSocketClient * wsc = nullptr;
  if (!request) { //not HTTP, use Websockets
    wsc = ws.client(wsClient);
    if (!wsc || wsc->queueLength() > 0) return false; //only send if queue free
  }
  #endif

  uint16_t used = strip.getLengthTotal();
  uint16_t n = (used -1) /MAX_LIVE_LEDS +1; //only serve every n'th LED if count over MAX_LIVE_LEDS
  char buffer[2000];
  strcpy_P(buffer, PSTR("{\"leds\":["));
  obuf = buffer;
  olen = 9;

  for (uint16_t i= 0; i < used; i += n)
  {
    olen += sprintf(obuf + olen, "\"%06X\",", strip.getPixelColor(i) & 0xFFFFFF);
  }
  olen -= 1;
  oappend((const char*)F("],\"n\":"));
  oappendi(n);
  oappend("}");
  if (request) {
    request->send(200, "application/json", buffer);
  }
  #ifdef WLED_ENABLE_WEBSOCKETS
  else {
    wsc->text(obuf, olen);
  }
  #endif
  return true;
}
