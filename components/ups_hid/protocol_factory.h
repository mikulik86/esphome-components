#pragma once

#include <memory>
#include <functional>
#include <unordered_map>
#include <vector>
#include <string>

namespace esphome {
namespace ups_hid {

// Forward declarations
class UpsProtocolBase;
class UpsHidComponent;

/**
 * Protocol Factory with Self-Registration Support
 * 
 * Enables protocols to register themselves automatically, following the
 * Open/Closed Principle - new protocols can be added without modifying
 * existing code.
 * 
 * Design Pattern: Factory Method + Registry Pattern
 */
class ProtocolFactory {
public:
    // Protocol creator function type
    using CreatorFunc = std::function<std::unique_ptr<UpsProtocolBase>(UpsHidComponent*)>;
    
    // Protocol metadata for better selection
    struct ProtocolInfo {
        CreatorFunc creator;
        std::string name;
        std::string description;
        std::vector<uint16_t> supported_vendors;
        int priority; // Higher priority = tried first
    };
    
    /**
     * Register a protocol with specific vendor IDs
     */
    static void register_protocol_for_vendor(uint16_t vendor_id, 
                                           const ProtocolInfo& info);
    
    /**
     * Register a fallback protocol (tried when vendor-specific fails)
     */
    static void register_fallback_protocol(const ProtocolInfo& info);
    
    /**
     * Create protocol instance for specific vendor
     */
    static std::unique_ptr<UpsProtocolBase> create_for_vendor(uint16_t vendor_id, 
                                                            UpsHidComponent* parent);
    
    /**
     * Create protocol instance by name (manual selection)
     */
    static std::unique_ptr<UpsProtocolBase> create_by_name(const std::string& protocol_name,
                                                         UpsHidComponent* parent);
    
    /**
     * Get ordered list of protocols to try for a vendor
     * Returns vendor-specific first, then fallbacks by priority
     */
    static std::vector<ProtocolInfo> get_protocols_for_vendor(uint16_t vendor_id);
    
    /**
     * Get list of all registered protocols
     */
    static std::vector<std::pair<uint16_t, ProtocolInfo>> get_all_protocols();
    
    /**
     * Check if vendor has registered protocols
     */
    static bool has_vendor_support(uint16_t vendor_id);

private:
    // Vendor-specific protocol registry
    static std::unordered_map<uint16_t, std::vector<ProtocolInfo>>& get_vendor_registry();
    
    // Fallback protocol registry (sorted by priority)
    static std::vector<ProtocolInfo>& get_fallback_registry();
    
    // Ensure registries are initialized
    static void ensure_initialized();
};

/**
 * Protocol Registration
 *
 * The REGISTER_* macros below define a registration function
 * esphome::ups_hid::register_<protocol_name>(), which
 * ProtocolFactory::ensure_initialized() calls explicitly on first use of the factory.
 *
 * Why not static self-registration: ESPHome builds this component into a static
 * library, and the linker only pulls an object file out of that library if another
 * linked object references one of its symbols. A protocol .cpp whose only entry
 * point is a static registrar object is never referenced, so the linker dropped it
 * and the protocol silently never registered (symptom: "No protocol found with name
 * containing ..." / "No suitable protocol found for vendor ..."). Calling the
 * register functions from the factory creates a hard reference to every protocol
 * object file, so all of them are always linked.
 *
 * To add a protocol: use one of the macros at the end of its .cpp (global scope),
 * declare register_<protocol_name>() in the list below, and call it from
 * ProtocolFactory::ensure_initialized() in protocol_factory.cpp.
 */

// Built-in protocols (defined by the macro invocations in protocol_*.cpp)
void register_apc_hid_protocol();
void register_cyberpower_hid_protocol();
void register_generic_hid_protocol();

// Define the registration function for a vendor-specific protocol
#define REGISTER_UPS_PROTOCOL_FOR_VENDOR(vendor_id, protocol_name, creator_func, name_str, desc_str, prio) \
    namespace esphome { \
    namespace ups_hid { \
    void register_##protocol_name() { \
        ProtocolFactory::ProtocolInfo info; \
        info.creator = creator_func; \
        info.name = name_str; \
        info.description = desc_str; \
        info.supported_vendors = {vendor_id}; \
        info.priority = prio; \
        ProtocolFactory::register_protocol_for_vendor(vendor_id, info); \
    } \
    } \
    }

// Define the registration function for a fallback protocol
#define REGISTER_UPS_FALLBACK_PROTOCOL(protocol_name, creator_func, name_str, desc_str, prio) \
    namespace esphome { \
    namespace ups_hid { \
    void register_##protocol_name() { \
        ProtocolFactory::ProtocolInfo info; \
        info.creator = creator_func; \
        info.name = name_str; \
        info.description = desc_str; \
        info.supported_vendors = {}; \
        info.priority = prio; \
        ProtocolFactory::register_fallback_protocol(info); \
    } \
    } \
    }

} // namespace ups_hid
} // namespace esphome
