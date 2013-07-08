#include <Time.h>
#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <ICMPPing.h>
#include <WebServer.h>
#include "Config.h"

#define DEBUG

#define LIGHT_ON_START            8 // hour
#define LIGHT_ON_FINISH           0 // hour
#define LIGHT_SWITCH_PIN          3

#define WEBSERVER_PREFIX          ""
#define WEBSERVER_PORT            80

#define CONNECTION_CHECK_INTERVAL 300 // seconds
#define MAC_ADDRESSS              0x70, 0x5A, 0xB6, 0x01, 0x02, 0x03

#define NTP_SYNC_INTERVAL         3600 // seconds
#define NTP_TIME_SERVER           132, 163, 4, 101
#define NTP_PACKET_SIZE           48
#define NTP_LOCAL_PORT            8888
#define NTP_TIME_ZONE             3

#define PING_SERVER               8, 8, 8, 8

/* Switches */
bool           lightSwitchModeAuto = true;

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

// no-cost stream operator as described at
// http://sundial.org/arduino/?page_id=119
template<class T>
inline Print &operator <<(Print &obj, T arg)
{ obj.print(arg); return obj; }

void defaultCmd(WebServer &server, WebServer::ConnectionType type, char *, bool)
{
    server.httpSuccess();

    if (type != WebServer::GET) return;

    P(htmlHead) =
"<!DOCTYPE html><html><head>"
  "<title>Fish house</title>"
"</head><body>"
    ;

    P(htmlForm) =
"<h1>Fish house!</h1>"
    ;

    P(htmlControls) =
"<p>"
    "<button onclick=\"location.href='/light/on'\">Turn On</button>"
    "<button onclick=\"location.href='/light/off'\">Turn Off</button>"
    "<button onclick=\"location.href='/light/auto'\">Auto</button>"
"</p>"
    ;

    P(htmlTail) =
"</body></html>"
    ;

    server.printP(htmlHead);
    server.printP(htmlForm);

    server << "<p><label>Light mode:</label> <strong>";
    if (lightSwitchModeAuto) {
        server << "Auto";
    }
    else {
        server << "Manual";
    }
    server << "</strong></p>";

    server << "<p><label>Light status:</label> <strong>";
    if (digitalRead(LIGHT_SWITCH_PIN) == RELAY_ON) {
        server << "ON";
    }
    else {
        server << "OFF";
    }
    server << "</strong></p>";

    server.printP(htmlControls);

    printTime(server);

    server.printP(htmlTail);
}

void lightOffCmd(WebServer &server, WebServer::ConnectionType type, char *, bool)
{
    lightSwitchModeAuto = false;
    digitalWrite(LIGHT_SWITCH_PIN, RELAY_OFF);
    debug_println("Light is OFF");
    server.httpSeeOther("/");
}

void lightOnCmd(WebServer &server, WebServer::ConnectionType type, char *, bool)
{
    lightSwitchModeAuto = false;
    digitalWrite(LIGHT_SWITCH_PIN, RELAY_ON);
    debug_println("Light is ON");
    server.httpSeeOther("/");
}

void lightAutoCmd(WebServer &server, WebServer::ConnectionType type, char *, bool)
{
    lightSwitchModeAuto = true;
    debug_println("Light switch mode is AUTO");
    server.httpSeeOther("/");
}

void initWebServer() {
    webserver.setDefaultCommand(&defaultCmd);

    webserver.addCommand("light/on",   &lightOnCmd);
    webserver.addCommand("light/off",  &lightOffCmd);
    webserver.addCommand("light/auto", &lightAutoCmd);

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

    sendNTPpacket(timeServer);
    uint32_t beginWait = millis();
    while (millis() - beginWait < 1500) {
        int size = Udp.parsePacket();
        if (size >= NTP_PACKET_SIZE) {
            debug_println("Receive NTP Response");
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

void updateTime() {
    unsigned long now;
    time_t        time;

    if (!isConnected) return;

    now = millis();
    if (timeLastUpdate != 0 && now >= timeLastUpdate && (now - timeLastUpdate) < (unsigned long) NTP_SYNC_INTERVAL * 1000) {
        return;
    }

    time = getNtpTime();
    if (time == 0) return;

    setTime(time);
    timeLastUpdate = now;

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
            Serial.print("IP: ");
            Serial.println( Ethernet.localIP() );
#endif

            isFirstConnection = false;

            initNTPSync();
            initWebServer();
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
    Serial.print("Time: ");
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
    server.print("<h3>Time: ");
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
    server.print("</h3>");
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
        digitalWrite(LIGHT_SWITCH_PIN, RELAY_ON);
        debug_println("Light is ON");
    }
    else {
        digitalWrite(LIGHT_SWITCH_PIN, RELAY_OFF);
        debug_println("Light is OFF");
    }
}

void initLightSwitch()
{
    digitalWrite(LIGHT_SWITCH_PIN, RELAY_OFF);
    pinMode(LIGHT_SWITCH_PIN, OUTPUT);

    lightSwitchModeAuto = true;

    maintainLightSwitch();
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
}

void loop ()
{
    checkConnection();
    updateTime();
    processWebServer();
    maintainLightSwitch();
}
