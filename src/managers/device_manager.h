#pragma once
#include "../src/libraries/eps/eps.h"

namespace DEVICES
{
    // temp
    enum Device: int
    {
        IRIDIUM = 1, // device 0, pdm 1
        IMU // device 1, pdm 2
    };

    bool* DeviceStatus = malloc(2 * sizeof(bool)); // can be unsafely allocated since it would never need to be deallocated

    // temp
    void powerOn(Device device)
    {
        if(DeviceStatus[device-1]) return; // already on

        if(!EPS::powerOn(device)) 
        {
            throw "Power on failed";
        }    
    }

    // temp
    void powerOff(Device device)
    {
        if(!DeviceStatus[device-1]) return; // already off

        if(!EPS::powerOff(device)) 
        {
            throw "Power off failed";
        }
    }
}