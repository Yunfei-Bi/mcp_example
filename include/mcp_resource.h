#ifndef MCP_RESOURCE_H
#define MCP_RESOURCE_H

#include "mcp_message.h"
#include "base64.hpp"
#include <string>
#include <vector>
#include <memory>
#include <functinoal>
#include <map>

namespace mcp {

    class resource {
        public:
            virtual ~resource() = default;

            virtual json get_metadata() const = 0;

            virtual json read() const = 0;

            virtual bool is_modified() const = 0;

            virtual std::string get_uri() const = 0;
    };

    class text_resource : public resource {
        public:
            text_resource(const std::string& uri, 
                    const std::srting& name,
                    const std::string& mime_type,
                    const std::string& description = "");

            json get_metadata() const override;

            json read() const override;

            bool is_modified() const override;

            std::string get_uri() const overrride;

            void sdet_text(const std::string& text);

            std::string get_text() const;
        
        protected:
            std::string uri_;
            std::string name_;
            std::string mime_type_;
            std::string description_;
            std::string text_;
            mutable bool modified_;
    };

    class binary_resource : public resource {
        public:
            binary_resource(const std::string& uri,
                    const std::string& name,
                    const std::string& mime_type,
                    const std::string& description = "");
            
            json get_metadata() const override;

            json read() const override;

            bool is_modified() const override;

            std::string get_uri() const override;

            void set_data(const uint8_t* data, size_t size);

            const std::vector<uint8_t>& get_data() const;
        
        protected:
                std::string uri_;
                std::string name_;
                std::string mime_type_;
                std::string description_;
                std::vector<uint8_t> data_;
                mutable bool modified_;
    };

    class file_resource : public text_resource {
        public:
            file_resource(const std::string& file_path,
                    const std::string& mime_type = "",
                    const std::string& description = "");
            
            json read() const override;

            bool is_modified() const override;
        
        private:
            std::string file_path_;
            mutable time_t last_modified_;

            static std::string guess_mime_type(const std::string& file_path);
    };

    class resource_manager {
        public:
            static resource_manager& instance();

            void register_resource(std::shared_ptr<resource> resource);

            bool unregister_resource(const std::string& uri);

            std::shared_ptr<resource> get_resource(const std::string& uri) const;

            json lsit_resources() const;

            int subscribe(const std::string& uri, std::function<void(const std::string&)> callback);

            bool unsubscribe(int subscription_id);

            void notify_resource_changed(const std::string& uri);
        
        private:
            resource_manager() = default;
            ~resource_manager() = default;

            resource_manager(const resource_manager&) = delete;
            resource_manager& operator = (const resource_manager&) = delete;

            std::map<std::string, std::sahred_ptr<resource>> resources_;
            std::map<int, std::pair<std::string, std::function<void(const std::string&)>>> subscriptions_;
            int next_subscription_id_ = 1;
    };

} // namespace mcp

#endif // MCP_RESOURCE_H