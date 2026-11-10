#include "onebot11/bot/standard_event_runtime.hpp"
#include "onebot11/bot/platform_adapter.hpp"
#include "standard_event_codec.hpp"
#include "standard_event_context.hpp"
#include <algorithm>
#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <cmath>
#include <deque>
#include <mutex>
#include <set>
#include <utility>

namespace obcx::onebot11::bot::standard::detail {
namespace {
using Json = common::json;
// Classify only top-level response discriminators without building a DOM or
// applying event bounds to API replies. Strict event preflight follows for all
// other frames; array entries never turn a poll batch into an API response.
class EventTrafficClassifier final : public nlohmann::json_sax<Json> {
public:
  auto null() -> bool override { return true; }
  auto boolean(bool) -> bool override { return true; }
  auto number_integer(number_integer_t) -> bool override { return true; }
  auto number_unsigned(number_unsigned_t) -> bool override { return true; }
  auto number_float(number_float_t n, const string_t &) -> bool override {
    return std::isfinite(n);
  }
  auto string(string_t &) -> bool override { return true; }
  auto binary(binary_t &) -> bool override { return false; }
  auto start_object(std::size_t) -> bool override {
    if (depth_ == 0)
      root_object_ = true;
    ++depth_;
    return true;
  }
  auto key(string_t &text) -> bool override {
    if (root_object_ && depth_ == 1 && (text == "echo" || text == "retcode"))
      api_only_ = true;
    return true;
  }
  auto end_object() -> bool override {
    --depth_;
    return true;
  }
  auto start_array(std::size_t) -> bool override {
    ++depth_;
    return true;
  }
  auto end_array() -> bool override {
    --depth_;
    return true;
  }
  auto parse_error(std::size_t, const std::string &, const Json::exception &)
      -> bool override {
    return false;
  }
  auto api_only() const -> bool { return api_only_; }

private:
  std::size_t depth_{};
  bool root_object_{};
  bool api_only_{};
};
class EventFramePreflight final : public nlohmann::json_sax<Json> {
public:
  explicit EventFramePreflight(const ValueLimits &limits) : limits_(limits) {}

  auto null() -> bool override { return value(false); }
  auto boolean(bool) -> bool override { return value(false); }
  auto number_integer(number_integer_t) -> bool override {
    return value(false);
  }
  auto number_unsigned(number_unsigned_t) -> bool override {
    return value(false);
  }
  auto number_float(number_float_t number, const string_t &) -> bool override {
    if (!std::isfinite(number))
      return false;
    return value(false);
  }
  auto string(string_t &text) -> bool override {
    value(false);
    input_text(text);
    return true;
  }
  auto binary(binary_t &) -> bool override { return false; }

  auto start_object(std::size_t) -> bool override {
    value(true);
    frames_.emplace_back();
    return true;
  }
  auto key(string_t &text) -> bool override {
    input_text(text);
    auto &keys = frames_.back();
    if (keys.contains(text))
      throw std::invalid_argument("standard event contains duplicate keys");
    // Charge the actual decoded key copy before insertion. Object teardown
    // releases scratch, but never refunds this cumulative logical cost.
    charge(text.size(), bytes_, limits_.maximum_bytes);
    keys.insert(text);
    return true;
  }
  auto end_object() -> bool override {
    frames_.pop_back();
    return true;
  }
  auto start_array(std::size_t) -> bool override {
    value(false);
    frames_.emplace_back();
    return true;
  }
  auto end_array() -> bool override {
    frames_.pop_back();
    return true;
  }
  auto parse_error(std::size_t, const std::string &, const Json::exception &)
      -> bool override {
    return false;
  }

  void reserve_retained_tree() {
    // No DOM exists yet. Reserve its full cost against the SAME ledger that
    // already contains preflight input and duplicate-detection key copies.
    charge(input_nodes_, nodes_, limits_.maximum_nodes);
    charge(input_bytes_, bytes_, limits_.maximum_bytes);
  }

private:
  static void charge(std::size_t count, std::size_t &used,
                     std::size_t maximum) {
    if (count > maximum - used)
      throw std::invalid_argument("standard event exceeds value limits");
    used += count;
  }

  auto value(bool object) -> bool {
    (void)object;
    // Open containers equal the next value's root-zero depth; keys do not
    // enter this path. Validated maximum_depth <= 32 also bounds stack growth.
    if (frames_.size() > limits_.maximum_depth)
      throw std::invalid_argument("standard event exceeds depth limit");
    charge(1, nodes_, limits_.maximum_nodes);
    charge(1, input_nodes_, limits_.maximum_nodes);
    return true;
  }
  void input_text(std::string_view text) {
    if (text.size() > limits_.maximum_text_bytes)
      throw std::invalid_argument("standard event exceeds text limit");
    // The strict JSON lexer validates UTF-8 and decodes escapes before these
    // callbacks. Empty strings/keys remain legal with a zero text allowance.
    charge(text.size(), bytes_, limits_.maximum_bytes);
    charge(text.size(), input_bytes_, limits_.maximum_bytes);
  }

  const ValueLimits &limits_;
  std::size_t nodes_{};
  std::size_t bytes_{};
  std::size_t input_nodes_{};
  std::size_t input_bytes_{};
  // Deque growth does not copy existing key sets. Arrays have empty sets;
  // neither stack/set bookkeeping nor lexer buffers are logical JSON nodes.
  std::deque<std::set<Json::string_t>> frames_;
};
} // namespace
auto parse_standard_event_frame(std::string_view wire,
                                const ValueLimits &values) -> common::json {
  values.validate();
  if (wire.size() > values.maximum_bytes ||
      wire.find('\0') != std::string_view::npos)
    throw std::invalid_argument("invalid standard event frame");
  try {
    EventFramePreflight preflight(values);
    if (!Json::sax_parse(wire.begin(), wire.end(), &preflight,
                         Json::input_format_t::json, true, false))
      throw std::invalid_argument("invalid standard event frame");
    preflight.reserve_retained_tree();
    return Json::parse(wire.begin(), wire.end(), nullptr, true, false);
  } catch (const Json::exception &) {
    throw std::invalid_argument("invalid standard event frame");
  }
}
} // namespace obcx::onebot11::bot::standard::detail

namespace obcx::onebot11::bot::standard {
struct StandardEventCapabilityV1::Subscriber {
  std::atomic_bool active{true};
  std::mutex mutex;
  Handler handler;

  explicit Subscriber(Handler value) : handler(std::move(value)) {}

  void remove() noexcept {
    Handler retired;
    {
      std::lock_guard lock(mutex);
      active.store(false);
      retired.swap(handler);
    }
  }

  void invoke(const std::shared_ptr<const StandardEventV1> &event) {
    Handler claimed;
    {
      std::lock_guard lock(mutex);
      if (!active.load())
        return;
      claimed.swap(handler);
    }
    // The capability strand serializes invocations. No callback is copied,
    // and removal can discard the stored callback without waiting for work.
    if (claimed && active.load()) {
      try {
        claimed(event);
      } catch (...) { /* Never expose native data. */
      }
    }
    {
      std::lock_guard lock(mutex);
      if (active.load())
        claimed.swap(handler);
    }
  }
};

struct StandardEventCapabilityV1::State {
  boost::asio::strand<boost::asio::any_io_executor> executor;
  obcx::bot::BotInstallationRef installation;
  std::shared_ptr<core::SelfIdentityCache> identity;
  std::optional<ValueLimits> limits;
  std::shared_ptr<std::atomic_bool> active;
  std::mutex mutex;
  std::vector<std::shared_ptr<Subscriber>> handlers;
  State(boost::asio::any_io_executor e, obcx::bot::BotInstallationRef i)
      : executor(boost::asio::make_strand(e)), installation(std::move(i)),
        active(std::make_shared<std::atomic_bool>(true)) {}
};
EventActionHandle::EventActionHandle(
    std::shared_ptr<const detail::StandardEventContext> context)
    : context_(std::move(context)) {}
auto EventActionHandle::installation() const
    -> const obcx::bot::BotInstallationRef & {
  return detail::StandardEventAccess::context(*this).installation;
}
StandardEventV1::StandardEventV1(obcx::bot::BotInstallationRef i,
                                 StandardEventObservation o,
                                 std::optional<EventActionHandle> h)
    : installation_(std::move(i)), observation_(std::move(o)),
      handle_(std::move(h)) {}
StandardEventCapabilityV1::StandardEventCapabilityV1(
    std::shared_ptr<State> state)
    : state_(std::move(state)) {}
StandardEventCapabilityV1::~StandardEventCapabilityV1() { close(); }
StandardEventCapabilityV1::Subscription::Subscription(
    std::shared_ptr<Subscriber> subscriber)
    : subscriber_(std::move(subscriber)) {}
StandardEventCapabilityV1::Subscription::Subscription(
    Subscription &&other) noexcept
    : subscriber_(std::move(other.subscriber_)) {}
auto StandardEventCapabilityV1::Subscription::operator=(
    Subscription &&other) noexcept -> Subscription & {
  if (this != &other) {
    reset();
    subscriber_ = std::move(other.subscriber_);
  }
  return *this;
}
StandardEventCapabilityV1::Subscription::~Subscription() { reset(); }
void StandardEventCapabilityV1::Subscription::reset() noexcept {
  if (auto subscriber = std::exchange(subscriber_, {}))
    subscriber->remove();
}
void StandardEventCapabilityV1::subscribe(Handler handler) {
  auto subscription = subscribe_scoped(std::move(handler));
  // Legacy host subscriptions remain owned by the capability until close.
  subscription.subscriber_.reset();
}
auto StandardEventCapabilityV1::subscribe_scoped(Handler handler)
    -> Subscription {
  if (!handler)
    throw std::invalid_argument("standard event subscriber required");
  auto subscriber = std::make_shared<Subscriber>(std::move(handler));
  {
    std::lock_guard lock(state_->mutex);
    if (!state_->active->load())
      throw std::invalid_argument("standard event capability closed");
    std::erase_if(state_->handlers,
                  [](const auto &entry) { return !entry->active.load(); });
    state_->handlers.push_back(subscriber);
  }
  return Subscription{std::move(subscriber)};
}
void StandardEventCapabilityV1::close() noexcept {
  state_->active->store(false);
  std::vector<std::shared_ptr<Subscriber>> retired;
  {
    std::lock_guard lock(state_->mutex);
    retired.swap(state_->handlers);
  }
  for (const auto &subscriber : retired)
    subscriber->remove();
}
namespace detail {
auto StandardEventAccess::create(boost::asio::any_io_executor e,
                                 obcx::bot::BotInstallationRef i)
    -> std::shared_ptr<StandardEventCapabilityV1> {
  i.validate();
  if (i.surface != obcx::bot::SurfaceId{"onebot11.qq"})
    throw std::invalid_argument("invalid standard event installation");
  return std::shared_ptr<StandardEventCapabilityV1>(
      new StandardEventCapabilityV1{
          std::make_shared<StandardEventCapabilityV1::State>(std::move(e),
                                                             std::move(i))});
}
void StandardEventAccess::configure(
    StandardEventCapabilityV1 &cap,
    const obcx::bot::BotInstallationRef &installation,
    std::shared_ptr<core::SelfIdentityCache> identity, ValueLimits limits) {
  installation.validate();
  limits.validate();
  auto s = cap.state_;
  if (!identity || installation != s->installation)
    throw std::invalid_argument("invalid standard event configuration");
  std::lock_guard lock(s->mutex);
  if (!s->active->load() || s->identity || s->limits)
    throw std::invalid_argument("standard event configuration unavailable");
  s->identity = std::move(identity);
  s->limits = limits;
}
auto StandardEventAccess::context(const EventActionHandle &h)
    -> const StandardEventContext & {
  if (!h.context_)
    throw std::invalid_argument("empty event action handle");
  return *h.context_;
}
void StandardEventAccess::validate(const EventActionHandle &h) {
  const auto &c = context(h);
  if (!c.active->load())
    throw std::invalid_argument("retired event action handle");
  QqPlatformAdapter binding;
  binding.codec().validate(c.installation);
  const obcx::bot::UserRef self{
      c.installation,
      binding.codec().make<obcx::bot::UserId>(
          "user",
          {std::to_string(c.native.at("self_id").get<std::int64_t>())})};
  if (!c.identity->matches_observed_user(c.origin, self))
    throw std::invalid_argument("event action origin is unavailable");
}
auto StandardEventAccess::receive(StandardEventCapabilityV1 &cap,
                                  std::string_view wire) -> EventIngressFrame {
  try {
    if (wire.find('\0') != std::string_view::npos)
      return {EventIngressDisposition::Rejected, nullptr};
    EventTrafficClassifier classifier;
    if (!Json::sax_parse(wire.begin(), wire.end(), &classifier,
                         Json::input_format_t::json, true, false))
      return {EventIngressDisposition::Rejected, nullptr};
    if (classifier.api_only())
      return {EventIngressDisposition::ApiOnly, nullptr};
    auto s = cap.state_;
    std::optional<ValueLimits> limits;
    {
      std::lock_guard lock(s->mutex);
      // Closing the sibling capability stops standard admission, not legacy
      // delivery. Its configure-once structural limits remain authoritative.
      if (!s->limits)
        return {EventIngressDisposition::Rejected, nullptr};
      limits = s->limits;
    }
    auto frame = parse_standard_event_frame(wire, *limits);
    if (!frame.is_object() && !frame.is_array())
      return {EventIngressDisposition::Rejected, nullptr};
    return {EventIngressDisposition::EventCompatible, std::move(frame)};
  } catch (...) {
    return {EventIngressDisposition::Rejected, nullptr};
  }
}
auto StandardEventAccess::event_candidate(const common::json &d) -> bool {
  return d.is_object() && !d.contains("echo") && !d.contains("retcode");
}
void StandardEventAccess::admit(
    StandardEventCapabilityV1 &cap, common::json d,
    const std::optional<core::SelfIdentityOrigin> &origin) {
  auto s = cap.state_;
  std::shared_ptr<core::SelfIdentityCache> identity;
  std::optional<ValueLimits> limits;
  std::vector<std::shared_ptr<StandardEventCapabilityV1::Subscriber>> handlers;
  try {
    {
      std::lock_guard lock(s->mutex);
      if (!s->active->load() || !s->identity || !s->limits)
        return;
      identity = s->identity;
      limits = s->limits;
      // Freeze eligibility at admission, before decoding or native queueing.
      // Only removable core state is copied, never subscriber callables.
      handlers = s->handlers;
    }
    if (!event_candidate(d) || !d.contains("post_type"))
      return;
    auto observation = decode_standard_event(d, *limits);
    std::optional<EventActionHandle> handle;
    if (origin &&
        identity->observe_user_and_capture(*origin, std::nullopt, [] {})
            .accepted()) {
      std::optional<EventActionSource> source;
      if (std::holds_alternative<PrivateMessageObservation>(observation))
        source = EventActionSource::PrivateMessage;
      if (std::holds_alternative<GroupMessageObservation>(observation))
        source = EventActionSource::GroupMessage;
      if (std::holds_alternative<ReceivedFriendRequest>(observation))
        source = EventActionSource::FriendRequest;
      if (std::holds_alternative<ReceivedGroupRequest>(observation))
        source = EventActionSource::GroupRequest;
      if (source)
        handle = EventActionHandle{std::make_shared<StandardEventContext>(
            StandardEventContext{s->installation, identity, *origin, s->active,
                                 *source, std::move(d)})};
    }
    auto event = std::shared_ptr<const StandardEventV1>(new StandardEventV1{
        s->installation, std::move(observation), std::move(handle)});
    // No user callback runs under transport/cache/subscriber locks.
    boost::asio::post(s->executor, [s, event = std::move(event),
                                    handlers = std::move(handlers)] {
      try {
        for (const auto &handler : handlers) {
          if (!s->active->load())
            break;
          try {
            handler->invoke(event);
          } catch (...) { /* Fixed boundary: never log native data. */
          }
        }
      } catch (
          ...) { /* Delivery allocation failure never exposes native data. */
      }
    });
  } catch (...) {
    // A structurally valid extension may have no standard typed projection.
    // The transport can still convert it through the legacy event adapter.
  }
}
} // namespace detail
} // namespace obcx::onebot11::bot::standard
