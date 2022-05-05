#include "iridium.h"

#include <Arduino.h>
#include <vector>
#include <string>

#pragma region

#define GEO_C request("AT-MSGEO")
// Current geolocation, xyz cartesian
// return format: <x>, <y>, <z>, <time_stamp>
// time_stamp uses same 32 bit format as MSSTM

#define REGISTER request("AT+SBDREG")
#define REGISTER(x) request(strcat("AT+SBDREG"), (char*)x)
// Performs a manual registration, consisting of attach and location update. No MO/MT messages transferred
// Optional param location

#define MODEL reqeust("AT+CGMM")
#define PHONE_REV request("AT+CGMR")
#define IMEI request("AT+CSGN")

#define NETWORK_TIME request("AT-MSSTM")
// System time, GMT, retrieved from satellite network (used as a network check)
// returns a 32 bit integer formatted in hex, with no leading zeros. Counts number of 90 millisecond intervals
// that have elapsed since the epoch current epoch is May 11, 2014, at 14:23:55, and will change again around
// 2026

#define SHUTDOWN request("AT*F", 1)

#define RSSI request("AT+CSQ", 10)
// Returns strength of satellite connection, may take up to
// ten seconds if iridium is in satellite handoff

#define LAST_RSSI request("AT+CSQF")
// Returns last known signal strength, immediately

#define RING_ALERT request("AT+SBDMTA")
#define RING_ALERT(x) request(strcat("AT+SBDMTA", (char*)x))
// Enable or disable ring indications for SBD Ring Alerts. When ring indication is enabled, ISU asserts RI
// line and issues the unsolicited result code SBDRING when an SBD ring alert is received Ring alerts can only
// be sent after the unit is registered :optional param b: set 1/0 enable/disable

#define BAT_CHECK request("AT+CBC")
// doesn't seem relevant to us?

#define SOFT_RST request("ATZn", 1)
// Resets settings without power cycle

#define SBD_WT(x) request(strcat("AT+SBDWT=", (char*)x))
// Load message into mobile originated buffer. SBDWT uses text, SBDWB uses binary. 

#define SBD_WB(x) request(strcat("AT+SBDWB=", (char*)x))
// For SBDWB, input message byte length
// Once "READY" is read in, write each byte, then the two least significant checksum bytes, MSB first
// Final response: 0: success, 1: timeout (insufficient number of bytes transferred in 60 seconds)
// 2: Checksum does not match calculated checksum, 3: message length too long or short
// Keep messages 340 bytes or shorter

#define SBD_RT request("AT+SBDRT")
#define SBD_RB request("AT+SBDRB")
// Read message from mobile terminated buffer. SBDRT uses text, SBDRB uses binary. Only one message is
// contained in buffer at a time

#define SBD_STATUS request("AT+SBDS")
// Returns state of mobile originated and mobile terminated buffers
// SBDS return format: <MO flag>, <MOMSN>, <MT flag>, <MTMSN>
// beamcommunications 101-102

#define SBD_STATUS_EX request("AT+SBDSX")
// SBDSX return format: <MO flag>, <MOMSN>, <MT Flag>, <MTMSN>, <RA flag>, <msg waiting>
// beamcommunications 103
// MO flag: (1/0) whether message in mobile originated buffer
// MOMSN: sequence number that will be used in the next mobile originated SBD session
// MT flag: (1/0) whether message in mobile terminated buffer
// MTMSN: sequence number in the next mobile terminated SBD session, -1 if nothing in the MT buffer
// RA flag: (1/0) whether an SBD ring alert has been received and needs to be answered
// msg waiting: how many SBD mobile terminated messages are queued at the gateway for collection by ISU

#define SBD_TIMEOUT request("AT+SBDST")
#define SBD_TIMEOUT(x) request(strcat("AT+SBDST=", (char*)x))
// Reads or sets session timeout settings, after which time ISU will stop trying to transmit/receive to GSS,
// in seconds. 0 means infinite timeout

#define SBD_TRANSFER_MOMT request("AT+SBDTC")
// Transfers contents of mobile originated buffer to mobile terminated buffer, to test reading and writing to
// ISU without initiating SBD sessions with GSS/ESS returns response of the form "SBDTC: Outbound SBD copied
// to Inbound SBD: size = <size>" followed by "OK", where size is message length in bytes
// beamcommunications 104

#define SBD_INITIATE request("AT+SBDI", 60)
// Transmits contents of mobile originated buffer to GSS, transfer oldest message in GSS queuefrom GSS to ISU
// beamcommunications 94-95

#define SBD_INITIATE_EX request("AT+SBDIX", 60)
#define SBD_INITIATE_EX(x) request(strcat("AT+SBDIX=", (char*)x))
// returns: <MO status>,<MOMSN>,<MT status>,<MTMSN>,<MT length>,<MT queued>
// MO status: 0: no message to send, 1: successful send, 2: error while sending
// MOMSN: sequence number for next MO transmission
// MT status: 0: no message to receive, 1: successful receive, 2: error while receiving
// MTMSN: sequence number for next MT receive
// MT length: length in bytes of received message
// MT queued: number of MT messages in GSS waiting to be transferred to ISU
// beamcommunications 95-96

#define SBD_CLR(x) request(strcat("AT+SBDD", (char*)x))
// Clear one or both buffers. BUFFERS MUST BE CLEARED AFTER ANY MESSAGING ACTIVITY
// param type: buffers to clear. 0 = mobile originated, 1 = mobile terminated, 2 = both
// returns bool if buffer wasnt cleared successfully (1 = error, 0 = successful)

#pragma endregion Request Macros

namespace Iridium
{
    const char* PORT = "/dev/serial0";

    void Init()
    {
        Serial.begin(19200); // connect serial
        while(!Serial.available()) delay(0.5);
    }

    void terminate()
    {
        check_buffer();
        SHUTDOWN;
        // TODO: close serial?
    }

    int check_signal_active()
    {
        // Passivley check signal strength, for transmit/recieve timing

        std::string raw(RSSI);

        auto pos = raw.find("CSQ:");
        if(pos == std::string::npos) return 0;

        return (int)(raw.substr(pos + 4, pos + 5).c_str() - '0'); // string to int conversion
    }

    int check_signal_passive()
    {
        // Passivley check signal strength, for transmit/recieve timing

        std::string raw(LAST_RSSI);

        auto pos = raw.find("CSQF:");
        if(pos == std::string::npos) return 0;

        return (int)(raw.substr(pos + 5, pos + 6).c_str() - '0'); // string to int conversion
    }

    void check_buffer() // Checks buffer for existing messages
    {
        // Checks buffer for existing messages

        const char* stat = SBD_STATUS;
        // ls = self.process(stat, "SBDS").splot(",")
        char* ls = strtok((char*)process(stat, "SBDS"), ",");
        ls = strtok(NULL, ",");
        ls = strtok(NULL, ","); // get third occurance

        if((int)ls == 1)
        {
            // TODO: do something with buffer
        }

        std::string temp(SBD_CLR(2));
        if(temp.find("0\r\n\r\nOK") == std::string::npos)
        {
            throw "Iridium Error: Error clearing buffer";
        }
    }

    const char* process(const char* data, const char* cmd)
    {
        // really jank for something that was one line in python

        char* temp1;
        temp1 = strtok((char*)data, strcat((char*)cmd,":"));
        temp1 = strtok(NULL, strcat((char*)cmd,":")); // get second occurance

        char* temp2;
        temp2 = strtok((char*)temp1, "\r\nOK");

        // jank trimming
        std::string temp3(temp2);
        auto ltrail = temp3.find_first_not_of(" \n\r\t\f\v");
        if(ltrail == std::string::npos) ltrail = 0;
        auto rtrail = temp3.find_first_not_of(" \n\r\t\f\v");
        if(rtrail == std::string::npos) rtrail = temp3.length();

        return temp3.substr(ltrail, rtrail+1).c_str();
    }

    const char* request(const char* command, int timeout=0.5)
    {
        // Requests information from Iridium and returns unprocessed response
        
        Serial.flush();
        write(command);

        std::string result("");
        int time = millis();

        while(time - millis() < timeout)
        {
            delay(0.1);
            result += read();

           if(result.find("ERROR") != std::string::npos)
           {
               return (result.substr(2) + "ERROR" + "\n").c_str(); // formatted so that process() can still decode properly
           }
           if(result.find("OK") != std::string::npos)
           {
               return result.c_str();
           }
        }

        throw "Iridium Error: Incomplete response";
    }

    bool write(const char* command)
    {
        // Write a command to the serial port

        Serial.write((strcat((char*)command, "\r\n"))); // might or might not work as UTF-8 encoding
        return true;
    }

    const char* read() // private read method
    {
        // Reads in as many available bytes as it can if timeout permits

        std::vector<byte> output;

        for(int i=0; i<50; i++)
        {
            byte next_byte;
            try
            {
                next_byte = Serial.readBytes((char*)PORT, 1);
            }
            catch(...)
            {
                break;
            }

            if(next_byte = byte()) break;

            output.push_back(next_byte);
        }

        const char* toReturn;
        auto it = output.begin();
        while(it != output.end())
        {
            toReturn = strcat((char*)toReturn, (char*)*it); // TODO: fix. probably doesn't work as decoding UTF-8 from bytes
            it++;
        }

        return toReturn;
    }

    // temp
    const char* Read() // public read method
    {
        return "";
    }

    // temp
    void Transmit() // public transmit method
    {
        return;
    }
}