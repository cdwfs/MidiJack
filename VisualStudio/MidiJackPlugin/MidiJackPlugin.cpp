#include "stdafx.h"

namespace
{
    // Basic type aliases
    using DeviceHandle = HMIDIIN;
    using DeviceID = uint32_t;
    using EndpointID = uint32_t;

    struct DeviceInfo
    {
      DeviceHandle handle;
      DeviceID deviceId;
      EndpointID endpointId;
      MIDIINCAPS deviceCaps;
    };
    std::map<DeviceHandle, DeviceInfo> handle_to_device_info;
    std::map<EndpointID, DeviceHandle> endpoint_id_to_handle;

    // MIDI message storage class
    class MidiMessage
    {
        DeviceID source_;
        uint8_t status_;
        uint8_t data1_;
        uint8_t data2_;

    public:

        MidiMessage(DeviceID source, uint32_t rawData)
            : source_(source), status_(rawData), data1_(rawData >> 8), data2_(rawData >> 16)
        {
        }

        uint64_t Encode64Bit()
        {
            uint64_t ul = source_;
            ul |= (uint64_t)status_ << 32;
            ul |= (uint64_t)data1_ << 40;
            ul |= (uint64_t)data2_ << 48;
            return ul;
        }

        std::string ToString()
        {
            char temp[256];
            std::snprintf(temp, sizeof(temp), "(%X) %02X %02X %02X", source_, status_, data1_, data2_);
            return temp;
        }
    };

    // Incoming MIDI message queue
    std::queue<MidiMessage> message_queue;

    // Device handler lists
    std::list<DeviceHandle> active_handles;

    // Mutex for resources
    std::recursive_mutex resource_lock;

    // MIDI input callback
    static void CALLBACK MidiInProc(HMIDIIN hMidiIn, UINT wMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
    {
        if (wMsg == MIM_DATA)
        {
            EndpointID id = handle_to_device_info[hMidiIn].endpointId;
            uint32_t raw = static_cast<uint32_t>(dwParam1);
            resource_lock.lock();
            message_queue.push(MidiMessage(id, raw));
            resource_lock.unlock();
        }
    }

    // Retrieve a name of a given device.
    std::string GetDeviceName(DeviceHandle handle)
    {
        auto itor = handle_to_device_info.find(handle);
        if (itor != handle_to_device_info.end()) {
            std::wstring name((*itor).second.deviceCaps.szPname);
            return std::string(name.begin(), name.end());
        }
        return "unknown";
    }

    std::string GetMidiErrorText(MMRESULT err)
    {
        std::wstring buf(512, 0);
        midiInGetErrorText(err, &buf[0], 512);
        return std::string(buf.begin(), buf.end());
    }

    // Open a MIDI device with a given device ID (0..numConnectedDevices-1).
    void OpenDevice(unsigned int deviceId)
    {
        static const DWORD_PTR callback = reinterpret_cast<DWORD_PTR>(MidiInProc);
        DeviceHandle handle;
        if (midiInOpen(&handle, deviceId, callback, NULL, CALLBACK_FUNCTION) == MMSYSERR_NOERROR)
        {
            if (midiInStart(handle) == MMSYSERR_NOERROR)
            {
                DeviceInfo info = {};
                info.handle = handle;
                info.deviceId = deviceId;
                info.endpointId = static_cast<EndpointID>(uintptr_t(handle) & 0xFFFFFFFF);
                if (midiInGetDevCaps(deviceId, &info.deviceCaps, sizeof(info.deviceCaps)) == MMSYSERR_NOERROR)
                {
                    resource_lock.lock();
                    endpoint_id_to_handle[info.endpointId] = handle;
                    handle_to_device_info[handle] = info;
                    active_handles.push_back(handle);
                    resource_lock.unlock();
                }
                else
                {
                    midiInClose(handle);
                }
            }
            else
            {
                midiInClose(handle);
            }
        }
    }

    // Close a given handler.
    void CloseDevice(DeviceHandle handle)
    {
        midiInClose(handle);

        resource_lock.lock();
        active_handles.remove(handle);
        EndpointID endpoint_id = handle_to_device_info[handle].endpointId;
        endpoint_id_to_handle.erase(endpoint_id);
        handle_to_device_info.erase(handle);
        resource_lock.unlock();
    }

    // Open the all devices.
    void OpenAllDevices()
    {
        uint32_t device_count = midiInGetNumDevs();
        for (DeviceID id = 0; id < device_count; id++)
        {
            OpenDevice(id);
        }
    }

    // Refresh device handlers
    void RefreshDevices()
    {
        resource_lock.lock();

        // Close disconnected handlers.
        for (auto h : active_handles)
        {
          CloseDevice(h);
        }

        // Try open all devices
        OpenAllDevices();

        resource_lock.unlock();
    }

    // Close the all devices.
    void CloseAllDevices()
    {
        resource_lock.lock();
        while (!active_handles.empty())
            CloseDevice(active_handles.front());
        resource_lock.unlock();
    }
}

// Exported functions

#define EXPORT_API extern "C" __declspec(dllexport)

// Refresh the list of active endpoint devices.
// This will force-close all previously-connected
// endpoints and recreate them from scratch.
EXPORT_API int MidiJackRefreshEndpoints()
{
    RefreshDevices();

    return static_cast<int>(active_handles.size());
}

// Counts the number of endpoints.
EXPORT_API int MidiJackCountEndpoints()
{
    return static_cast<int>(active_handles.size());
}

// Get the unique ID of an endpoint.
EXPORT_API uint32_t MidiJackGetEndpointIDAtIndex(int index)
{
    auto itr = active_handles.begin();
    std::advance(itr, index);
    return handle_to_device_info[*itr].endpointId;
}

// Get the name of an endpoint.
EXPORT_API const char* MidiJackGetEndpointName(uint32_t id)
{
    auto handle = endpoint_id_to_handle[id];
    static std::string buffer;
    buffer = GetDeviceName(handle);
    return buffer.c_str();
}

// Retrieve and erase an MIDI message data from the message queue.
EXPORT_API uint64_t MidiJackDequeueIncomingData()
{
    if (message_queue.empty()) return 0;

    resource_lock.lock();
    auto msg = message_queue.front();
    message_queue.pop();
    resource_lock.unlock();

    return msg.Encode64Bit();
}
