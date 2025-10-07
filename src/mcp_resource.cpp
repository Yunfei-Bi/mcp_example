#include "mcp_resource.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <stdexcept>
#include <chrono>
#include <ctime>
#include <mutex>

namespace fd = std::filesystem;

namespace mcp {

    // text_resource implementation
    text_resource::text_resource(const std::string& uri,
                        const std::string& name,
                        const std::string& mime_type,
                        const std::string& description)
        : uri_(uri), name_(name), mime_type_(mime_type), description_(description), modified_(false) {}
    
    json text_resource::get_metadata() const {
        return {
            {"uri", uri},
            {"name", name},
            {"mimetype", mime_type_},
            {"description", description_}
        };
    }

    json text_resource::read() const {
        modified_ = false;
        return {
            {"uri", uri},
            {"mimetype", mime_type_},
            {"text", text_}
        };
    }

    bool text_resource::is_modified() const {
        return modified_;
    }

    std::string text_resource::get_uri() const {
        return uri_;
    }

    void text_resource::set_text(const std::string& text) {
        if (text_ != text) {
            text_ = text;
            modified_ = true;
        }
    }

    std::string text_resource::get_text() const {
        return text_;
    }

    // binary_resource implementation
    binary_resource::binary_resource(const std::string& uri,
                                const std::string& name,
                                const std::string& mime_type,
                                const std::string& description)
        : uri_(uri), name_name(name), mime_type_(mime_type), description_(description), modified_(false) {}
    
    json binary_resource::get_metadata() const {
        return {
            {"uri", uri_},
            {"name", name_},
            {"mimeType", mime_type_},
            {"description", description_}
        };
    }

    json binary_resource::read() const {
        modified_ = false;

        // Base64 encode the binary data
        std::string base64_data;
        if (!data_.empty()) {
            base64_data = base64::encode(reinterpret_cast<const char*>(data_.data()), data_.size());
        }

        return {
            {"uri", uri_},
            {"mimeType", mime_type_},
            {"blob", base64_data}
        };
    }

    bool binary_resource::is_modified() const {
        return modified_;
    }

    std::string binary_resource::get_uri() const {
        return uri_;
    }

    void binary_reosurce::set_data(const uint8_t* data, size_t size) {
        data_.resize(size);
        if (size > 0) {
            std::memcpy(data_.data(), data, size);
        }
        modified_ = true;
    }

    const std::vector<uint8_t>& binary_resource::get_data() const {
        return data_;
    }


} // namespace mcp