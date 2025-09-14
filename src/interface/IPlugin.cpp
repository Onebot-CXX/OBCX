#include "interface/IPlugin.hpp"

namespace obcx::interface {

// 静态成员定义
std::vector<std::unique_ptr<core::IBot>> *IPlugin::bots_ = nullptr;
std::mutex *IPlugin::bots_mutex_ = nullptr;

std::tuple<std::unique_lock<std::mutex>,
           std::vector<std::unique_ptr<core::IBot>> &>
IPlugin::get_bots() {
  if (!bots_ || !bots_mutex_) {
    throw std::runtime_error(
        "Bot vector not initialized. Call set_bots() first.");
  }

  std::unique_lock lock(*bots_mutex_);
  return std::make_tuple(std::move(lock), std::ref(*bots_));
}

void IPlugin::set_bots(std::vector<std::unique_ptr<core::IBot>> *bots,
                       std::mutex *mutex) {
  bots_ = bots;
  bots_mutex_ = mutex;
}
std::optional<toml::table> IPlugin::get_config_section(
    const std::string &section_name) const {
  auto config = common::ConfigLoader::instance().get_plugin_config(get_name());
  if (!config)
    return std::nullopt;

  if (auto section = config->config.get(section_name)) {
    if (auto section_table = section->as_table()) {
      return *section_table;
    }
  }

  return std::nullopt;
}
} // namespace obcx::interface