#pragma once

#include "esp_err.h"
#include <vector>
#include <cstdint>
#include <memory>

namespace esphome {
namespace ups_hid {

/**
 * Abstract USB Transport Interface
 * 
 * Provides a clean abstraction over USB communication, allowing for
 * different implementations (ESP32 hardware, simulation, etc.)
 * 
 * Design Pattern: Strategy Pattern for transport selection
 */
class IUsbTransport {
public:
    virtual ~IUsbTransport() = default;
    
    // Transport lifecycle
    virtual esp_err_t initialize() = 0;
    virtual esp_err_t deinitialize() = 0;
    
    // Device management
    virtual bool is_connected() const = 0;
    virtual uint16_t get_vendor_id() const = 0;
    virtual uint16_t get_product_id() const = 0;
    
    // HID communication
    virtual esp_err_t hid_get_report(uint8_t report_type, uint8_t report_id, 
                                   uint8_t* data, size_t* data_len, 
                                   uint32_t timeout_ms = 1000) = 0;
    
    virtual esp_err_t hid_set_report(uint8_t report_type, uint8_t report_id,
                                   const uint8_t* data, size_t data_len,
                                   uint32_t timeout_ms = 1000) = 0;
    
    // String descriptors
    virtual esp_err_t get_string_descriptor(uint8_t string_index, 
                                          std::string& result) = 0;
    
    // HID report descriptor of the claimed interface. descriptor_index > 0 selects an
    // alternate descriptor on devices that expose more than one (Eaton firmware 2.02).
    virtual esp_err_t get_hid_report_descriptor(uint8_t /*descriptor_index*/,
                                              std::vector<uint8_t>& /*descriptor*/) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    // Device release number (bcdDevice), 0 if unknown
    virtual uint16_t get_device_release() const { return 0; }

    // Changes every time a device is attached, so a replug between two polls can be
    // told apart from a connection that never dropped. 0 if not tracked.
    virtual uint32_t get_connection_id() const { return 0; }

    // Error information
    virtual std::string get_last_error() const = 0;
};

// Forward declaration - implementation in usb_transport_factory.h
class UsbTransportFactory;

} // namespace ups_hid
} // namespace esphome