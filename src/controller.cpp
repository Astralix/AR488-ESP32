#include <Arduino.h>
#include "controller.h"
#include "gpib.h"
#include "commands.h"
#include "serial.h"

#ifdef ESP32
#include <Preferences.h>
#elif defined(E2END)
#include "AR488_Eeprom.h"
#include <EEPROM.h>
#endif

#ifdef AR488_WIFI_ENABLE
#include <WiFi.h>
#include <esp_system.h>   // esp_read_mac()
#include <ESPmDNS.h>
#endif

#if defined(USE_MACROS)
#include "macros.h"
#endif

// TODO: dbSerial

/*
   Implements most of the CONTROLLER functions;
   Substantially compatible with 'standard' Prologix "++" commands
   (see +savecfg command in the manual for differences)

   Principle of operation:
   - Commands received from USB are buffered and whole terminated lines processed
   - Interface commands prefixed with "++" are passed to the command handler
   - Instrument commands and data not prefixed with '++' are sent directly to the GPIB bus.
   - To receive from the instrument, issue a ++read command or put the controller in auto mode (++auto 1|2)
   - Characters received over the GPIB bus are unbuffered and sent directly to USB
   NOTES:
   - GPIB line in a HIGH state is un-asserted
   - GPIB line in a LOW state is asserted
   - The ATMega processor control pins have a high impedance when set as inputs
   - When set to INPUT_PULLUP, a 10k pull-up (to VCC) resistor is applied to the input
*/

/*
   Standard commands

   ++addr         - display/set device address
   ++auto         - automatically request talk and read response (0=off; 1=Prologix; 2=onquery; 3=continuous)
   ++clr          - send Selected Device Clear to current GPIB address
   ++eoi          - enable/disable assertion of EOI signal
   ++eos          - specify GPIB termination character
   ++eot_enable   - enable/disable appending user specified character to USB output on EOI detection
   ++eot_char     - set character to append to USB output when EOT enabled
   ++ifc          - assert IFC signal for 150 miscoseconds - make AR488 controller in charge
   ++llo          - local lockout - disable front panel operation on instrument
   ++loc          - enable front panel operation on instrument
   ++lon          - put controller in listen-only mode (listen to all traffic)
   ++mode         - set the interface mode (0=controller/1=device)
   ++read         - read data from instrument
   ++read_tmo_ms  - read timeout specified between 1 - 3000 milliseconds
   ++rst          - reset the controller
   ++savecfg      - save configration
   ++spoll        - serial poll the addressed host or all instruments
   ++srq          - return status of srq signal (1-srq asserted/0-srq not asserted)
   ++status       - set the status byte to be returned on being polled (bit 6 = RQS, i.e SRQ asserted)
   ++trg          - send trigger to selected devices (up to 15 addresses)
   ++ver          - display firmware version
*/

/*
   Proprietry commands:

   ++aspoll       - serial poll all instruments (alias: ++spoll all)
   ++default      - set configuration to controller default settings
   ++dcl          - send unaddressed (all) device clear  [power on reset] (is the rst?)
   ++id name      - show/set the name of the interface
   ++id serial    - show/set the serial number of the interface
   ++id verstr    - show/set the version string (replaces setvstr)
   ++idn          - enable/disable reply to *idn? (disabled by default)
   ++ren          - assert or unassert the REN signal
   ++ppoll        - conduct a parallel poll
   ++setvstr      - set custom version string (to identify controller, e.g. "GPIB-USB"). Max 47 chars, excess truncated.
   ++srqauto      - automatically condiuct serial poll when SRQ is asserted
   ++ton          - put controller in talk-only mode (send data only)
   ++verbose      - verbose (human readable) mode
*/

/*
   NOT YET IMPLEMENTED

   ++help     - show summary of commands
   ++myaddr   - set the controller address
*/


Controller::Controller():
	serialstream(NULL), btstream(NULL), tcpstream(NULL)
#ifdef AR488_WIFI_ENABLE
	, wifiserver(23), serverclient()
#endif
{
  serialstream = getSerialStream();
  cmdstream = serialstream;
#ifdef AR488_BT_ENABLE
  btstream = getBTSerialStream();
#endif
}

/***** Add character to the buffer and parse *****/
uint8_t Controller::parseInput(char c) {

  uint8_t r = 0;

  // Line end handling: CR, LF, CR+LF and LF+CR are all accepted as one
  // line end. If the other line end character follows immediately, it
  // belongs to the same line end and is discarded.
  if ((c == CR || c == LF) && !isEsc && lastEol && c != lastEol) {
    lastEol = 0;
    return 0;
  }
  lastEol = 0;

  // Read until buffer full (buffer-size - 2 characters)
  if (pbPtr < PBSIZE) {
    // Actions on specific characters
    switch (c) {
      // Carriage return or newline? Then process the line
      case LF:
      case CR:
        // If escaped just add to buffer
        if (isEsc) {
          addPbuf(c);
          isEsc = false;
        } else {
          lastEol = c;
          // Carriage return on blank line?
          // Note: for data CR and LF will always be escaped
          if (pbPtr == 0) {
#ifdef USE_MACROS
			if (editMacro < NUM_MACROS) {
			  flushPbuf();
			  return 4;  //
			}
#endif
            flushPbuf();
            return 0;
          } else {
            // Buffer starts with ++ and contains at least 3 characters - command?
            if (pbPtr>2 && isCmd(pBuf) && !isPlusEscaped) {
              // Exclamation mark (break read loop command)
              if (pBuf[2]==0x21) {
                r = 3;
                flushPbuf();
              // Otherwise flag command received and ready to process
              }else{
                r = 1;
              }
            // Buffer contains *idn? query and interface to respond
            }else if (pbPtr>3 && config.idn>0 && isIdnQuery(pBuf)){
              sendIdn = true;
              flushPbuf();
            // Buffer has at least 1 character = instrument data to send to gpib bus
            }else if (pbPtr > 0) {
              r = 2;
            }
            isPlusEscaped = false;
          }
        }
        break;
      case ESC:
        // Handle the escape character
        if (isEsc) {
          // Add character to buffer and cancel escape
          addPbuf(c);
          isEsc = false;
        } else {
          // Set escape flag
          isEsc  = true;  // Set escape flag
        }
        break;
      case PLUS:
        if (isEsc) {
          isEsc = false;
          if (pbPtr < 2) isPlusEscaped = true;
        }
        addPbuf(c);
        break;
	  case BS:
	  case DEL:  // most terminals send DEL for the backspace key
        if (isEsc) {
          addPbuf(c);
          isEsc = false;
		} else if (pbPtr > 0) {
			pbPtr--;
		}
		break;
      // Something else?
      default: // any char other than defined above
        // Buffer contains '++' (start of command). Stop sending data to serial port by halting GPIB receive.
        addPbuf(c);
        isEsc = false;
    }
  }
  if (pbPtr >= PBSIZE) {
    if (isCmd(pBuf) && !r) {  // Command without terminator and buffer full
      if (verbose()) {
        cmdstream->println(F("ERROR - Command buffer overflow!"));
      }
      flushPbuf();
    }else{  // Buffer contains data and is full, so process the buffer (send data via GPIB)
      dataBufferFull = true;
      r = 2;
    }
  }
  return r;
}


/***** Is this a command? *****/
bool Controller::isCmd(char *buffr) {
  if (buffr[0] == PLUS && buffr[1] == PLUS) {
#ifdef DEBUG1
    dbSerial->println(F("isCmd: Command detected."));
#endif
    return true;
  }
  return false;
}


/***** Is this an *idn? query? *****/
bool Controller::isIdnQuery(char *buffr) {
  // Check for upper or lower case *idn?
  if (strncasecmp(buffr, "*idn?", 5)==0) {
#ifdef DEBUG1
    dbSerial->println(F("isIdnQuery: Detected IDN query."));
#endif
    return true;
  }
  return false;
}


/***** Add character to the buffer *****/
void Controller::addPbuf(char c) {
  pBuf[pbPtr] = c;
  pbPtr++;
}


/***** Clear the parse buffer *****/
void Controller::flushPbuf() {
  memset(pBuf, '\0', PBSIZE);
  pbPtr = 0;
  lnRdy = 0;
  dataBufferFull = false;
}


/***** Show a prompt *****/
void Controller::showPrompt() {
  if(prompt())
#ifdef USE_MACROS
	if (editMacro < NUM_MACROS)
	  cmdstream->print("| ");
	else
#endif
	  cmdstream->print("> ");
}


/***** Serial event handler *****/
/*
 * Note: the Arduino serial buffer is 64 characters long. Characters are stored in
 * the pBuf buffer until serialEvent_h() is called. parsedInput() takes a character at
 * a time and places it into the 256 character parse buffer whereupon it is parsed
 * to determine whether a command or data are present.
 * lnRdy=0: terminator not detected yet
 * lnRdy=1: terminator detected, sequence in parse buffer is a ++ command
 * lnRdy=2: terminator detected, sequence in parse buffer is data or direct instrument command
 * lnRdy=3: terminator detected, sequence in parse buffer is data or direct instrument command
 * lnRdy=4: terminator detected, sequence in parse buffer is line of the currently edited macro
 */
uint8_t Controller::serialIn_h() {
  uint8_t bufferStatus = 0;
  // Parse serial input until we have detected a line terminator
  while (cmdstream->available() && bufferStatus==0) {   // Parse while characters available and line is not complete
	bufferStatus = parseInput(cmdstream->read());
  }

#ifdef DEBUG1
  if (bufferStatus) {
    dbSerial->print(F("BufferStatus: "));
    dbSerial->println(bufferStatus);
  }
#endif

  lnRdy = bufferStatus;
#if defined(USE_MACROS)
  if (lnRdy > 0 && editMacro < NUM_MACROS)
	// we have a parsed line, and a macro being edited
	lnRdy = 4;
#endif
  return lnRdy;
}


void Controller::reset() {
#ifdef WDTO_1S  // Where defined, reset controller using watchdog timeout
  unsigned long tout;
  tout = millis() + 2000;
  wdt_enable(WDTO_1S);
  while (millis() < tout) {};
  // Should never reach here....
  if (verbose()) {
    arSerial->println(F("Reset FAILED."));
  };
#else  // Otherwise restart program (soft reset)
#if defined(__AVR__)
  asm volatile ("  jmp 0");
#elif defined(ESP32)
  ESP.restart();
#endif
#endif
}

// Default GPIB address of the instrument to talk to (override with -D AR488_DEFAULT_PADDR=n)
#ifndef AR488_DEFAULT_PADDR
#define AR488_DEFAULT_PADDR 1
#endif

void Controller::resetConfig() {
  // Set default values ({'\0'} sets version string array to null)
  config = {false, false, 2, 0, AR488_DEFAULT_PADDR, 0, 0, 0, 0, 1200, 0, {'\0'}, 0, 0, {'\0'}, 0, 0, false, false
#ifdef AR488_WIFI_ENABLE
			,{'\0'}, {'\0'}, {'\0'}
#endif
  };
}

void Controller::initConfig()
{
  /***** Initialise the interface *****/
  resetConfig();
#ifdef ESP32
  Preferences pref;
  pref.begin("ar488", true);
  config.eot_en = pref.getBool("eot_en", config.eot_en);
  config.eoi = pref.getBool("eoi", config.eoi);
  config.cmode = pref.getUChar("cmode", config.cmode);
  config.caddr = pref.getUChar("caddr", config.caddr);
  config.paddr = pref.getUChar("paddr", config.paddr);
  config.saddr = pref.getUChar("saddr", config.saddr);
  config.eos = pref.getUChar("eos", config.eos);
  config.stat = pref.getUChar("stat", config.stat);
  config.amode = pref.getUChar("amode", config.amode);
  config.rtmo = pref.getInt("rtmo", config.rtmo);
  config.eot_ch = pref.getChar("eot_ch", config.eot_ch);
  config.tmbus = pref.getUShort("tmbus", config.tmbus);
  config.eor = pref.getUChar("eor", config.eor);
  config.serial = pref.getUInt("serial", config.serial);
  config.idn = pref.getUInt("idn", config.idn);
  config.isVerb = pref.getBool("isVerb", config.isVerb);
  config.showPrompt = pref.getBool("showPrompt", config.showPrompt);
  if (pref.isKey("vstr")) {
	pref.getBytes("vstr", config.vstr, 48);
  }
  if (pref.isKey("sname")) {
	pref.getBytes("sname", config.sname, 16);
  }
#ifdef AR488_WIFI_ENABLE
  if (pref.isKey("ssid")) {
	pref.getBytes("ssid", config.ssid, sizeof(config.ssid));
  }
  if (pref.isKey("passkey")) {
	pref.getBytes("passkey", config.passkey, sizeof(config.passkey));
  }
  if (pref.isKey("hostname")) {
	pref.getBytes("hostname", config.hostname, sizeof(config.hostname));
  }
#endif
  pref.end();

#elif defined(E2END)
  // Read data from non-volatile memory
  //(will only read if previous config has already been saved)
  //  epGetCfg();
  if (!epGet(0, config)) {
	resetConfig();
  }
#endif

  // Initialize the interface in device mode
  if (config.cmode == 1)
	gpib->initDevice();
  // Initialize the interface in controller mode
  else
	gpib->initController();

  gpib->clearATN();
  gpib->clearSRQ();

}

void Controller::saveConfig()
{
#ifdef ESP32
  Preferences pref;
  if (!pref.begin("ar488", false)) {
    cmdstream->println(F("ERROR - Settings could not be saved!"));
    return;
  }
  pref.putBool("eot_en", config.eot_en);
  pref.putBool("eoi", config.eoi);
  pref.putBool("isVerb", config.isVerb);
  pref.putBool("showPrompt", config.showPrompt);
  pref.putUChar("cmode", config.cmode);
  pref.putUChar("caddr", config.caddr);
  pref.putUChar("paddr", config.paddr);
  pref.putUChar("saddr", config.saddr);
  pref.putUChar("eos", config.eos);
  pref.putUChar("stat", config.stat);
  pref.putUChar("amode", config.amode);
  pref.putInt("rtmo", config.rtmo);
  pref.putChar("eot_ch", config.eot_ch);
  pref.putUShort("tmbus", config.tmbus);
  pref.putUChar("eor", config.eor);
  pref.putUInt("serial", config.serial);
  pref.putUInt("idn", config.idn);

  pref.putBytes("vstr", config.vstr, 48);
  pref.putBytes("sname", config.sname, 16);

#ifdef AR488_WIFI_ENABLE
  pref.putBytes("ssid", config.ssid, sizeof(config.ssid));
  pref.putBytes("passkey", config.passkey, sizeof(config.passkey));
  pref.putBytes("hostname", config.hostname, sizeof(config.hostname));
#endif

  pref.end();
  cmdstream->println(F("Settings saved."));

#elif defined(E2END)
  epPut(0, config);
  cmdstream->println(F("Settings saved."));

#else
  cmdstream->println(F("EEPROM not supported."));
#endif
}

void Controller::sendToInstrument()
{
  if (isRO) return;
  if (pbPtr == 0) return;
  if (pBuf[pbPtr-1] == '?') gpib->isQuery = true;
  gpib->gpibSendData(pBuf, pbPtr, dataBufferFull);
  flushPbuf();
}


/***** Execute a command *****/
void Controller::execCmd()
{
  char line[PBSIZE];
  int dsize = pbPtr;
  // Copy collected chars to line buffer
  memcpy(line, pBuf, pbPtr);

  // Flush the parse buffer
  flushPbuf();

#ifdef DEBUG1
  dbSerial->print(F("execCmd: Command received: ")); printHex(line, dsize);
#endif

  // Its a ++command so shift everything two bytes left (ignore ++) and parse
  for (int i = 0; i < dsize-2; i++) {
    line[i] = line[i + 2];
  }
  // Replace last two bytes with a null (\0) character
  line[dsize - 2] = '\0';
  line[dsize - 1] = '\0';
#ifdef DEBUG1
  dbSerial->print(F("execCmd: Sent to the command processor: ")); printHex(line, dsize-2);
#endif
  // Execute the command
  getCmd(line);

  showPrompt();
}

#ifdef AR488_WIFI_ENABLE
// Reason code of the last failed/lost WiFi connection (0 = none)
static volatile uint8_t wifiLastReason = 0;

static void onWifiDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  wifiLastReason = info.wifi_sta_disconnected.reason;
}

static const __FlashStringHelper *wifiReasonText(uint8_t reason) {
  switch (reason) {
    case WIFI_REASON_NO_AP_FOUND:            return F("SSID not found (out of range or 5 GHz only?)");
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:      return F("authentication failed (wrong password?)");
    case WIFI_REASON_ASSOC_FAIL:             return F("association failed");
    case WIFI_REASON_BEACON_TIMEOUT:         return F("connection lost (signal too weak?)");
    default:                                 return F("connection failed");
  }
}

/***** Network host name: configured name, or "ar488-" + last 3 bytes of the MAC *****/
const char *Controller::wifiHostname()
{
  static char name[32];
  if (strlen(config.hostname) > 0) return config.hostname;
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(name, sizeof(name), "ar488-%02x%02x%02x", mac[3], mac[4], mac[5]);
  return name;
}

/*
 * Start (or restart) the WiFi connection in the background.
 * Does not block: the TCP console server on port 23 is started by
 * connectWifi() as soon as the connection is up, and the ESP32 WiFi
 * stack reconnects automatically if the access point goes away.
 */
void Controller::setupWifi()
{
  if (strlen(config.ssid) == 0) return;
  static bool eventRegistered = false;
  if (!eventRegistered) {
    WiFi.onEvent(onWifiDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    eventRegistered = true;
  }
  wifiLastReason = 0;
  serialstream->print(F("Connecting to WiFi: "));
  serialstream->println(config.ssid);
  // Restart the station interface so that new settings (incl. host name) apply
  if (WiFi.getMode() != WIFI_OFF) {
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
  }
  WiFi.setHostname(wifiHostname());  // must be set before the interface starts
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(config.ssid, config.passkey);
}

void Controller::connectWifi()
{
  bool connected = (WiFi.status() == WL_CONNECTED);
  static bool wasConnected = false;
  if (connected != wasConnected) {
    wasConnected = connected;
    if (connected) {
      wifiLastReason = 0;
      serialstream->print(F("WiFi connected, IP address: "));
      serialstream->println(WiFi.localIP());
      if (!wifiServerUp) {
        wifiserver.begin();
        wifiserver.setNoDelay(true);
        wifiServerUp = true;
      }
      // (Re)announce <hostname>.local and the telnet console via mDNS;
      // restarted on every connect so a changed host name takes effect
      MDNS.end();
      if (MDNS.begin(wifiHostname())) {
        MDNS.setInstanceName("AR488 GPIB adapter");
        MDNS.addService("telnet", "tcp", 23);
        serialstream->print(F("mDNS name: "));
        serialstream->print(wifiHostname());
        serialstream->println(F(".local"));
      } else {
        serialstream->println(F("mDNS start failed"));
      }
    } else {
      serialstream->println(F("WiFi connection lost, reconnecting..."));
    }
  }
  if (connected && wifiServerUp) {
    //check if there are any new clients
    if (wifiserver.hasClient()) {
	  if (!serverclient || !serverclient.connected())
	  {
		if (serverclient) serverclient.stop();
		serverclient = wifiserver.available();
		if (!serverclient)
		  serialstream->println(F("available broken"));
		serialstream->print(F("New TCP client: "));
		serialstream->println(serverclient.remoteIP());
		tcpstream = (Stream*) &serverclient;
	  }
	  else
	  {
        //no free/disconnected spot so reject
        wifiserver.available().stop();
		tcpstream = NULL;
      }
    } else {
	  if (!serverclient.connected() && (cmdstream == (Stream*)&serverclient)) {
		serverclient.stop();
		// client got disconnected
		tcpstream = NULL;
	  }
	}
  }
}

void Controller::scanWifi()
{
  // Scanning needs the station interface, which is off if no SSID is configured
  if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
  cmdstream->println(F("Scanning..."));
  int n = WiFi.scanNetworks();
  if (n < 0) {
    cmdstream->println(F("Scan failed (connection attempt in progress? try again)"));
    return;
  }
  if (n == 0) {
    cmdstream->println(F("No networks found"));
    return;
  }
  cmdstream->print(n);
  cmdstream->println(F(" networks found (* = encrypted):"));
  for (int i = 0; i < n; ++i) {
    cmdstream->print(i + 1);
    cmdstream->print(F(": "));
    cmdstream->print(WiFi.SSID(i));
    cmdstream->print(F(" ("));
    cmdstream->print(WiFi.RSSI(i));
    cmdstream->print(F(" dBm, ch "));
    cmdstream->print(WiFi.channel(i));
    cmdstream->print(F(")"));
    cmdstream->println((WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "" : " *");
  }
  WiFi.scanDelete();
}

/***** Show WiFi connection state *****/
void Controller::statusWifi()
{
  cmdstream->print(F("Host:    "));
  cmdstream->print(wifiHostname());
  cmdstream->println(strlen(config.hostname) ? "" : " (default)");
  cmdstream->print(F("SSID:    "));
  cmdstream->println(strlen(config.ssid) ? config.ssid : "(not set)");
  cmdstream->print(F("Status:  "));
  if (strlen(config.ssid) == 0) {
    cmdstream->println(F("off - set ++wifi ssid / ++wifi pass, then ++wifi connect"));
  } else if (WiFi.status() == WL_CONNECTED) {
    cmdstream->println(F("connected"));
    cmdstream->print(F("IP:      "));
    cmdstream->println(WiFi.localIP());
    cmdstream->print(F("mDNS:    "));
    cmdstream->print(wifiHostname());
    cmdstream->println(F(".local"));
    cmdstream->print(F("Signal:  "));
    cmdstream->print(WiFi.RSSI());
    cmdstream->print(F(" dBm, channel "));
    cmdstream->println(WiFi.channel());
    cmdstream->print(F("Telnet:  port 23, "));
    cmdstream->println(serverclient.connected() ? F("client connected") : F("no client"));
  } else {
    cmdstream->println(F("not connected, retrying"));
    if (wifiLastReason) {
      cmdstream->print(F("Reason:  "));
      cmdstream->print(wifiReasonText(wifiLastReason));
      cmdstream->print(F(" [code "));
      cmdstream->print(wifiLastReason);
      cmdstream->println(F("]"));
    }
  }
  cmdstream->print(F("MAC:     "));
  cmdstream->println(WiFi.macAddress());
}
#endif

#if defined (USE_MACROS)
void Controller::displayMacros()
{
  String macro;

  for(int i=0; i<NUM_MACROS; i++) {
	macro = getMacro(i);
	if (macro.length() > 0) {
	  cmdstream->println(String("Macro ") + i + ":");
	  cmdstream->println(macro);
	}
  }
  cmdstream->println();
}

void Controller::appendToMacro()
{
  String macro;
  macro = pBuf;
  macro.trim();

  if (macro.length() > 0)
  {
	macro = getMacro(editMacro) + macro + "\n";
	if (macro.length() >= MACRO_MAX_LEN) {
	  if (verbose())
		cmdstream->println(F("Macro max length exceeded. Deleting."));
	  deleteMacro(editMacro);
	  editMacro = 255;
	}
	else
	  saveMacro(editMacro, macro);
  }
  else
	// blank line: end of macro edit mode
	editMacro = 255;
  flushPbuf();
  showPrompt();
}

#endif

void Controller::selectStream()
{
#ifdef AR488_WIFI_ENABLE
  // check for new tcp cnx
  if (strlen(config.ssid) > 0)
	connectWifi();
#endif
  Stream *current = cmdstream;

  String nextstream = "";
  if (serialstream->available()) {
	if (cmdstream != serialstream) {
	  nextstream = "serial";
	  cmdstream = serialstream;
	}
  }
  else if ((btstream != NULL) &&  btstream->available()) {
	if (cmdstream != btstream) {
	  nextstream = "BT";
	  cmdstream = btstream;
	}
  }
  else if ((tcpstream != NULL) &&  tcpstream->available()) {
	if (cmdstream != tcpstream) {
	  nextstream = "TCP";
	  cmdstream = tcpstream;
	}
  }

  if (nextstream.length() > 0) {
	current->println("Moving serial console to " + nextstream);
	showPrompt();
  }
}
