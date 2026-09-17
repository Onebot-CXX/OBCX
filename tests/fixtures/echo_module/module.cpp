#include "module.hpp"
#include "contract.hpp"
#include "core/bot/bot_component_runtime.hpp"
#include "core/bot/configuration_fingerprint.hpp"
#include "core/bot/configuration_validation.hpp"
#include "core/bot/operation_registry.hpp"

namespace obcx::tests::echo {
namespace {

// Private typed configuration: the generic runtime never interprets these keys.
struct Connection {
  std::string prefix;
  std::string secret;
  std::shared_ptr<Observations> observations;
};
auto connection_descriptor() -> core::ComponentDescriptor {
  return {core::ComponentId{"test.echo.connection"},
          {core::CapabilityId{"test.echo.connection"}},
          {}};
}
auto operation_descriptor() -> core::ComponentDescriptor {
  return {core::ComponentId{"test.echo.operations"},
          {core::CapabilityId{"bot.operations"}},
          {core::CapabilityId{"test.echo.connection"}}};
}
auto definition() -> core::OperationDefinition<Request> {
  return core::OperationDefinition<Request>{{"test.echo.connection"}};
}
class ConnectionComponent final : public core::BotComponent {
public:
  explicit ConnectionComponent(Connection config)
      : connection_(std::make_shared<Connection>(std::move(config))) {
    ++connection_->observations->constructions;
  }
  auto descriptor() const -> core::ComponentDescriptor override {
    return connection_descriptor();
  }
  void install_capabilities(core::CapabilityRegistry &registry) override {
    registry.install(descriptor().id,
                     core::CapabilityId{"test.echo.connection"}, connection_);
  }
  void prepare(const core::CapabilityRegistry &) override {
    ++connection_->observations->prepares;
  }
  void start() override { ++connection_->observations->starts; }
  void stop() override {
    std::scoped_lock lock(connection_->observations->mutex);
    connection_->observations->stopped.emplace_back("connection");
  }

private:
  std::shared_ptr<Connection> connection_;
};
class OperationsComponent final : public core::BotComponent {
public:
  OperationsComponent(std::string id,
                      std::shared_ptr<Observations> observations)
      : registry_(std::make_shared<core::OperationRegistry>(
            bot::BotInstallationRef{std::move(id), surface})),
        observations_(std::move(observations)) {
    ++observations_->constructions;
  }
  auto descriptor() const -> core::ComponentDescriptor override {
    return operation_descriptor();
  }
  void install_capabilities(core::CapabilityRegistry &registry) override {
    registry.install(descriptor().id, core::CapabilityId{"bot.operations"},
                     registry_);
  }
  void prepare(const core::CapabilityRegistry &registry) override {
    auto connection =
        registry.get<Connection>(core::CapabilityId{"test.echo.connection"});
    registry_->install(definition().bind(
        [connection](const Request &request)
            -> boost::asio::awaitable<bot::BotOperationResult<Result>> {
          ++connection->observations->calls;
          co_return bot::BotOperationResult<Result>::success(
              {connection->prefix + request.text});
        }));
    const std::vector<std::string> capabilities{"test.echo.connection"};
    registry_->seal(registry_->installation(), capabilities);
    ++observations_->prepares;
  }
  void start() override { ++observations_->starts; }
  void stop() override {
    registry_->close();
    std::scoped_lock lock(observations_->mutex);
    observations_->stopped.emplace_back("operations");
  }

private:
  std::shared_ptr<core::OperationRegistry> registry_;
  std::shared_ptr<Observations> observations_;
};
} // namespace

void register_module(core::BotPlatformCatalog &catalog,
                     std::shared_ptr<Observations> observations) {
  catalog.register_recipe(
      {surface, "memory", "echo",
       [observations](const core::BotInstallationInput &input,
                      const toml::table &table, std::string_view path) {
         ++observations->parses;
         core::configuration::validate_keys(table, {"prefix", "secret"}, path,
                                            {});
         const Connection connection{
             core::configuration::required_string(table, "prefix", path),
             core::configuration::required_string(table, "secret", path),
             observations};
         const core::BotRecipeDescription recipe{
             "test.echo.memory",
             surface,
             "memory",
             {connection_descriptor(), operation_descriptor()},
             {definition().description().action},
             std::nullopt};
         return std::make_shared<core::BotInstallationPlan>(
             common::BotInstallationMetadata{input.installation_id,
                                             input.enabled, input.surface,
                                             input.transport, "echo", ""},
             recipe, core::configuration_digest(table),
             [connection,
              id = input.installation_id](boost::asio::io_context &) {
               std::vector<std::unique_ptr<core::BotComponent>> components;
               components.push_back(
                   std::make_unique<ConnectionComponent>(connection));
               components.push_back(std::make_unique<OperationsComponent>(
                   id, connection.observations));
               return components;
             },
             nullptr);
       }});
}
} // namespace obcx::tests::echo
