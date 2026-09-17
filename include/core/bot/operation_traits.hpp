#ifndef OBCX_INCLUDE_CORE_BOT_OPERATION_TRAITS_HPP_
#define OBCX_INCLUDE_CORE_BOT_OPERATION_TRAITS_HPP_

#include "core/bot/references.hpp"

namespace obcx::bot {

// Specializations are owned by the module defining the request, not a central
// action list. A typed adapter also requires installation() and
// validate_result().
template <typename Request> struct OperationTraits;

template <typename Request, typename Result, bool SideEffecting>
struct OperationContract {
  using request_type = Request;
  using result_type = Result;
  static constexpr bool side_effecting = SideEffecting;

  [[nodiscard]] static auto action() -> const ActionId & {
    return Request::action;
  }
};

} // namespace obcx::bot

#endif // OBCX_INCLUDE_CORE_BOT_OPERATION_TRAITS_HPP_
