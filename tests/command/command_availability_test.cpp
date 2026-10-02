#include "core/actor/command_availability.hpp"

#include <gtest/gtest.h>

namespace {
using namespace obcx::command;

TEST(CommandAvailabilityTest, ExactInstallationGroupAndConversation) {
  const GroupScope scope{"qq", "primary", "42", TopicSelection::None,
                         std::nullopt};
  Subject subject{"qq", "primary", ConversationKind::Group,
                  "42", "7",       std::nullopt};
  EXPECT_TRUE(valid_scope(scope));
  EXPECT_TRUE(matches(scope, subject));
  subject.bot = "secondary";
  EXPECT_FALSE(matches(scope, subject));
  subject.bot = "primary";
  subject.group_id = "43";
  EXPECT_FALSE(matches(scope, subject));
  subject.group_id = "42";
  subject.conversation = ConversationKind::Private;
  EXPECT_FALSE(matches(scope, subject));
  EXPECT_FALSE(matches(GroupScopes{}, subject));
}

TEST(CommandAvailabilityTest, ExplicitTopicSelectorsDoNotConflateContexts) {
  GroupScope scope{"telegram", "primary", "-42", TopicSelection::None,
                   std::nullopt};
  Subject subject{"telegram", "primary", ConversationKind::Group,
                  "-42",      "7",       std::nullopt};
  EXPECT_TRUE(matches(scope, subject));
  subject.topic_id = 10;
  EXPECT_FALSE(matches(scope, subject));
  scope.topic = TopicSelection::Exact;
  scope.topic_id = 10;
  EXPECT_TRUE(matches(scope, subject));
  subject.topic_id = 11;
  EXPECT_FALSE(matches(scope, subject));
  subject.topic_id.reset();
  EXPECT_FALSE(matches(scope, subject));
  scope.topic = TopicSelection::Any;
  scope.topic_id.reset();
  EXPECT_TRUE(matches(scope, subject));
  subject.topic_id = 11;
  EXPECT_TRUE(matches(scope, subject));
  subject.topic_id = 0;
  EXPECT_FALSE(matches(scope, subject));
}

TEST(CommandAvailabilityTest, RejectsMalformedScopesWithoutGrantingAccess) {
  const GroupScopes invalid{
      {"qq", "primary", "42", TopicSelection::Any, std::nullopt},
      {"qq", "primary", "42", TopicSelection::Exact, 1},
      {"qq", "primary", "42", TopicSelection::None, 1},
      {"qq", "primary", "", TopicSelection::None, std::nullopt},
      {"qq", "primary", "4\n2", TopicSelection::None, std::nullopt},
      {"telegram", "primary", "-42", TopicSelection::Exact, 0},
      {"telegram", "primary", "-42", TopicSelection::Exact, std::nullopt},
      {"telegram", "primary", "-42", TopicSelection::Any, 10},
      {"telegram", "", "-42", TopicSelection::None, std::nullopt},
      {"telegram", "primary", std::string(1025, '1'), TopicSelection::None,
       std::nullopt},
      {"unknown", "primary", "42", TopicSelection::None, std::nullopt},
      {"telegram", "primary", "42", static_cast<TopicSelection>(255),
       std::nullopt},
  };
  for (const auto &scope : invalid) {
    EXPECT_FALSE(valid_scope(scope));
    EXPECT_FALSE(matches(scope, Subject{scope.platform, scope.bot,
                                        ConversationKind::Group, scope.group_id,
                                        "7", scope.topic_id}));
  }
}
} // namespace
