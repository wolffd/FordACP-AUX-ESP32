/*
 * Original code for Yampp -  Andrew Hammond <andyh@mindcode.co.uk>
 *
 * For Arduino Mega 2560 - Krzysztof Pintscher <niou.ns@gmail.com>
 * Full schematics and connection by Krzysztof Pintscher
 * please keep our credits in header :)
 *
 * Integration with AT Commands for OVC3868 - Dale Thomas <dalethomas@me.com>
 *
 ******************************************************************
 * Revisions:
 *
 * 2013-12-17 version 0.9 - public relase
 * 2014-11-30 version 1.0 - Annotations and integration with AT file -DT
 * 2017-05-07 version 1.1 - Removed Bluetooth functionality and editted ports for use with Arduino UNO. -Anson Liu (ansonliu.com)
 * 2026-02-22 version 1.1 - Added handshake failsave and ported for ESP32 (Arduino Target:ESP32-WROOM-DA) using ESPSoftwareSerial - Daniel Wolff (info@juppiemusic.com), github.com/wolffd
 *
 */

#include <SoftwareSerial.h>
#include <Adafruit_NeoPixel.h>

typedef unsigned char u08;
typedef unsigned short u16;
typedef unsigned long u32;

// tested if this helps diconnecting properly but doesnt automagically reconnect on start
#define FEATURE_END_BLUETOOTH_ON_CD_DESELECT false

// TODO: enabling this seems to crash the ESP32 or at least stop the handshake detection from working
//       try to just send the time through our ACP state machine, maybe after an ack or in an empty message block
#define FEATURE_CD_TIME_UPDATE true

// Restart acp if we seem to be stuck in an endless handshake
#define RESET_ON_HANDSHAKE_RETRIES true

// LED DEBUGGING
#define PIN_WS2812B 7 // Arduino-Pin, der mit WS2812B verbunden ist
#define NUM_PIXELS 1  // Die Anzahl der LEDs (Pixel) am WS2812B
Adafruit_NeoPixel WS2812B(NUM_PIXELS, PIN_WS2812B, NEO_GRB);

uint32_t red = WS2812B.Color(255, 0, 0);
uint32_t green = WS2812B.Color(0, 255, 0);
uint32_t blue = WS2812B.Color(0, 0, 255);
uint32_t white = WS2812B.Color(255, 255, 255);
uint32_t magenta = WS2812B.Color(255, 0, 255);
uint32_t cyan = WS2812B.Color(0, 255, 255);
uint32_t yellow = WS2812B.Color(255, 255, 0);

// ACP UART Messages and settings
#define ACP_HANDSHAKE_WATCHDOG_ENTRY_MULTIPLIER 3
#define ACP_HANDSHAKE_WATCHDOG_MAX_TRESHOLD 30

#define ACP_UART_BAUD_RATE 9600
#define ACP_LISTEN 0
#define ACP_SENDACK 1
#define ACP_MSGREADY 2
#define ACP_WAITACK 3
#define ACP_SENDING 4

#define ACK_MESSAGE 0x06
#define ACK_ANY_MESSAGE false // true by default, but expects only us on the bus

#define PRIORTY_MEDIUM 0x71

#define TARGET_ADDRESS_9A 0x9a
#define TARGET_ADDRESS_9B 0x9b
#define SOURCE_ADDRESS_CD_CHANGER 0x82
#define SOURCE_ADDRESS_HEADUNIT 0x80

#define HANDSHAKE_1 0xE0
#define HANDSHAKE_2 0xFC
#define HANDSHAKE_3 0xC8

#define DISK_STATUS_REQUEST 0xFF
#define DISK_STATUS_REQUEST_RESPONSE_DISK_OK 0x00
#define DISK_STATUS_REQUEST_RESPONSE_NO_DISK 0x01
#define DISK_STATUS_REQUEST_RESPONSE_NO_DISKS 0x02

#define NUMBER_BUTTON_PRESSED 0xC2
#define NUMBER_BUTTON_PRESSED_1 0x01
#define NUMBER_BUTTON_PRESSED_2 0x02
#define NUMBER_BUTTON_PRESSED_3 0x03
#define NUMBER_BUTTON_PRESSED_4 0x04
#define NUMBER_BUTTON_PRESSED_5 0x05
#define NUMBER_BUTTON_PRESSED_6 0x06

#define DISK_MANAGEMENT 0xD0
#define DISK_MANAGEMENT_CHANGE_DISK 0x9a
// DISK_MANAGEMENT_REQUEST_CURRENT_DISK any other value != 0x9a

#define CONTROL_COMMAND 0xC1
#define CONTROL_COMMAND_CD_DESELECT 0x00
#define CONTROL_COMMAND_CD_SELECT 0x40
#define CONTROL_COMMAND_SCAN 0x41
#define CONTROL_COMMAND_FF 0x42
#define CONTROL_COMMAND_REWIND 0x44
#define CONTROL_COMMAND_SHUFFLE 0x50
#define CONTROL_COMMAND_COMP 0x60

#define NEXT_TRACK 0xC3
#define PREV_TRACK 0x43

// ACP ESPSoftwareSerial setup
const uint8_t RX_PIN = 13;    // 5; on ESP32-C3
const uint8_t TX_PIN = 12;    // 4; on ESP32-C3
const uint8_t switchPin = 14; // 6; on ESP32-C3  // RE/DE control on MAX485
constexpr bool invert = false;
EspSoftwareSerial::UART acpSerial;

// generic variables
uint8_t acp_rx[12];
uint8_t acp_tx[12];
uint8_t acp_rxindex;
uint8_t acp_txindex;
u08 acp_status; // hack for debuggging
u08 acp_txsize;
u08 acp_timeout;
u08 acp_checksum;
u08 acp_handshake_retries;
u08 acp_mode;
u08 acp_last_command_rx;
uint16_t acp_ltimeout;


/*
 * ACP 9-bit serial communication setup
 https://github.com/plerup/espsoftwareserial/tree/main
 To allow flexible 9-bit and data/addressing protocols,
 the additional parity modes MARK and SPACE are also available.
 Furthermore, the parity mode can be individually set in each call to write().
 This allows a simple implementation of protocols where the parity bit is used
 to distinguish between data and addresses/commands ("9-bit" protocols).
 First set up EspSoftwareSerial::UART with parity mode SPACE, e.g. acpSerialIAL_8S1.
 This will add a parity bit to every byte sent,
 setting it to logical zero (SPACE parity).

 To detect incoming bytes with the parity bit set (MARK parity),
  use the readParity() function. To send a byte with the parity bit set,
  just add MARK as the second argument when writing, e.g. write(ch, SWSERIAL_PARITY_MARK).
 */
void acp_setup()
{

  randomSeed(analogRead(0)); // (for ACP Reset Pause)

  acpSerial.begin(ACP_UART_BAUD_RATE, EspSoftwareSerial::SWSERIAL_8S1, RX_PIN, TX_PIN, invert);
  acpSerial.setTransmitEnablePin(switchPin); // Auto-control RE/DE pin

  if (DEBUG_LED)
  {
    //  set all leds on WS2812B rgb led strip on with DIN on pin 7  to red PIN_WS2812B
    WS2812B.begin();
    WS2812B.fill(red);
    WS2812B.setBrightness(100);
    WS2812B.show();
  }


  if (DEBUG_SERIAL)
  {
    Serial.begin(115200); 
    delay(500);
    Serial.println("ACP Module booting");
  }

  if (!acpSerial)
  { // If the object did not initialize, then its configuration is invalid
    if (DEBUG_SERIAL)
    {
      Serial.println("Invalid EspSoftwareSerial pin configuration, check config");
    }
    while (1)
    { // Don't continue with invalid configuration
      delay(500);
    }
  }

  delay(500);

  if (DEBUG_LED)
  {
    WS2812B.setBrightness(0);
  }
}

void acp_uart_handler()
{
  // Read incoming bytes and process
  uint8_t ndata = acpSerial.available();

  // if (DEBUG_SERIAL) {printHex(&ndata, "ACP UART Handler Looks for Packets: ", 1);}

  while (acpSerial.available())
  {

    //  if (DEBUG_SERIAL) {printHex(&acp_status, "ACP handler with  status:", 1); } // non-finished packets

    /*
    ACP docs:
    Access Control Semaphores
    Start of Message = Minimum Idle Interval (14 BTI)
    End of Data = 9th data bit equal to "1"

    SoftwareSerial detects this (eod) in the parity bit
    */

    uint8_t ch = acpSerial.read();
    auto eod = acpSerial.readParity(); // true if MARK parity (9th bit set)
    // if (DEBUG_SERIAL) { printHex(acp_rx, "RX: ", acp_rxindex); } // non-finished packets
    // if (DEBUG_SERIAL) { printHex(&eod, "EOD: ", 1); } // non-finished packets

    if (acp_status != ACP_LISTEN)
      return; // ignore incoming msgs if busy processing

    if (!eod)
      acp_checksum += ch;
    acp_rx[acp_rxindex++] = ch;

    if (acp_rxindex > 12)
    {
      acp_reset();
    }
    else if (eod)
    { // else if (eod)  {

      // Message complete (last byte is checksum)
      if (
          (acp_checksum == ch) &&
          (ACK_ANY_MESSAGE || (acp_rx[1] == TARGET_ADDRESS_9A || acp_rx[1] == TARGET_ADDRESS_9B)))
      {
        if (DEBUG_SERIAL)
        {
          printHex(acp_rx, "RX OK: ", acp_rxindex);
        }
        if (DEBUG_LED)
        {
          WS2812B.fill(green);
          WS2812B.setBrightness(100);
          WS2812B.show();
        }
        acp_status = ACP_SENDACK;
        acp_handler();
      }
      else
      {
        if (DEBUG_SERIAL && (acp_checksum != ch))
        {
          printHex(acp_rx, "RX NOK: ", acp_rxindex);
        }
        if (DEBUG_SERIAL && (acp_checksum == ch))
        {
          printHex(acp_rx, "RX FOREIGN: ", acp_rxindex);
        }
        if (DEBUG_LED && (acp_checksum != ch))
        {
          WS2812B.fill(red);
          WS2812B.setBrightness(100);
          WS2812B.show();
        }
        acp_reset(); // Abort message
      }
      acp_checksum = 0;
    }
  }
}

// Hex Serial Print
void printHex(uint8_t *byte, const char *msg, uint8_t nBytes)
{
  Serial.print(msg);

  for (uint8_t i = 0; i < nBytes; i++)
  {
    Serial.print("0x");
    Serial.print(byte[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
}

void acp_sendack(void)
{
  if (DEBUG_SERIAL)
  {
    Serial.println("ACK");
  }
  if (DEBUG_LED)
  {
    WS2812B.fill(green);
    WS2812B.setBrightness(100);
    WS2812B.show();
  }

  acpSerial.write(ACK_MESSAGE);
  acpSerial.flush();
}

void acp_reset(void)
{
  acp_timeout = 0;
  acp_checksum = 0;
  acp_rxindex = 0;
  acp_txindex = 0;
  acp_txsize = 0;
  acp_status = ACP_LISTEN;
}

void acp_sendmsg(void)
{

  if (DEBUG_LED)
  {
    WS2812B.fill(blue);
    WS2812B.setBrightness(100); // ein Wert von 0 bis 255
    WS2812B.show();
  }

  u08 i;
  delayMicroseconds(104 * 17); // Start of Message = Minimum Idle Interval (14 BTI)

  for (i = 0; i <= acp_txsize; i++)
  {

    // Send all bytes, the last byte needs parity bits (bit9) set to 1 (high)
    // ESPSoftwareSerial: To send a byte with the parity bit set, just add MARK as the second argument when writing, e.g. write(ch, SWSERIAL_PARITY_MARK).
    if (i == acp_txsize)
    {
      acpSerial.write(acp_tx[i], EspSoftwareSerial::PARITY_MARK);
    }
    else
    {
      acpSerial.write(acp_tx[i]);
    }
  }

  acpSerial.flush();
  acp_status = ACP_WAITACK; // wait for ACK

  if (DEBUG_SERIAL)
  {
    printHex(acp_tx, "TX: ", acp_txsize + 1);
  }
}

void acp_handler()
{
  if (acp_status == ACP_LISTEN)
  {
    if (++acp_ltimeout == 1000)
      acp_ltimeout = 0;
  }
  if (acp_status == ACP_SENDACK)
  {
    acp_sendack();
    acp_status = ACP_MSGREADY;
  }
  else if (acp_status == ACP_WAITACK)
    acp_reset(); // HU does not seem to return an ACK
  if (acp_status == ACP_MSGREADY)
  {
    acp_status = ACP_SENDING;
    acp_process(); // Process message
    // if (DEBUG_SERIAL) {Serial.println("ACP Processed");}
  }
  else if (acp_status == ACP_SENDING)
  {
    acp_timeout++;
    if (!acp_timeout)
    {
      acp_reset();
    }
  }
}

// This is a workaround that allows to repeat handshake with RDS5000
// we just repeat the answer of the first handshake with a delay
// alternatively, resetting the ESP has a similar effect.
void fix_ACP_communication()
{
  // ESP.restart();  // this works "very reliably" (3rd time usually)

  // send ack and first handshake, maybe the original ack was not received
  acp_reset();
  // acpSerial.stopListening();
  delay(random(80, 120)); // ca 100ms
  acp_sendack();
  acp_tx[0] = PRIORTY_MEDIUM; // 0x71 medium/low priority
  acp_tx[1] = TARGET_ADDRESS_9B;
  acp_tx[2] = SOURCE_ADDRESS_CD_CHANGER; // 0x82 message from "CD Changer" (bluetooth interface)
  acp_tx[3] = HANDSHAKE_1;               // Handshake 1
  acp_tx[4] = 0x04;                      // reply with 0x04
  acp_chksum_send(5);
  // acpSerial.listen();
}

void acp_handshake_watchdog_reset()
{
  acp_handshake_retries = 0;
}

void acp_handshake_watchdog(uint8_t last_command_rx)
{
  if (last_command_rx == HANDSHAKE_1 || last_command_rx == HANDSHAKE_2 || last_command_rx == HANDSHAKE_3)
  {
    acp_handshake_retries += ACP_HANDSHAKE_WATCHDOG_ENTRY_MULTIPLIER;
    if (DEBUG_SERIAL)
    {
      printHex(&acp_handshake_retries, "Handshake score: ", 1);
    }
  }
  else if (acp_handshake_retries > 0)
  {
    acp_handshake_retries -= 1;
  }
  if (acp_handshake_retries >= ACP_HANDSHAKE_WATCHDOG_MAX_TRESHOLD && RESET_ON_HANDSHAKE_RETRIES)
  {
    if (DEBUG_SERIAL)
    {
      Serial.println("Too many handshake retries, resetting ACP");
    }
    fix_ACP_communication();
    acp_handshake_watchdog_reset();
  }
}

void acp_process(void)
{

  acp_timeout = 0;
  acp_tx[0] = PRIORTY_MEDIUM; // 0x71 medium/low priority
  acp_tx[1] = TARGET_ADDRESS_9B;
  acp_tx[2] = SOURCE_ADDRESS_CD_CHANGER; // 0x82 message from "CD Changer" (bluetooth interface)
  acp_tx[3] = acp_rx[3];                 // answer to specific command

  acp_last_command_rx = acp_rx[3]; // for watchdog

  if (acp_rx[2] == SOURCE_ADDRESS_HEADUNIT) //  0x80 Message from Head Unit
  {
    if (acp_rx[1] == TARGET_ADDRESS_9A || acp_rx[1] == TARGET_ADDRESS_9B) // CD Changer functional address
    {
      switch (acp_rx[3])
      {
      case HANDSHAKE_3:        // Handshake 3 - CD Changer now recognized
        acp_tx[4] = acp_rx[4]; // need to reply with same byte
        acp_chksum_send(5);
        break;

      case HANDSHAKE_2:        // Handshake 2
        acp_tx[4] = acp_rx[4]; // need to reply with same byte
        acp_chksum_send(5);
        break;

      case HANDSHAKE_1:   // Handshake 1
        acp_tx[4] = 0x04; // reply with 0x04
        acp_chksum_send(5);
        break;

      case DISK_STATUS_REQUEST: // Current disc status request - responses are
        acp_tx[4] = DISK_STATUS_REQUEST_RESPONSE_DISK_OK;
        // 00 - Disc OK
        // 01 - No disc in current slot
        // 02 - No discs
        // 03 - Check disc in current slot
        // 04 - Check all discs!
        acp_chksum_send(5);
        if (FEATURE_CD_TIME_UPDATE)
        {
          PlayTime();
        }
        break;

      case 0x42: // [<- Tune]
        /*
      switch(callStatus){
      case false:
      callStatus = true;
      at_process(8);     // Answer Call
      break;
      case true:
      callStatus = false;
      at_process(10);    // End Call
      break;
      }
      */
        acp_chksum_send(5);

        break;

      case NUMBER_BUTTON_PRESSED:
        // radio number buttons?

        acp_tx[4] = 0x00;
        switch (acp_rx[4])
        {
        case 1:
          lastCommand = playPause;
          break;
        case 2:
          lastCommand = activateSiri;
          break;
        case 3:
          lastCommand = prevTrack;
          break;
        case 4:
          lastCommand = nextTrack;
          break;
        case 5:
          switch (rewindState)
          {
          case false:
            rewindState = true;
            // Start Rewind
            lastCommand = rewindTrack;
            break;
          case true:
            rewindState = false;
            // Stop Fast Forward / Rewind
            lastCommand = cancelCommand;
            break;
          }
          break;
        case 6:
          switch (ffState)
          {
          case false:
            ffState = true;
            // Start Fast Forward
            lastCommand = fastForwardTrack;
            break;
          case true:
            ffState = false;
            // Stop Fast Forward / Rewind
            lastCommand = cancelCommand;
            break;
          }
          break;
        }
        acp_chksum_send(5);
        break;
      case DISK_MANAGEMENT:
        if (acp_rx[1] == DISK_MANAGEMENT_CHANGE_DISK) // Command to change disc
        {
          // u08 disc = plist_change(acp_rx[4]);
          acp_tx[4] = 1 & 0x7F;
          acp_chksum_send(5);
          // if(disc & 0x80)
          // acp_nodisc();
          break;
        }
        else // Request current disc
        {
          // acp_tx[4] = get_disc();
        }
        if (acp_rx[3] != 0xD0)
          acp_chksum_send(5);
        break;

      case CONTROL_COMMAND: // Command
        acp_mode = acp_rx[4];

        switch (acp_mode)
        {
        case CONTROL_COMMAND_CD_DESELECT: // Switch from CD ie. FM, AM, Tape or power button.
          // there is no opposite to this. turning power back on only sends "play"
          // disconnect audio source
          if (FEATURE_END_BLUETOOTH_ON_CD_DESELECT)
          {
            avrc_shutdown();
          }
          else
          {
            playingState = true;
            lastCommand = playPause;
          }
          break;
        case CONTROL_COMMAND_CD_SELECT: // Switch to CD ie. Audio On (vehichle is turned on) or CD button
          if (lastCommand != noCommand)
          { // unfortunately it's always nocommand
            if (FEATURE_END_BLUETOOTH_ON_CD_DESELECT)
            {
              avrc_resume();
            }
            else
            {
              playingState = false;
              lastCommand = playPause;
            }
          }
          break;
        case CONTROL_COMMAND_SCAN: // Scan Button
          // Mute/Unmute Mic
          break;
        case CONTROL_COMMAND_FF: // FF Button
          switch (ffState)
          {
          case false:
            ffState = true;
            // Start Fast Forward
            lastCommand = fastForwardTrack;
            break;
          case true:
            ffState = false;
            // Stop Fast Forward / Rewind
            lastCommand = cancelCommand;
            break;
          }
          break;
        case CONTROL_COMMAND_REWIND: // Rew Button
          switch (rewindState)
          {
          case false:
            rewindState = true;
            // Start Rewind
            lastCommand = rewindTrack;
            break;
          case true:
            rewindState = false;
            // Stop Fast Forward / Rewind
            lastCommand = cancelCommand;
            break;
          }
          break;
        case CONTROL_COMMAND_SHUFFLE: // Shuffle Button
          lastCommand = activateSiri;
          break;
        case CONTROL_COMMAND_COMP: // Comp Button
          // Play/Pause Music
          lastCommand = playPause;
          break;
        }

        acp_mode = (acp_mode & 0x40);
        acp_tx[4] = acp_mode;
        acp_chksum_send(5);
        break;

      case NEXT_TRACK:
        change_track(true); // Next Track
        acp_tx[4] = BCD(currentTrack);
        acp_chksum_send(5);

        lastCommand = nextTrack;
        break;

      case PREV_TRACK:
        change_track(false); // Prev Track
        acp_tx[4] = BCD(currentTrack);
        acp_chksum_send(5);

        lastCommand = prevTrack;
        break;

      default:
        acp_reset(); // unknown - ignore
      }
      if (RESET_ON_HANDSHAKE_RETRIES)
        acp_handshake_watchdog(acp_last_command_rx);
    }
    else
      acp_reset(); // Ignore all other acp messages
  }
}

void acp_chksum_send(unsigned char buffercount)
{
  u08 i;
  u08 checksum = 0;
  for (i = 0; i < buffercount; i++)
    checksum += acp_tx[i];
  acp_txsize = buffercount;
  acp_tx[acp_txsize] = checksum;
  acp_sendmsg();
}

void acp_loop()
{
  acp_uart_handler();
  acp_handler();
}
