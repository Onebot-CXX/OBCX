module;

import std;

import common.config_loader;
import common.logger;
import interface.connection_manager;

export module common.component_manager;
export namespace obcx::common
{
    class PluginManager;
}

export namespace obcx::core
{
    class IBot;
}

export namespace obcx::common
{
    class ComponentManager
    {
    public:
        static auto instance() -> ComponentManager&;

        static auto create_bot(
            const obcx::common::BotConfig& config,
            const std::shared_ptr<::obcx::core::TaskScheduler>& task_scheduler)
            -> std::unique_ptr<::obcx::core::IBot>;

        static auto get_connection_type(const std::string& type,
                                        const std::string& bot_type)
            -> ::obcx::network::ConnectionManagerFactory::ConnectionType;

        static auto create_connection_config(const toml::table& conn_table)
            -> obcx::common::ConnectionConfig;

        static auto setup_bot(::obcx::core::IBot& bot,
                              const obcx::common::BotConfig& config,
                              obcx::common::PluginManager& plugin_manager) -> bool;

    private:
        ComponentManager() = default;
    };
} // namespace obcx::common
