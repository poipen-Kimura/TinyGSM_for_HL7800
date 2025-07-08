/**
 * @file       TinyGsmClientHL7800.h
 * @author     poipen-Kimura ,Volodymyr Shymanskyy
 * @license    LGPL-3.0
 * @copyright  Copyright (c) 2025 poipen-Kimura
 * @date       Mar 2025
 */

#ifndef SRC_TINYGSMCLIENTHL7800_H_
#define SRC_TINYGSMCLIENTHL7800_H_
// #pragma message("TinyGSM:  TinyGsmClientHL7800")

// #define TINY_GSM_DEBUG Serial

#define TINY_GSM_MUX_COUNT 6
#define TINY_GSM_BUFFER_READ_AND_CHECK_SIZE
#ifdef AT_NL
#undef AT_NL
#endif
#define AT_NL "\r\n"

#ifdef MODEM_MANUFACTURER
#undef MODEM_MANUFACTURER
#endif
#define MODEM_MANUFACTURER "Sierra Wireless"

#ifdef MODEM_MODEL
#undef MODEM_MODEL
#endif
#define MODEM_MODEL "HL7800"

#include "TinyGsmModem.tpp"
#include "TinyGsmTCP.tpp"
#include "TinyGsmGPRS.tpp"
#include "TinyGsmSMS.tpp"
#include "TinyGsmTime.tpp"

enum HL7800RegStatus {
  REG_NO_RESULT               = -1,
  REG_UNREGISTERED            = 0,
  REG_SEARCHING               = 2,
  REG_DENIED                  = 3,
  REG_OK_HOME                 = 1,
  REG_OK_ROAMING              = 5,
  REG_UNKNOWN                 = 4,
  REG_SMS_ONLY_HOME           = 6, // (not applicable)
  REG_SMS_ONLY_ROAMING        = 7, // (not applicable)
  REG_EMERGENCY_ONLY          = 8,
  REG_NO_FALLBACK_LTE_HOME    = 9, // (not applicable)
  REG_NO_FALLBACK_LTE_ROAMING = 10 // (not applicable)
};

class TinyGsmHL7800 : public TinyGsmModem<TinyGsmHL7800>,
                      public TinyGsmGPRS<TinyGsmHL7800>,
                      public TinyGsmTCP<TinyGsmHL7800, TINY_GSM_MUX_COUNT>,
                      public TinyGsmSMS<TinyGsmHL7800>,
                      public TinyGsmTime<TinyGsmHL7800> {
  friend class TinyGsmModem<TinyGsmHL7800>;
  friend class TinyGsmGPRS<TinyGsmHL7800>;
  friend class TinyGsmTCP<TinyGsmHL7800, TINY_GSM_MUX_COUNT>;
  friend class TinyGsmSMS<TinyGsmHL7800>;
  friend class TinyGsmTime<TinyGsmHL7800>;

  /*
   * Inner Client
   */
 public:
  class GsmClientHL7800 : public GsmClient {
    friend class TinyGsmHL7800;

   public:
    GsmClientHL7800() {}

    explicit GsmClientHL7800(TinyGsmHL7800& modem, uint8_t mux = 1) {
      init(&modem, mux);
    }

    bool init(TinyGsmHL7800* modem, uint8_t mux = 1) {
      this->at       = modem;
      sock_connected = false;

      // adjust for zero indexed socket array vs Sierras' 1 indexed mux numbers
      // using modulus will force 6 back to 0
      if (mux >= 1 && mux <= TINY_GSM_MUX_COUNT) {
        this->mux = mux;
      } else {
        this->mux = (mux % TINY_GSM_MUX_COUNT) + 1;
      }
      at->sockets[this->mux % TINY_GSM_MUX_COUNT] = this;

      return true;
    }

   public:
    virtual int connect(const char* host, uint16_t port, int timeout_s) {
      // stop();
      TINY_GSM_YIELD();
      rx.clear();

      uint8_t oldMux = mux;
      sock_connected = at->modemConnect(host, port, &mux, false, timeout_s);
      if (mux != oldMux) {
        DBG("WARNING:  Mux number changed from", oldMux, "to", mux);
        at->sockets[oldMux] = nullptr;
      }
      at->sockets[mux % TINY_GSM_MUX_COUNT] = this;
      at->maintain();
      return sock_connected;
    }
    TINY_GSM_CLIENT_CONNECT_OVERRIDES

    void stop(uint32_t maxWaitMs) {
      TINY_GSM_YIELD();
      at->sendAT(GF("+KTCPCLOSE="), mux, GF(",1"));
      at->waitResponse(maxWaitMs);
      sock_connected = false;
      at->sendAT(GF("+KTCPDEL="), mux);
      at->waitResponse(maxWaitMs);
      rx.clear();
    }
    void stop() override {
      stop(1000L);
    }

    /*
     * Extended API
     */

    String remoteIP() TINY_GSM_ATTR_NOT_IMPLEMENTED;
  };

  /*
   * Inner Secure Client
   */
  // NOT SUPPORTED

  /*
   * Constructor
   */
 public:
  explicit TinyGsmHL7800(Stream& stream) : stream(stream) {
    memset(sockets, 0, sizeof(sockets));
  }

  /*
   * Basic functions
   */
 protected:
  bool initImpl(const char* pin = nullptr) {
    DBG(GF("### TinyGSM Version:"), TINYGSM_VERSION);
    DBG(GF("### TinyGSM Compiled Module:  TinyGsmClientHL7800"));

    if (!testAT()) { return false; }

    sendAT(GF("E0"));  // Echo Off
    if (waitResponse() != 1) { return false; }
    sendAT(GF("&K3"));  // Hardware flow on
    if (waitResponse() != 1) { return false; }
    sendAT(GF("+KPATTERN=\"--EOF--Pattern--\"")); // set EOF pattern
    if (waitResponse() != 1) { return false; }

#ifdef TINY_GSM_DEBUG
    sendAT(GF("+CMEE=1"));  // turn on verbose error codes
#else
    sendAT(GF("+CMEE=0"));  // turn off error codes
#endif
    waitResponse();

    DBG(GF("### Modem:"), getModemName());

    SimStatus ret = getSimStatus();
    // if the sim isn't ready and a pin has been provided, try to unlock the sim
    if (ret != SIM_READY && pin != nullptr && strlen(pin) > 0) {
      simUnlock(pin);
      return (getSimStatus() == SIM_READY);
    } else {
      // if the sim is ready, or it's locked but no pin has been provided,
      // return true
      return (ret == SIM_READY || ret == SIM_LOCKED);
    }
  }

  // // This is extracted from the modem info
  // String getModemNameImpl() {
  //   sendAT(GF("I"));
  //   String name = stream.readStringUntil('\n');  // read the modem name
  //   name.trim();
  //   waitResponse();         // wait for the OK
  //   return name;
  // }

  // This is extracted from the modem info
  // String getModemManufacturerImpl() {
  //   sendAT(GF("+CGMI"));
  //   String factory = stream.readStringUntil('\n');  // read the factory
  //   factory.trim();
  //   if (waitResponse() == 1) { return factory; }
  //   return MODEM_MANUFACTURER;
  // }

  // // This is extracted from the modem info
  // String getModemModelImpl() {
  //   sendAT(GF("I"));
  //   String model = stream.readStringUntil('\n');  // read the model
  //   model.trim();
  //   if (waitResponse() == 1) { return model; }
  //   return MODEM_MODEL;
  // }

  // // Gets the modem firmware version
  // // This is extracted from the modem info
  // String getModemRevisionImpl() {
  //   sendAT(GF("I"));
  //   streamSkipUntil('\n');                      // skip the factory
  //   streamSkipUntil('\n');                      // skip the model
  //   String res = stream.readStringUntil('\n');  // read the revision
  //   res.trim();
  //   waitResponse();  // wait for the OK
  //   return res;
  // }

  // Extra stuff here - pwr save, internal stack
  bool factoryDefaultImpl() {
    sendAT(GF("&FZE0&W"));  // Factory + Reset + Echo Off + Write
    waitResponse();
    sendAT(GF("+IPR=115200"));  // Set-baud-default(115200)
    waitResponse();
    sendAT(GF("&W"));  // Write configuration
    return waitResponse() == 1;
  }

  void maintainImpl() {
#if defined TINY_GSM_BUFFER_READ_AND_CHECK_SIZE
    // Keep listening for modem URC's and proactively iterate through
    // sockets asking if any data is avaiable
    for (int mux = 1; mux <= TINY_GSM_MUX_COUNT; mux++) {
      GsmClientHL7800* sock = sockets[mux % TINY_GSM_MUX_COUNT];
      if (sock && sock->got_data) {
        sock->got_data       = false;
        sock->sock_available = modemGetAvailable(mux);
        // modemGetConnected() always checks the state of ALL socks
        // modemGetConnected();
      }
    }
    while (stream.available()) {
      waitResponse(15, nullptr, nullptr);
    }
#elif defined TINY_GSM_NO_MODEM_BUFFER || defined TINY_GSM_BUFFER_READ_NO_CHECK
    // Just listen for any URC's
    waitResponse(100, nullptr, nullptr);
#else
#error Modem client has been incorrectly created
#endif
  }

  /*
   * Power functions
   */
 protected:
  bool restartImpl(const char* pin = nullptr) {
    if (!testAT()) { return false; }
    if (!setPhoneFunctionality(1, true)) { return false; }
    delay(500);
    // sendAT(GF("E0"));  // Echo Off
    // waitResponse();
    return init(pin);
  }

  bool powerOffImpl() {
    sendAT(GF("+CPWROFF"));
    return waitResponse(3000L) == 1;
  }

  bool sleepEnableImpl(bool enable = true) {
    // Sleep mode is controlled by the AT+KSLEEP command.
    // The command has the following parameters:
    // 0 — Sleep mode permission is driven by a HW signal (DTR).
    //     If the signal is active (low level), the module doesn’t enter sleep mode.
    // 1 — Standalone sleep mode. The module decides by itself when it enters sleep mode.
    // 2 — Sleep mode is always disabled
    sendAT(GF("+KSLEEP="), enable ? "1" : "2");
    return waitResponse() == 1;
  }

  bool setPhoneFunctionalityImpl(uint8_t fun, bool reset = false) {
    sendAT(GF("+CFUN="), fun, reset ? ",1" : "");
    return waitResponse(10000L) == 1;
  }

  /*
   * Generic network functions
   */
 public:
  HL7800RegStatus getRegistrationStatus() {
    // Check first for EPS registration
    HL7800RegStatus epsStatus =
        (HL7800RegStatus)getRegistrationStatusXREG("CEREG");

    // If we're connected on EPS, great!
    if (epsStatus == REG_OK_HOME || epsStatus == REG_OK_ROAMING) {
      return epsStatus;
    } else {
      DBG("### CEREG status:", epsStatus);
      // Otherwise, check generic network status
      return (HL7800RegStatus)getRegistrationStatusXREG("CREG");
    }
  }

 protected:
  bool isNetworkConnectedImpl() {
    HL7800RegStatus s = getRegistrationStatus();
    return (s == REG_OK_HOME || s == REG_OK_ROAMING);
  }

  String getLocalIPImpl() {
    sendAT(GF("+KCGPADDR"));
    if (waitResponse(GF(AT_NL "+KCGPADDR:")) != 1) { return ""; }
    streamSkipUntil('\"');
    String res = stream.readStringUntil('\"');
    waitResponse();
    res.trim();
    return res;
  }

  /*
   * Secure socket layer (SSL) functions
   */
  // No functions of this type supported

  /*
   * WiFi functions
   */
  // No functions of this type supported

  /*
   * GPRS functions
   */
 protected:
  bool gprsConnectImpl(const char* apn, const char* user = nullptr,
                       const char* pwd = nullptr) {
    gprsDisconnect();

    sendAT(GF("+KCNXCFG=1,\"GPRS\",\""), apn, GF("\",\""), user, GF("\",\""), pwd, GF("\", \"IPV4\" ,\"0.0.0.0\",\"0.0.0.0\",\"0.0.0.0\""));
    waitResponse();
    sendAT(GF("+KCNXUP=1"));
    waitResponse();

    const uint32_t timeout_ms = 60000L;
    for (uint32_t start = millis(); millis() - start < timeout_ms;) {
      if (isGprsConnected()) {
        return true;
      }
      delay(500);
    }
    return false;
  }

  bool gprsDisconnectImpl() {
    // TODO(?): There is no command in AT command set
    // XIIC=0 does not work
    return true;
  }

  // bool isGprsConnectedImpl() {
  //   sendAT(GF("+KCNXCFG?"));
  //   if (waitResponse(GF(AT_NL "+KCNXCFG:")) != 1) { return false; }
  //   streamSkipUntil(',');
  //   streamSkipUntil(',');
  //   streamSkipUntil(',');
  //   streamSkipUntil(',');
  //   streamSkipUntil(',');
  //   streamSkipUntil(',');
  //   streamSkipUntil(',');
  //   streamSkipUntil(',');
  //   streamSkipUntil(',');
  //   int8_t res = streamGetIntLength(1);
  //   return res == 2;
  // }

  /*
   * SIM card functions
   */
 protected:
  // Able to follow all SIM card functions as inherited from TinyGsmGPRS.tpp


  /*
   * Phone Call functions
   */
  // No functions of this type supported

  /*
   * Audio functions
   */
  // No functions of this type supported

  /*
   * Text messaging (SMS) functions
   */
 protected:
  bool sendSMS_UTF16Impl(const String& number, const void* text,
                         size_t len) TINY_GSM_ATTR_NOT_AVAILABLE;

  /*
   * GSM Location functions
   */
  // No functions of this type supported

  /*
   * GPS/GNSS/GLONASS location functions
   */
  // No functions of this type supported

  /*
   * Time functions
   */
  // Follows all clock functions as inherited from TinyGsmTime.tpp
  /*
   * NTP server functions
   */
  // Follows all NTP server functions as inherited from TinyGsmNTP.tpp

  /*
   * BLE functions
   */
  // No functions of this type supported

  /*
   * NTP server functions
   */
  // No functions of this type supported

  /*
   * BLE functions
   */
  // No functions of this type supported

  /*
   * Battery functions
   */
  // No functions of this type supported

  /*
   * Temperature functions
   */
  // No functions of this type supported

  /*
   * Client related functions
   */
 protected:
  bool modemConnect(const char* host, uint16_t port, uint8_t* mux,
                    bool ssl = false, int timeout_s = 75) {
    uint32_t timeout_ms = ((uint32_t)timeout_s) * 1000;
    uint32_t startMillis = millis();

    // create a TCPsocket
    sendAT(GF("+KTCPCFG=1,0,\""), host, GF("\","), port);
    // reply is +KTCPCFG: ## of socket created
    if (waitResponse(GF(AT_NL "+KTCPCFG:")) != 1) { return false; }
    *mux = streamGetIntBefore('\n');
    waitResponse();
    
    // connect on the allocated socket
    sendAT(GF("+KTCPCNX="), *mux);
    waitResponse();
    int8_t rsp = waitResponse(timeout_ms - (millis() - startMillis), GF(AT_NL "+KTCP_IND:"));
    return (1 == rsp);
  }

  int16_t modemSend(const void* buff, size_t len, uint8_t mux) {
    if (sockets[mux % TINY_GSM_MUX_COUNT]->sock_connected == false) {
      DBG("### Sock closed, cannot send data!");
      return 0;
    }

    sendAT(GF("+KTCPSND="), mux, ',', (uint16_t)len);
    if (waitResponse(GF(AT_NL "CONNECT" AT_NL)) != 1) { return 0; }
    stream.write(reinterpret_cast<const uint8_t*>(buff), len);
    stream.write(GF("--EOF--Pattern--"));
    stream.flush();
    if (waitResponse() != 1) {
      DBG("### no OK after send");
      return 0;
    }
    if (waitResponse(500, GF(AT_NL "+KTCP_DATA:")) != 1) {
      DBG("### No response data");
      // return 0; -- this is not an error, Response data may not be available.
    }

    DBG("### modemSend finished", len, " on mux:", mux);
    return len;
  }

  size_t modemRead(size_t size, uint8_t mux) {
    if (!sockets[mux % TINY_GSM_MUX_COUNT]) return 0;
    sendAT(GF("+KTCPRCV="), mux, ',', (uint16_t)size);
    if (waitResponse(GF(AT_NL "CONNECT" AT_NL)) != 1) { return 0; }
    // streamSkipUntil(',');  // Skip mux
    // int16_t len = streamGetIntBefore(',');
    // streamSkipUntil('\"');

    for (int i = 0; i < size; i++) { moveCharFromStreamToFifo(mux); }
    // streamSkipUntil('\"');
    waitResponse(GF("--EOF--Pattern--"));
    waitResponse();
    DBG("### READ:", size, "from", mux);
    sockets[mux % TINY_GSM_MUX_COUNT]->sock_available = modemGetAvailable(mux);
    return size;
  }

  size_t modemGetAvailable(uint8_t mux) {
    sendAT(GF("+KTCPSTAT="), mux);
    size_t result = 0;
    int8_t tcp_notif = 0;
    if (waitResponse(GF("+KTCPSTAT:")) == 1) {
      streamSkipUntil(',');                       // Skip status
      tcp_notif = streamGetIntBefore(',');        // get  tcp_notif
      streamSkipUntil(',');                       // Skip rem_data
      result = streamGetIntBefore('\n');          // get  rcv_data
      waitResponse();
    }
    if (!result) { sockets[mux % TINY_GSM_MUX_COUNT]->sock_connected = (tcp_notif == -1); }
    if (result) { DBG("### Available:", result, "on", mux); }
    return result;
  }

  bool modemGetConnected(uint8_t mux) {
    sendAT(GF("+KTCPSTAT="), mux);
    uint8_t res = waitResponse(GF(AT_NL "+KTCPSTAT:"));
    if (res != 1) { return false; }

    int8_t status = streamGetIntBefore(',');
    // 0: Socket not defined, use +KTCPCFG to create a TCP socket
    // 1: Socket is only defined but not used
    // 2: Socket is opening and connecting to the server, cannot be used
    // 3: Connection is up, socket can be used to send/receive data
    // 4: Connection is closing, it cannot be used, wait for status 5
    // 5: Socket is closed

    int8_t tcp_notif = streamGetIntBefore(',');

    waitResponse();
    return (-1 == tcp_notif);
  }

  String dnsIpQuery(const char* host) {
    sendAT(GF("+DNS=\""), host, GF("\""));
    if (waitResponse(10000L, GF(AT_NL "+DNS:")) != 1) { return ""; }
    String res = stream.readStringUntil('\n');
    waitResponse(GF("+DNS:OK" AT_NL));
    res.trim();
    return res;
  }

  inline void moveCharFromStreamToFifo(uint8_t mux) {
    if (!sockets[mux % TINY_GSM_MUX_COUNT]) return;
    uint32_t startMillis = millis();
    while (!stream.available() &&
           (millis() - startMillis < sockets[mux % TINY_GSM_MUX_COUNT]->_timeout)) {
      TINY_GSM_YIELD();
    }
    char c = stream.read();
    sockets[mux % TINY_GSM_MUX_COUNT]->rx.put(c);
    // char vc = (c >= 32 && c <= 126) ? c : '?';
    // char buf[40];
    // sprintf(buf, "[ %c ] (0x%02x)", vc, c);
    // DBG("### modem -> fifo: ", buf, "on session:", mux);
  }

  /*
   * Utilities
   */
 public:
  bool handleURCs(String& data) {
    if (data.endsWith(GF("+KTCP_DATA:"))) {
      int8_t  mux      = streamGetIntBefore(',');
      int16_t len      = streamGetIntBefore('\n');
      if (mux >= 0 && mux < TINY_GSM_MUX_COUNT && sockets[mux % TINY_GSM_MUX_COUNT]) {
        sockets[mux % TINY_GSM_MUX_COUNT]->got_data       = true;
        sockets[mux % TINY_GSM_MUX_COUNT]->sock_available = len;
      }
      data = "";
      DBG("### Data Received URC: ", len, "on", mux);
      return true;
    } else if (data.endsWith(GF("+TCPCLOSE:"))) {
      int8_t mux = streamGetIntBefore(',');
      streamSkipUntil('\n');
      if (mux >= 0 && mux < TINY_GSM_MUX_COUNT && sockets[mux % TINY_GSM_MUX_COUNT]) {
        sockets[mux % TINY_GSM_MUX_COUNT]->sock_connected = false;
      }
      data = "";
      DBG("### Socket Closed: ", mux);
      return true;
    }
    return false;
  }

 public:
  Stream& stream;

 protected:
  GsmClientHL7800* sockets[TINY_GSM_MUX_COUNT];
};

#endif  // SRC_TINYGSMCLIENTHL7800_H_
