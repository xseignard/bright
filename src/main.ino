#include <SPI.h>
#include <Ethernet.h>
#include <Tlc59711.h>
#include <ArtNode.h>
#include <ArtNetFrameExtension.h>
#include <elapsedMillis.h>

///////////////////////////////////////////////////////////////////////////////
// CONFIGURATION
///////////////////////////////////////////////////////////////////////////////
#define CUSTOM_ID 1
///////////////////////////////////////////////////////////////////////////////
// output config
#if CUSTOM_ID == 1
#define NUM_STRIPS 16

#elif CUSTOM_ID == 2
#define NUM_STRIPS 12

#elif CUSTOM_ID == 3
#define NUM_STRIPS 8

#elif CUSTOM_ID == 4
#define NUM_STRIPS 12
#endif

///////////////////////////////////////////////////////////////////////////////
// TOUCH BELOW AT YOUR OWN RISKS!!!!
///////////////////////////////////////////////////////////////////////////////
#define NUM_TLC NUM_STRIPS * 3
#define LEDS_PER_TLC 12
#define NUM_LEDS NUM_TLC * LEDS_PER_TLC
#define DATA_PIN 7
#define CLOCK_PIN 6
const int numArtnetPorts = ceil((NUM_LEDS * 2) / 512);

Tlc59711 tlc(NUM_TLC, CLOCK_PIN, DATA_PIN);

///////////////////////////////////////////////////////////////////////////////
// debug mode: 0 no debug, 1 debug
// when in debug mode, some informations a sent via serial
// debug slows down the artnet decoding!!
#define DEBUG 0
// pass sync to 0 if your software don't send art-sync packets (e.g millumin, max/msp, and so on)
#define SYNC 1

#if CUSTOM_ID == 1
#define NAME "BRIGHT 1"
#define LONG_NAME "BRIGHT 1"

#elif CUSTOM_ID == 2
#define NAME "BRIGHT 2"
#define LONG_NAME "BRIGHT 2"

#elif CUSTOM_ID == 3
#define NAME "BRIGHT 3"
#define LONG_NAME "BRIGHT 3"

#elif CUSTOM_ID == 4
#define NAME "BRIGHT 4"
#define LONG_NAME "BRIGHT 4"
#endif

///////////////////////////////////////////////////////////////////////////////
// artnet and udp conf
#define VERSION_HI 0
#define VERSION_LO 1
ArtConfig config = {
  {0x00, 0x00, 0x00, 0x00, 0x00, CUSTOM_ID},   // MAC address, will be overwritten with the one burnt into the teensy
  {0, 0, 0, 0},                         // IP, will be generated from the mac adress as artnet spec tells us
  {255, 0, 0, 0},                       // subnet mask
  0x1936,                               // UDP port
  false,                                // DHCP
  0, 0,                                 // net (0-127) and subnet (0-15)
  NAME,                                 // short name
  LONG_NAME,                            // long name
  numArtnetPorts,                     // number of ports
  {                                     // port types
    PortTypeDmx | PortTypeOutput,
    PortTypeDmx | PortTypeOutput,
    PortTypeDmx | PortTypeOutput,
    PortTypeDmx | PortTypeOutput
  },
  {0, 0, 0, 0},                         // port input universes (0-15)
  {0, 1, 2, 3},                         // port output universes (0-15)
  VERSION_HI,
  VERSION_LO
};
const int oemCode = 0x0000; // OemUnkown
ArtNodeExtended node;
EthernetUDP udp;
// 576 is the max size of an artnet packet
byte buffer[576];

elapsedMillis fps;
unsigned long interval = 17;
///////////////////////////////////////////////////////////////////////////////
void setup() {
  if (DEBUG) Serial.begin(9600);
  // generating IP
  // TODO: also generate mac address from SAMD21 uuid and derive an IP
  config.ip[0] = 2;
  config.ip[1] = 1;
  config.ip[2] = 0;
  config.ip[3] = CUSTOM_ID;
  if (DEBUG) diagnostic();
  IPAddress gateway(config.ip[0], 0, 0, 1);
  IPAddress subnet(255, 0, 0, 0);
  // start ethernet, udp and artnet
  Ethernet.init(10);
  Ethernet.begin(config.mac, config.ip, gateway, gateway, subnet);
  // Check for Ethernet hardware present
  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    if (DEBUG) Serial.println("Ethernet shield was not found. Sorry, can't run without hardware. :(");
    while (true) {
      delay(1); // do nothing, no point running without Ethernet hardware
    }
  }
  if (Ethernet.linkStatus() == LinkOFF) {
    if (DEBUG) Serial.println("Ethernet cable is not connected.");
  }
  udp.begin(config.udpPort);
  node = ArtNodeExtended(config, sizeof(buffer), buffer);
  // init leds
  tlc.beginSlow();
  // proceed a blink to check if everything is ok
  blink();
}

///////////////////////////////////////////////////////////////////////////////
void loop() {
  while (udp.parsePacket()) {
    // read the header to make sure it's Art-Net
    unsigned int n = udp.read(buffer, sizeof(ArtHeader));
    if (n >= sizeof(ArtHeader)) {
      ArtHeader* header = (ArtHeader*) buffer;
      // check packet ID, if "Art-Net", we are receiving artnet!!
      if (memcmp(header->ID, "Art-Net", 8) == 0) {
        // read the rest of the packet
        udp.read(buffer + sizeof(ArtHeader), udp.available());
        // check the op-code, and act accordingly
        switch (header->OpCode) {
          // for a poll message, send back the node capabilities
          case OpPoll: {
            if (DEBUG) Serial.println("POLL");
            node.createPollReply();
            udp.beginPacket(node.broadcastIP(), config.udpPort);
            udp.write(buffer, sizeof(ArtPollReply));
            udp.endPacket();
            break;
          }

          // for a DMX message, decode and set the leds to their new values
          case OpDmx: {
            if (DEBUG) Serial.println("DMX");
            ArtDmx* dmx = (ArtDmx*) buffer;
            int port = node.getAddress(dmx->SubUni, dmx->Net) - node.getStartAddress();
            if (port >= 0 && port < config.numPorts) {
              byte* data = (byte*) dmx->Data;
              int max;
              if (CUSTOM_ID == 1) {
                if (port == 2) max = 72;
                else max = 252;
                // max = 252;
              }
              else if (CUSTOM_ID == 2) {
                if (port == 1) max = 180;
                else max = 252;
              }
              else if (CUSTOM_ID == 3) {
                if (port == 1) max = 36;
                else max = 252;
              }
              else {
                if (port == 1) max = 180;
                else max = 252;
              }
              for (int i = 0; i < max; i++) {
                uint16_t value = data[i * 2] * 256 + data[i * 2 + 1];
                tlc.setChannel(i + port * 252, value);
              }
            }

            if (fps >= interval) {
              tlc.write();
              fps = 0;
            }
            // if (!SYNC) {
            //   tlc.write();
            // }
            break;
          }

          // for a sync message, show the updated state of the pixels
          case OpSync: {
            if (DEBUG) Serial.println("SYNC");
            // if (SYNC) {
            //   tlc.write();
            // }
            break;
          }

          // default, do nothing since the packet hasn't been recognized as a artnet packet
          default:
            break;
        }
      }
      // check packet ID, if "Art-Ext", we are receiving artnet extended!!
      // and if OpCode is OpPoll then we can send an extended poll reply
      // that will let us inform about the 16 available universes
      else if (
        memcmp(header->ID, "Art-Ext", 8) == 0 &&
        header->OpCode == (OpPoll | 0x0001)
      ) {
        if (DEBUG) Serial.println("POLL EXTENDED");
        // read the rest of the packet
        udp.read(buffer + sizeof(ArtHeader), udp.available());
        node.createExtendedPollReply();
        udp.beginPacket(node.broadcastIP(), config.udpPort);
        udp.write(buffer, sizeof(ArtPollReply));
        udp.endPacket();
      }
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
// blink test
void blink() {
  tlc.setBrightness(50, 50, 50);
  tlc.setChannel(0, 65535);
  tlc.write();
  delay(2000);
  tlc.setTmgrst();
  tlc.setChannel(0, 0);
  tlc.write();
  delay(100);
}

///////////////////////////////////////////////////////////////////////////////
// some debug diagnostic
void diagnostic() {
  delay(5000);
  Serial.println(F_CPU);
  // Serial.println(F_BUS);
  Serial.println("MAC address");
  // prints the MAC address
  for (int i = 0; i < 6; ++i) {
    Serial.print(config.mac[i]);
    if (i < 5) Serial.print(":");
  }
  Serial.println("");
  // prints the IP
  Serial.println("IP address");
  for (int i = 0; i < 4; ++i) {
    Serial.print(config.ip[i]);
    if (i < 3) {
      Serial.print(".");
    }
  }
  Serial.println("");
  // prints the number of ports
  Serial.print("Number of ports: ");
  Serial.println(config.numPorts);
}
