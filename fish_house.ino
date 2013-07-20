#include <Time.h>
#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <ICMPPing.h>
#include <WebServer.h>
#include <Syslog.h>
#include "Config.h"

/*#define DEBUG*/
#define USE_SYSTEM_INFO

#define MAINTAIN_INTERVAL         10

#define LIGHT_ON_START            7  // hour
#define LIGHT_ON_FINISH           22 // hour
#define LIGHT_SWITCH_PIN          3
#define LIGHT_SENSOR_PIN          0

#define FEED_ON_START             8  // hour
#define FEED_ON_FINISH            9  // hour
#define FEED_DURATION             4
#define FEED_SWITCH_PIN           4

#define WEBSERVER_PREFIX          ""
#define WEBSERVER_PORT            80

#define CONNECTION_CHECK_INTERVAL 300 // seconds
#define MAC_ADDRESSS              0x70, 0x5A, 0xB6, 0x01, 0x02, 0x03

#define NTP_SYNC_INTERVAL         3600 // seconds
#define NTP_TIME_SERVER           132, 163, 4, 101
#define NTP_PACKET_SIZE           48
#define NTP_LOCAL_PORT            8887
#define NTP_TIME_ZONE             3

#define PING_SERVER               8, 8, 8, 8

byte    loggerHost[] =            { 67, 214, 212, 103 };

/* Switches */
bool           lightSwitchModeAuto = true;
unsigned long  timeLastMaintain    = 0;
time_t         lastFeed            = 0;
bool           feedSwitchModeAuto  = true;

/* WebServer */
WebServer      webserver(WEBSERVER_PREFIX, WEBSERVER_PORT);

/* Ethernet */
EthernetClient client;
byte           mac[]             = { MAC_ADDRESSS };
bool           isFirstConnection = true;
bool           isConnected       = false;
unsigned long  lastCheck         = 0;

/* NTP */
EthernetUDP    Udp;
IPAddress      timeServer(NTP_TIME_SERVER); // time-a.timefreq.bldrdoc.gov
byte           packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets
unsigned long  timeLastUpdate    = 0;
char           ntpBuffer[32];

/* Ping */
IPAddress      pingAddr(PING_SERVER);
SOCKET         pingSocket = 3;
ICMPPing       ping(pingSocket, (uint16_t) random(0, 255));

/* Relay */
#define RELAY_ON                  0
#define RELAY_OFF                 1

/* Debug print */
#ifdef DEBUG
#define debug_print(msg)          Serial.print(F(msg))
#define debug_println(msg)        Serial.println(F(msg))
#else
#define debug_print(msg)
#define debug_println(msg)
#endif

#ifdef USE_SYSTEM_INFO
int ramFree() {
    extern int __heap_start, *__brkval;
    int v;
    int a = (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
    return a;
}

int ramSize() {
    int v;
    int a = (int) &v;
    return a;
}
#endif

char *currentTime(){
    sprintf(ntpBuffer, "Current time: %04d/%02d/%02d %02d:%02d:%02d", year(), month(), day(), hour(), minute(), second());
    return ntpBuffer;
}

P(HTML_HEAD) =
"<!DOCTYPE html><html><head>"
  "<title>Fish house</title>"
"</head><body>"
;

P(HTML_FORM) =
"<h1>Fish house!</h1>"
;

P(HTML_LIGHT_CONTROLS) =
"<p>"
    "<button onclick=\"location.href='/light/on'\">Light turn ON</button>"
    "<button onclick=\"location.href='/light/off'\">Light turn OFF</button>"
    "<button onclick=\"location.href='/light/auto'\">Auto light mode</button>"
"</p>"
;

P(HTML_FEED_CONTROLS) =
"<p>"
    "<button onclick=\"location.href='/feed/start'\">Start feeding</button>"
    "<button onclick=\"location.href='/feed/auto'\">Auto feed mode</button>"
    "<button onclick=\"location.href='/feed/manual'\">Manual feed mode</button>"
"</p>"
;

P(HTML_TAIL) =
"</body></html>"
;

P(HTML_H3_START)           = "<h3>";
P(HTML_H3_END)             = "</h3>";

P(TXT_TIME_IS)             = "Time: ";
P(TXT_LAST_FEED_TIME_IS)   = "Last feed: ";
P(HTML_LIGHT_MODE_START)   = "<p><label>Light mode:</label> <strong>";
P(HTML_LIGHT_MODE_END)     = "</strong></p>";
P(TXT_AUTO)                = "Auto";
P(TXT_MANUAL)              = "Manual";
P(TXT_ON)                  = "ON";
P(TXT_OFF)                 = "OFF";
P(HTML_LIGHT_STATUS_START) = "<p><label>Light status:</label> <strong>";
#ifdef USE_SYSTEM_INFO
P(TXT_RAM_1)               = "RAM (byte): ";
P(TXT_RAM_2)               = " free of ";
#endif
P(HTML_LIGHT_SENSOR_START) = "<p><label>Light sensor:</label> <strong>";
P(HTML_FEED_MODE_START)    = "<p><label>Feed mode:</label> <strong>";

void defaultCmd(WebServer &server, WebServer::ConnectionType type, char *, bool)
{
    server.httpSuccess();

    if (type != WebServer::GET) return;

    server.printP(HTML_HEAD);
    server.printP(HTML_FORM);

    server.printP(HTML_LIGHT_MODE_START);
    if (lightSwitchModeAuto) {
        server.printP(TXT_AUTO);
    }
    else {
        server.printP(TXT_MANUAL);
    }
    server.printP(HTML_LIGHT_MODE_END);

    server.printP(HTML_LIGHT_STATUS_START);
    if (digitalRead(LIGHT_SWITCH_PIN) == RELAY_ON) {
        server.printP(TXT_ON);
    }
    else {
        server.printP(TXT_OFF);
    }
    server.printP(HTML_LIGHT_MODE_END);

    server.printP(HTML_LIGHT_SENSOR_START);
    server.print(analogRead(LIGHT_SENSOR_PIN));
    server.printP(HTML_LIGHT_MODE_END);

    server.printP(HTML_FEED_MODE_START);
    if (feedSwitchModeAuto) {
        server.printP(TXT_AUTO);
    }
    else {
        server.printP(TXT_MANUAL);
    }
    server.printP(HTML_LIGHT_MODE_END);
    printLastFeedTime(server);

    server.printP(HTML_LIGHT_CONTROLS);
    server.printP(HTML_FEED_CONTROLS);

    printTime(server);

#ifdef USE_SYSTEM_INFO
    server.printP(HTML_H3_START);
    server.printP(TXT_RAM_1);
    server.print(ramFree());
    server.printP(TXT_RAM_2);
    server.print(ramSize());
    server.printP(HTML_H3_END);
#endif

    server.printP(HTML_TAIL);
}

void lightTurnOff(void) {
    digitalWrite(LIGHT_SWITCH_PIN, RELAY_OFF);
    debug_println("Light is OFF");
    P(TXT_LIGHT_IS_OFF) = "Light is OFF";
    loggerP(TXT_LIGHT_IS_OFF);
}

void lightTurnOn(void) {
    digitalWrite(LIGHT_SWITCH_PIN, RELAY_ON);
    debug_println("Light is ON");
    P(TXT_LIGHT_IS_ON) = "Light is ON";
    loggerP(TXT_LIGHT_IS_ON);
}

void lightOffCmd(WebServer &server, WebServer::ConnectionType type, char *, bool)
{
    lightSwitchModeAuto = false;
    lightTurnOff();
    server.httpSeeOther("/");
}

void lightOnCmd(WebServer &server, WebServer::ConnectionType type, char *, bool)
{
    lightSwitchModeAuto = false;
    lightTurnOn();
    server.httpSeeOther("/");
}

void lightAutoCmd(WebServer &server, WebServer::ConnectionType type, char *, bool)
{
    lightSwitchModeAuto = true;
    debug_println("Light switch mode is AUTO");
    P(TXT_LIGHT_MODE_IS_AUTO) = "Light switch mode is AUTO";
    loggerP(TXT_LIGHT_MODE_IS_AUTO);
    server.httpSeeOther("/");
}

void feedAutoCmd(WebServer &server, WebServer::ConnectionType type, char *, bool)
{
    feedSwitchModeAuto = true;
    debug_println("Feed switch mode is AUTO");
    P(TXT_FEED_MODE_IS_AUTO) = "Feed switch mode is AUTO";
    loggerP(TXT_FEED_MODE_IS_AUTO);
    server.httpSeeOther("/");
}

void feedManualCmd(WebServer &server, WebServer::ConnectionType type, char *, bool)
{
    feedSwitchModeAuto = false;
    debug_println("Feed switch mode is MANUAL");
    P(TXT_FEED_MODE_IS_MANUAL) = "Feed switch mode is MANUAL";
    loggerP(TXT_FEED_MODE_IS_MANUAL);
    server.httpSeeOther("/");
}

void feedStart()
{
    digitalWrite(FEED_SWITCH_PIN, RELAY_ON);
    debug_println("Feeding is started");
    P(TXT_FEEDING_IS_STARTED) = "Feeding is started";
    loggerP(TXT_FEEDING_IS_STARTED);
    delay(FEED_DURATION * 1000);
    digitalWrite(FEED_SWITCH_PIN, RELAY_OFF);
    debug_println("Feeding is finished");
    P(TXT_FEEDING_IS_FINISHED) = "Feeding is finished";
    loggerP(TXT_FEEDING_IS_FINISHED);
    lastFeed = now();
}

void feedStartCmd(WebServer &server, WebServer::ConnectionType type, char *, bool)
{
    feedStart();
    server.httpSeeOther("/");
}

void initWebServer() {
    webserver.setDefaultCommand(&defaultCmd);

    webserver.addCommand("light/on",    &lightOnCmd);
    webserver.addCommand("light/off",   &lightOffCmd);
    webserver.addCommand("light/auto",  &lightAutoCmd);
    webserver.addCommand("feed/start",  &feedStartCmd);
    webserver.addCommand("feed/auto",   &feedAutoCmd);
    webserver.addCommand("feed/manual", &feedManualCmd);

    webserver.begin();
}

void processWebServer() {
    char webserverBuffer[64];
    int  len = 64;

    webserver.processConnection(webserverBuffer, &len);
}

time_t getNtpTime()
{
    if (!isConnected) return 0;

    while (Udp.parsePacket() > 0) ; // discard any previously received packets

    debug_println("Transmit NTP Request");
    P(TXT_SENT_NTP_REQUEST) = "Transmit NTP Request";
    loggerP(TXT_SENT_NTP_REQUEST);

    sendNTPpacket(timeServer);
    uint32_t beginWait = millis();
    while (millis() - beginWait < 1500) {
        int size = Udp.parsePacket();
        if (size >= NTP_PACKET_SIZE) {
            debug_println("Receive NTP Response");
            P(TXT_RECV_NTP_RESPONSE) = "Receive NTP Response";
            loggerP(TXT_RECV_NTP_RESPONSE);

            Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
            unsigned long secsSince1900;
            // convert four bytes starting at location 40 to a long integer
            secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
            secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
            secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
            secsSince1900 |= (unsigned long)packetBuffer[43];
            return secsSince1900 - 2208988800UL + NTP_TIME_ZONE * SECS_PER_HOUR;
        }
    }
    debug_println("No NTP Response :-(");
    P(TXT_NO_NTP_RESPONSE) = "No NTP Response :-(";
    loggerP(TXT_NO_NTP_RESPONSE);

    return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress &address)
{
    // set all bytes in the buffer to 0
    memset(packetBuffer, 0, NTP_PACKET_SIZE);
    // Initialize values needed to form NTP request
    // (see URL above for details on the packets)
    packetBuffer[0] = 0b11100011;   // LI, Version, Mode
    packetBuffer[1] = 0;     // Stratum, or type of clock
    packetBuffer[2] = 6;     // Polling Interval
    packetBuffer[3] = 0xEC;  // Peer Clock Precision
    // 8 bytes of zero for Root Delay & Root Dispersion
    packetBuffer[12]  = 49;
    packetBuffer[13]  = 0x4E;
    packetBuffer[14]  = 49;
    packetBuffer[15]  = 52;
    // all NTP fields have been given values, now
    // you can send a packet requesting a timestamp:
    Udp.beginPacket(address, 123); //NTP requests are to port 123
    Udp.write(packetBuffer, NTP_PACKET_SIZE);
    Udp.endPacket();
}

void logger(char *msg) {
    if (!isConnected) return;
    debug_println("logger");
    Syslog.logger(1, 5, "Fish:", msg);
}

void loggerP(const unsigned char *msg) {
    if (!isConnected) return;
    P(TXT_FISH) = "Fish:";
    debug_println("logger");
    Syslog.loggerP(1, 5, TXT_FISH, msg);
}

void updateTime() {
    unsigned long now;
    time_t        time;

    if (!isConnected) return;

    now = millis();
    if (timeLastUpdate != 0 && now >= timeLastUpdate && (now - timeLastUpdate) < (unsigned long) NTP_SYNC_INTERVAL * 1000) {
        return;
    }

    P(TXT_UPDATE_TIME_FAILED) = "Failed to update time";

    time = getNtpTime();
    if (time == 0) {
        loggerP(TXT_UPDATE_TIME_FAILED);
        return;
    }

    setTime(time);
    timeLastUpdate = now;

    logger(currentTime());

#ifdef DEBUG
    digitalClockDisplay();
#endif
}

void initNTPSync()
{
    Udp.begin(NTP_LOCAL_PORT);
}

void checkConnection()
{
    unsigned long now;

    now = millis();
    if (!isFirstConnection && now >= lastCheck && (now - lastCheck) < (unsigned long) CONNECTION_CHECK_INTERVAL * 1000) {
        return;
    }

    lastCheck = now;

    if (isFirstConnection) {
        debug_println("Initiate connection...");
        if ( Ethernet.begin(mac) == 0 ) {
            debug_println("Failed to configure Ethernet using DHCP");
            return;
        }
        else {
            debug_println("Connected successfully");

#ifdef DEBUG
            Serial.print(F("IP: "));
            Serial.println( Ethernet.localIP() );
#endif

            isFirstConnection = false;

            initNTPSync();
            initWebServer();
            initLogger();
        }
    }
    else {
        debug_println("Maintain connection...");
        switch ( Ethernet.maintain() ) {
            case 0:
                debug_println("Nothing happened");
                break;
            case 1:
                debug_println("Renew failed");
                break;
            case 2:
                debug_println("Rebind fail");
                break;
            case 3:
                debug_println("Rebind success");
                break;
            default:
                debug_println("Unknown state");
        }
    }

    debug_println("Send echo request...");

    ICMPEchoReply echoReply = ping(pingAddr, 4);
    if (echoReply.status == SUCCESS) {
#ifdef DEBUG
        Serial.print(F("Reply["));
        Serial.print(echoReply.data.seq);
        Serial.print(F("] from: "));
        Serial.print(echoReply.addr[0]);
        Serial.print(".");
        Serial.print(echoReply.addr[1]);
        Serial.print(".");
        Serial.print(echoReply.addr[2]);
        Serial.print(".");
        Serial.print(echoReply.addr[3]);
        Serial.print(F(": bytes="));
        Serial.print(REQ_DATASIZE);
        Serial.print(F(" time="));
        Serial.print(millis() - echoReply.data.time);
        Serial.print(F(" TTL="));
        Serial.println(echoReply.ttl);
#endif

        isConnected = true;

        return;
    }

#ifdef DEBUG
    Serial.print(F("Echo request failed: "));
    Serial.println(echoReply.status);
#endif

    isConnected = false;
}

void digitalClockDisplay(){
    // digital clock display of the time
    Serial.print(F("Time: "));
    Serial.print(year());
    Serial.print("/");
    _printDigits(month());
    Serial.print("/");
    _printDigits(day());
    Serial.print(" ");
    _printDigits(hour());
    Serial.print(":");
    _printDigits(minute());
    Serial.print(":");
    _printDigits(second());
    Serial.println();
}

void printTime(WebServer &server) {
    // digital clock display of the time
    server.printP(HTML_H3_START);
    server.printP(TXT_TIME_IS);
    server.print(year());
    server.print("/");
    printDigits(server, month());
    server.print("/");
    printDigits(server, day());
    server.print(" ");
    printDigits(server, hour());
    server.print(":");
    printDigits(server, minute());
    server.print(":");
    printDigits(server, second());
    server.printP(HTML_H3_END);
}

void printLastFeedTime(WebServer &server) {
    // digital clock display of the time
    server.printP(HTML_H3_START);
    server.printP(TXT_LAST_FEED_TIME_IS);
    server.print(year(lastFeed));
    server.print("/");
    printDigits(server, month(lastFeed));
    server.print("/");
    printDigits(server, day(lastFeed));
    server.print(" ");
    printDigits(server, hour(lastFeed));
    server.print(":");
    printDigits(server, minute(lastFeed));
    server.print(":");
    printDigits(server, second(lastFeed));
    server.printP(HTML_H3_END);
}

void printDigits(WebServer &server, int digits){
    // utility for digital clock display: prints preceding colon and leading 0
    if(digits < 10)
        server.print('0');
    server.print(digits);
}

void _printDigits(int digits){
    // utility for digital clock display: prints preceding colon and leading 0
    if(digits < 10)
        Serial.print('0');
    Serial.print(digits);
}

void maintainSwitches() {
    unsigned long now;

    now = millis();
    if (timeLastMaintain != 0 && now >= timeLastMaintain && (now - timeLastMaintain) < (unsigned long) MAINTAIN_INTERVAL * 1000) {
        return;
    }

    timeLastMaintain = now;

    maintainLightSwitch();
    maintainFeedSwitch();
}

void maintainFeedSwitch() {
    int h;
    time_t t;

    debug_println("Maintain feed switch");

    if (!feedSwitchModeAuto) {
        debug_println("Time mode is MANUAL, skip");
        return;
    }

    if (timeStatus() == timeNotSet) {
        debug_println("Time is not set, skip");
        return;
    }

    h = hour();
#ifdef DEBUG
    Serial.print(F("Hour: "));
    Serial.println(h);
#endif

    if (
        (FEED_ON_START <= FEED_ON_FINISH && h >= FEED_ON_START && h < FEED_ON_FINISH) ||
        (FEED_ON_START > FEED_ON_FINISH && (h >= FEED_ON_START || h < FEED_ON_FINISH))
    ) {
        t = now();
        if (year(t) != year(lastFeed) || month(t) != month(lastFeed) || day(t) != day(lastFeed)) {
            feedStart();
        }
    }
}

void maintainLightSwitch() {
    int h;

    debug_println("Maintain light switch");

    if (!lightSwitchModeAuto) {
        debug_println("Time mode is MANUAL, skip");
        return;
    }

    if (timeStatus() == timeNotSet) {
        debug_println("Time is not set, skip");
        return;
    }

    h = hour();
#ifdef DEBUG
    Serial.print(F("Hour: "));
    Serial.println(h);
#endif

    if (
        (LIGHT_ON_START <= LIGHT_ON_FINISH && h >= LIGHT_ON_START && h < LIGHT_ON_FINISH) ||
        (LIGHT_ON_START > LIGHT_ON_FINISH && (h >= LIGHT_ON_START || h < LIGHT_ON_FINISH))
    ) {
        if (digitalRead(LIGHT_SWITCH_PIN) != RELAY_ON) {
            lightTurnOn();
        }
    }
    else {
        if (digitalRead(LIGHT_SWITCH_PIN) != RELAY_OFF) {
            lightTurnOff();
        }
    }
}

void initLightSwitch()
{
    pinMode(LIGHT_SENSOR_PIN, INPUT);

    digitalWrite(LIGHT_SWITCH_PIN, RELAY_OFF);
    pinMode(LIGHT_SWITCH_PIN, OUTPUT);

    lightSwitchModeAuto = true;

    maintainLightSwitch();
}

void initFeedSwitch()
{
    digitalWrite(LIGHT_SWITCH_PIN, RELAY_OFF);
    pinMode(LIGHT_SWITCH_PIN, OUTPUT);
}

void initLogger()
{
    P(TXT_STARTED) = "started";

    Syslog.setLoghost(loggerHost);
    loggerP(TXT_STARTED);
}

void initEthernetShield() {
    // Disable SDcard peripheral
    pinMode(4, OUTPUT);
    digitalWrite(4, HIGH);

    // Enable W5100 peripheral
    pinMode(10, OUTPUT);
    digitalWrite(10, LOW);
}

void setup()
{
    Serial.begin(9600);

    Serial.print("Fish house v");
    Serial.println(fish_house_VERSION);

    debug_println("Initializing...");

    initEthernetShield();
    initLightSwitch();
    initFeedSwitch();
}

void loop ()
{
    checkConnection();
    updateTime();
    processWebServer();
    maintainSwitches();
}
