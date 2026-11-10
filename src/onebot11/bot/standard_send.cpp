#include "standard_composition_codec.hpp"
#include "standard_send_codec.hpp"
#include <charconv>
#include <cmath>
#include <limits>
#include <type_traits>

// These field tables are private SDK parameter framing, never native JSON or
// a public arbitrary-message importer. All decode paths are bounded first.
namespace obcx::bot {
OBCX_BOT_FIELDS(onebot11::bot::standard::Text, OBCX_BOT_FIELD(text));
OBCX_BOT_FIELDS(onebot11::bot::standard::Face, OBCX_BOT_FIELD(id));
OBCX_BOT_FIELDS(onebot11::bot::standard::AtUser, OBCX_BOT_FIELD(user));
OBCX_BOT_FIELDS(onebot11::bot::standard::AtAll);
OBCX_BOT_FIELDS(onebot11::bot::standard::Rps);
OBCX_BOT_FIELDS(onebot11::bot::standard::Dice);
OBCX_BOT_FIELDS(onebot11::bot::standard::Shake);
OBCX_BOT_FIELDS(onebot11::bot::standard::Poke, OBCX_BOT_FIELD(type),
                OBCX_BOT_FIELD(id));
OBCX_BOT_FIELDS(onebot11::bot::standard::Anonymous, OBCX_BOT_FIELD(ignore));
OBCX_BOT_FIELDS(onebot11::bot::standard::Share, OBCX_BOT_FIELD(url),
                OBCX_BOT_FIELD(title), OBCX_BOT_FIELD(content),
                OBCX_BOT_FIELD(image_url));
OBCX_BOT_FIELDS(onebot11::bot::standard::UserContact, OBCX_BOT_FIELD(user));
OBCX_BOT_FIELDS(onebot11::bot::standard::GroupContact, OBCX_BOT_FIELD(group));
OBCX_BOT_FIELDS(onebot11::bot::standard::DecimalCoordinate,
                OBCX_BOT_FIELD(decimal));
OBCX_BOT_FIELDS(onebot11::bot::standard::Location, OBCX_BOT_FIELD(latitude),
                OBCX_BOT_FIELD(longitude), OBCX_BOT_FIELD(title),
                OBCX_BOT_FIELD(content));
OBCX_BOT_FIELDS(onebot11::bot::standard::Music, OBCX_BOT_FIELD(provider),
                OBCX_BOT_FIELD(id));
OBCX_BOT_FIELDS(onebot11::bot::standard::CustomMusic, OBCX_BOT_FIELD(url),
                OBCX_BOT_FIELD(audio_url), OBCX_BOT_FIELD(title),
                OBCX_BOT_FIELD(content), OBCX_BOT_FIELD(image_url));
OBCX_BOT_FIELDS(onebot11::bot::standard::XmlContent, OBCX_BOT_FIELD(document));
OBCX_BOT_FIELDS(onebot11::bot::standard::JsonContent, OBCX_BOT_FIELD(document));
OBCX_BOT_FIELDS(onebot11::bot::standard::ProviderFileName,
                OBCX_BOT_FIELD(installation), OBCX_BOT_FIELD(name),
                OBCX_BOT_FIELD(work));
OBCX_BOT_FIELDS(onebot11::bot::standard::InlineBase64, OBCX_BOT_FIELD(data));
OBCX_BOT_FIELDS(onebot11::bot::standard::RemoteMediaUrl, OBCX_BOT_FIELD(url),
                OBCX_BOT_FIELD(cache), OBCX_BOT_FIELD(proxy),
                OBCX_BOT_FIELD(timeout_seconds), OBCX_BOT_FIELD(work));
OBCX_BOT_FIELDS(onebot11::bot::standard::Image, OBCX_BOT_FIELD(source),
                OBCX_BOT_FIELD(flash));
OBCX_BOT_FIELDS(onebot11::bot::standard::Record, OBCX_BOT_FIELD(source),
                OBCX_BOT_FIELD(magic));
OBCX_BOT_FIELDS(onebot11::bot::standard::Video, OBCX_BOT_FIELD(source));
OBCX_BOT_FIELDS(onebot11::bot::standard::Reply, OBCX_BOT_FIELD(target));
OBCX_BOT_FIELDS(onebot11::bot::standard::ScopedReferenceNode,
                OBCX_BOT_FIELD(message), OBCX_BOT_FIELD(work));
OBCX_BOT_FIELDS(onebot11::bot::standard::IdSelectedNode,
                OBCX_BOT_FIELD(message), OBCX_BOT_FIELD(work));
OBCX_BOT_FIELDS(onebot11::bot::standard::SequenceIndex, OBCX_BOT_FIELD(value));
OBCX_BOT_FIELDS(onebot11::bot::standard::AuthoredNode, OBCX_BOT_FIELD(author),
                OBCX_BOT_FIELD(nickname), OBCX_BOT_FIELD(content));
OBCX_BOT_FIELDS(onebot11::bot::standard::Sequence, OBCX_BOT_FIELD(encoding),
                OBCX_BOT_FIELD(segments));
OBCX_BOT_FIELDS(onebot11::bot::standard::Composition,
                OBCX_BOT_FIELD(sequences));
OBCX_BOT_FIELDS(onebot11::bot::standard::LiteralText, OBCX_BOT_FIELD(text));
OBCX_BOT_FIELDS(onebot11::bot::standard::SendPrivateMsgParameters,
                OBCX_BOT_FIELD(target), OBCX_BOT_FIELD(message),
                OBCX_BOT_FIELD(auto_escape));
OBCX_BOT_FIELDS(onebot11::bot::standard::SendGroupMsgParameters,
                OBCX_BOT_FIELD(target), OBCX_BOT_FIELD(message),
                OBCX_BOT_FIELD(auto_escape));
OBCX_BOT_FIELDS(onebot11::bot::standard::SendMsgParameters,
                OBCX_BOT_FIELD(target), OBCX_BOT_FIELD(message_type),
                OBCX_BOT_FIELD(message), OBCX_BOT_FIELD(auto_escape));
} // namespace obcx::bot

namespace obcx::onebot11::bot::standard::detail {
namespace {
namespace bot_model = obcx::bot;
namespace dd = obcx::bot::detail;
using Json = bot_model::Json;

void send_wire_inspect(ContentBudget &budget, const Json &value,
                       std::size_t depth) {
  budget.node(depth);
  if (value.is_string())
    budget.string(value.get_ref<const std::string &>());
  else if (value.is_object()) {
    for (auto it = value.begin(); it != value.end(); ++it) {
      budget.string(it.key());
      send_wire_inspect(budget, it.value(), depth + 1);
    }
  } else if (value.is_array()) {
    for (const auto &item : value)
      send_wire_inspect(budget, item, depth + 1);
  } else if (!(value.is_null() || value.is_boolean() ||
               value.is_number_integer() ||
               (value.is_number_float() && std::isfinite(value.get<double>()))))
    invalid_content();
}

template <class T>
void send_sdk_inspect(ContentBudget &budget, const T &value,
                      std::size_t depth) {
  budget.node(depth);
  if constexpr (std::same_as<T, MessageSelector>) {
    budget.string("installation");
    send_sdk_inspect(budget, value.installation(), depth + 1);
    budget.string("message_id");
    budget.node(depth + 1);
  } else if constexpr (std::same_as<T, CallerFileUri>) {
    budget.string("uri");
    budget.string_value(value.uri, depth + 1);
  } else if constexpr (std::same_as<T, bot_model::BotInstallationRef>) {
    budget.string("installation_id");
    budget.string_value(value.installation_id, depth + 1);
    budget.string("surface");
    budget.string_value(value.surface.value(), depth + 1);
  } else if constexpr (std::same_as<T, bot_model::UserRef> ||
                       std::same_as<T, bot_model::ConversationRef>) {
    budget.string("installation");
    send_sdk_inspect(budget, value.installation, depth + 1);
    budget.string("id");
    budget.string_value(value.id.serialized(), depth + 1);
  } else if constexpr (std::same_as<T, bot_model::MessageRef>) {
    budget.string("conversation");
    send_sdk_inspect(budget, value.conversation, depth + 1);
    budget.string("id");
    budget.string_value(value.id.serialized(), depth + 1);
  } else if constexpr (dd::BotRecord<T>) {
    std::apply(
        [&](auto... field) {
          ((budget.string(field.first),
            send_sdk_inspect(budget, value.*field.second, depth + 1)),
           ...);
        },
        bot_model::BotFields<T>::fields());
  } else if constexpr (dd::IsVariant<T>::value) {
    budget.string("alternative");
    budget.node(depth + 1);
    budget.string("value");
    std::visit([&](const auto &v) { send_sdk_inspect(budget, v, depth + 1); },
               value);
  } else if constexpr (dd::IsVector<T>::value) {
    for (const auto &v : value)
      send_sdk_inspect(budget, v, depth + 1);
  } else if constexpr (dd::IsOptional<T>::value) {
    if (value)
      send_sdk_inspect(budget, *value, depth);
  } else if constexpr (std::same_as<T, std::string>)
    budget.string(value);
  else if constexpr (requires { value.serialized(); })
    budget.string(value.serialized());
}

template <class T>
auto send_sdk_pack(const T &value, const ValueLimits &limits) -> Json {
  if constexpr (std::same_as<T, MessageSelector>) {
    return {{"installation", value.installation()},
            {"message_id", QuerySelectors::export_message(value, limits)}};
  } else if constexpr (std::same_as<T, CallerFileUri>)
    return {{"uri", value.uri}};
  else if constexpr (std::same_as<T, bot_model::UserRef> ||
                     std::same_as<T, bot_model::ConversationRef> ||
                     std::same_as<T, bot_model::MessageRef>)
    return Json(value);
  else if constexpr (dd::BotRecord<T>) {
    Json out = Json::object();
    std::apply(
        [&](auto... field) {
          ((out[field.first] = send_sdk_pack(value.*field.second, limits)),
           ...);
        },
        bot_model::BotFields<T>::fields());
    return out;
  } else if constexpr (dd::IsVariant<T>::value) {
    return {{"alternative", value.index()},
            {"value",
             std::visit([&](const auto &v) { return send_sdk_pack(v, limits); },
                        value)}};
  } else if constexpr (dd::IsVector<T>::value) {
    Json out = Json::array();
    for (const auto &v : value)
      out.push_back(send_sdk_pack(v, limits));
    return out;
  } else if constexpr (dd::IsOptional<T>::value) {
    return value ? send_sdk_pack(*value, limits) : Json(nullptr);
  } else
    return dd::bot_encode(value);
}

template <class T> auto send_sdk_unpack(const Json &, const ValueLimits &) -> T;
template <class T, std::size_t I = 0>
auto send_sdk_variant(const Json &value, std::size_t index,
                      const ValueLimits &limits) -> T {
  if constexpr (I == std::variant_size_v<T>)
    invalid_content();
  else {
    if (index == I)
      return T{
          std::in_place_index<I>,
          send_sdk_unpack<std::variant_alternative_t<I, T>>(value, limits)};
    return send_sdk_variant<T, I + 1>(value, index, limits);
  }
}
template <class T>
auto send_sdk_unpack(const Json &value, const ValueLimits &limits) -> T {
  if constexpr (std::same_as<T, MessageSelector>) {
    dd::require_only_keys(value, "standard selector",
                          {"installation", "message_id"});
    return QuerySelectors::import_message(
        value.at("installation").get<bot_model::BotInstallationRef>(),
        dd::bot_decode<std::int32_t>(value.at("message_id")), limits);
  } else if constexpr (std::same_as<T, CallerFileUri>) {
    dd::require_only_keys(value, "standard file", {"uri"});
    return {dd::bot_decode<std::string>(value.at("uri")), {}};
  } else if constexpr (std::same_as<T, bot_model::UserRef> ||
                       std::same_as<T, bot_model::ConversationRef> ||
                       std::same_as<T, bot_model::MessageRef>)
    return value.get<T>();
  else if constexpr (dd::BotRecord<T>) {
    const auto fields = bot_model::BotFields<T>::fields();
    if (!value.is_object() ||
        value.size() != std::tuple_size_v<decltype(fields)>)
      invalid_content();
    return std::apply(
        [&](auto... field) {
          return T{send_sdk_unpack<
              typename dd::MemberType<decltype(field.second)>::type>(
              value.at(field.first), limits)...};
        },
        fields);
  } else if constexpr (dd::IsVariant<T>::value) {
    dd::require_only_keys(value, "standard alternative",
                          {"alternative", "value"});
    return send_sdk_variant<T>(value.at("value"),
                               dd::bounded_unsigned(value.at("alternative"),
                                                    std::variant_size_v<T> - 1),
                               limits);
  } else if constexpr (dd::IsVector<T>::value) {
    if (!value.is_array())
      invalid_content();
    T out;
    out.reserve(value.size());
    for (const auto &v : value)
      out.push_back(
          send_sdk_unpack<typename dd::IsVector<T>::value_type>(v, limits));
    return out;
  } else if constexpr (dd::IsOptional<T>::value) {
    if (value.is_null())
      return std::nullopt;
    return send_sdk_unpack<typename dd::IsOptional<T>::value_type>(value,
                                                                   limits);
  } else
    return dd::bot_decode<T>(value);
}

template <class Parameters>
void send_sdk_envelope(const Parameters &parameters, const CallControl &control,
                       const ValueLimits &limits) {
  limits.validate();
  ContentBudget budget(limits);
  // Reserve both borrowed projection and generated envelope before copying.
  // Conservatively count SDK keys/variant framing on both passes.
  for (unsigned pass = 0; pass < 2; ++pass) {
    budget.node(0);
    budget.string("parameters");
    send_sdk_inspect(budget, parameters, 1);
    budget.string("control");
    budget.node(1);
    budget.string("execution");
    std::string_view mode;
    switch (control.execution) {
    case ExecutionMode::Direct:
      mode = "direct";
      break;
    case ExecutionMode::Async:
      mode = "async";
      break;
    case ExecutionMode::RateLimited:
      mode = "rate_limited";
      break;
    default:
      invalid_content();
    }
    budget.string_value(mode, 2);
    budget.string("limits");
    budget.node(2);
    for (auto name : {"maximum_parameter_bytes", "maximum_response_bytes",
                      "timeout_milliseconds"}) {
      budget.string(name);
      budget.node(3);
    }
    budget.string("correlation");
    budget.node(2);
    if (control.correlation) {
      budget.string("attempt_id");
      budget.string_value(control.correlation->attempt_id, 3);
    }
    budget.string("value_limits");
    budget.node(1);
    for (auto name : {"maximum_bytes", "maximum_nodes", "maximum_depth",
                      "maximum_text_bytes"}) {
      budget.string(name);
      budget.node(2);
    }
    budget.string("deadline_ticks");
    budget.node(1);
  }
}

template <class Parameters>
auto send_encode_parameters(const Parameters &parameters,
                            const SendCodec &codec, const ValueLimits &limits)
    -> Json {
  limits.validate();
  ContentBudget borrowed(limits);
  send_sdk_inspect(borrowed, parameters, 0);
  codec.validate(parameters.target);
  const auto parts = codec.unpack(parameters.target.id);
  if (parts.components.size() != 1 ||
      (parts.kind != "private" && parts.kind != "group"))
    invalid_content();
  if constexpr (std::same_as<Parameters, SendPrivateMsgParameters>) {
    if (parts.kind != "private")
      invalid_content();
  } else if constexpr (std::same_as<Parameters, SendGroupMsgParameters>) {
    if (parts.kind != "group")
      invalid_content();
  } else if (parameters.message_type) {
    const auto type = *parameters.message_type;
    if ((type != SendMessageType::Private && type != SendMessageType::Group) ||
        (type == SendMessageType::Private) != (parts.kind == "private"))
      invalid_content();
  }
  std::int64_t id;
  const auto &text = parts.components[0];
  const auto [end, ec] =
      std::from_chars(text.data(), text.data() + text.size(), id);
  if (ec != std::errc{} || end != text.data() + text.size() || id <= 0)
    invalid_content();
  ContentBudget native(limits);
  native.node(0);
  native.string(parts.kind == "private" ? "user_id" : "group_id");
  native.node(1);
  native.string("message");
  native.string("auto_escape");
  native.node(1);
  if constexpr (std::same_as<Parameters, SendMsgParameters>) {
    if (parameters.message_type) {
      native.string("message_type");
      native.string_value(parts.kind, 1);
    }
  }
  auto content =
      encode_composition(parameters.message, parameters.target, codec, native);
  if (content.message.is_string() &&
      parameters.auto_escape != content.auto_escape)
    invalid_content();
  Json out{{parts.kind == "private" ? "user_id" : "group_id", id},
           {"auto_escape", parameters.auto_escape},
           {"message", std::move(content.message)}};
  if constexpr (std::same_as<Parameters, SendMsgParameters>)
    if (parameters.message_type)
      out["message_type"] = parts.kind;
  ContentBudget output(limits);
  send_wire_inspect(output, out, 0);
  return out;
}

auto send_decode_data(const Json &data,
                      const bot_model::ConversationRef &target,
                      const SendCodec &codec, const ValueLimits &limits)
    -> SendMessageData {
  limits.validate();
  ContentBudget budget(limits);
  send_wire_inspect(budget, data, 0);
  if (!data.is_object())
    invalid_content();
  const auto id = dd::bot_decode<std::int32_t>(data.at("message_id"));
  // Reserve the exact genuine opaque spelling before its retained copy.
  // NativeReferenceCodec uses three length-prefixed fields, hex encoded.
  char digits[12];
  const auto [end, error] = std::to_chars(digits, digits + sizeof(digits), id);
  if (error != std::errc{})
    invalid_content();
  const std::string_view number(digits, end);
  budget.node(0);
  budget.node(1);
  budget.node(1);
  inspect_reference(budget, target, 2);
  budget.node(2);
  std::size_t opaque_length = 0;
  budget.string_part(opaque_length,
                     std::string_view("obcx1:message:").size() +
                         2 * (12 + target.installation.surface.value().size() +
                              std::string_view("message").size() +
                              number.size()));
  codec.validate(target);
  SendMessageData result{id,
                         {target, codec.make<bot_model::MessageId>(
                                      "message", {std::string(number)})}};
  validate_send_result(result, target, codec, limits);
  return result;
}
} // namespace

void validate_send_result(const SendMessageData &result,
                          const bot_model::ConversationRef &target,
                          const SendCodec &codec, const ValueLimits &limits) {
  limits.validate();
  ContentBudget budget(limits);
  budget.node(0);
  budget.node(1);
  budget.node(1);
  inspect_reference(budget, result.message.conversation, 2);
  budget.string_value(result.message.id.serialized(), 2);
  if (result.message.conversation != target)
    invalid_content();
  codec.validate(result.message);
  const auto parts = codec.unpack(result.message.id);
  if (parts.kind != "message" || parts.components.size() != 1 ||
      parts.components[0] != std::to_string(result.message_id))
    invalid_content();
}

auto encode_send_private_msg(const SendPrivateMsgParameters &parameters,
                             const SendCodec &codec, const ValueLimits &limits)
    -> Json {
  return send_encode_parameters(parameters, codec, limits);
}
auto decode_send_private_msg(const Json &data,
                             const SendPrivateMsgParameters &parameters,
                             const SendCodec &codec, const ValueLimits &limits)
    -> SendMessageData {
  return send_decode_data(data, parameters.target, codec, limits);
}
auto send_sdk_encode(const SendPrivateMsgParameters &parameters,
                     const ValueLimits &limits) -> Json {
  limits.validate();
  ContentBudget budget(limits);
  send_sdk_inspect(budget, parameters, 0);
  send_sdk_inspect(budget, parameters, 0);
  return send_sdk_pack(parameters, limits);
}
auto send_sdk_decode_send_private_msg(const Json &wire,
                                      const ValueLimits &limits)
    -> SendPrivateMsgParameters {
  limits.validate();
  ContentBudget budget(limits);
  send_wire_inspect(budget, wire, 0);
  send_wire_inspect(budget, wire, 0);
  return send_sdk_unpack<SendPrivateMsgParameters>(wire, limits);
}

auto encode_send_group_msg(const SendGroupMsgParameters &parameters,
                           const SendCodec &codec, const ValueLimits &limits)
    -> Json {
  return send_encode_parameters(parameters, codec, limits);
}
auto decode_send_group_msg(const Json &data,
                           const SendGroupMsgParameters &parameters,
                           const SendCodec &codec, const ValueLimits &limits)
    -> SendMessageData {
  return send_decode_data(data, parameters.target, codec, limits);
}
auto send_sdk_encode(const SendGroupMsgParameters &parameters,
                     const ValueLimits &limits) -> Json {
  limits.validate();
  ContentBudget budget(limits);
  send_sdk_inspect(budget, parameters, 0);
  send_sdk_inspect(budget, parameters, 0);
  return send_sdk_pack(parameters, limits);
}
auto send_sdk_decode_send_group_msg(const Json &wire, const ValueLimits &limits)
    -> SendGroupMsgParameters {
  limits.validate();
  ContentBudget budget(limits);
  send_wire_inspect(budget, wire, 0);
  send_wire_inspect(budget, wire, 0);
  return send_sdk_unpack<SendGroupMsgParameters>(wire, limits);
}

auto encode_send_msg(const SendMsgParameters &parameters,
                     const SendCodec &codec, const ValueLimits &limits)
    -> Json {
  return send_encode_parameters(parameters, codec, limits);
}
auto decode_send_msg(const Json &data, const SendMsgParameters &parameters,
                     const SendCodec &codec, const ValueLimits &limits)
    -> SendMessageData {
  return send_decode_data(data, parameters.target, codec, limits);
}
auto send_sdk_encode(const SendMsgParameters &parameters,
                     const ValueLimits &limits) -> Json {
  limits.validate();
  ContentBudget budget(limits);
  send_sdk_inspect(budget, parameters, 0);
  send_sdk_inspect(budget, parameters, 0);
  return send_sdk_pack(parameters, limits);
}
auto send_sdk_decode_send_msg(const Json &wire, const ValueLimits &limits)
    -> SendMsgParameters {
  limits.validate();
  ContentBudget budget(limits);
  send_wire_inspect(budget, wire, 0);
  send_wire_inspect(budget, wire, 0);
  return send_sdk_unpack<SendMsgParameters>(wire, limits);
}
void send_sdk_validate_envelope(const SendPrivateMsgParameters &parameters,
                                const CallControl &control,
                                const ValueLimits &limits) {
  send_sdk_envelope(parameters, control, limits);
}
void send_sdk_validate_envelope(const SendGroupMsgParameters &parameters,
                                const CallControl &control,
                                const ValueLimits &limits) {
  send_sdk_envelope(parameters, control, limits);
}
void send_sdk_validate_envelope(const SendMsgParameters &parameters,
                                const CallControl &control,
                                const ValueLimits &limits) {
  send_sdk_envelope(parameters, control, limits);
}
} // namespace obcx::onebot11::bot::standard::detail
