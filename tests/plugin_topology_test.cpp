#include "common/plugin_manager.hpp"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

using namespace obcx::common;

class PluginTopologyTest : public ::testing::Test {
protected:
  void SetUp() override {
    test_config_dir_ = std::filesystem::temp_directory_path() / "obcx_test";
    std::filesystem::create_directories(test_config_dir_);
  }

  void TearDown() override {
    if (std::filesystem::exists(test_config_dir_)) {
      std::filesystem::remove_all(test_config_dir_);
    }
  }

  void create_test_config(const std::string &content) {
    auto config_path = test_config_dir_ / "test_config.toml";
    std::ofstream ofs(config_path);
    ofs << content;
    ofs.close();

    auto &config_loader = ConfigLoader::instance();
    config_loader.load_config(config_path.string());
  }

  std::filesystem::path test_config_dir_;
};

TEST_F(PluginTopologyTest, NoDependencies) {
  create_test_config(R"(
[plugins.plugin_a]
enabled = true
priority = 100

[plugins.plugin_b]
enabled = true
priority = 50

[plugins.plugin_c]
enabled = true
priority = 200
)");

  PluginManager pm;
  std::vector<std::string> plugin_names = {"plugin_a", "plugin_b", "plugin_c"};
  auto sorted = pm.sort_plugins_by_priority_and_dependencies(plugin_names);

  ASSERT_EQ(sorted.size(), 3);
  EXPECT_EQ(sorted[0], "plugin_c");
  EXPECT_EQ(sorted[1], "plugin_a");
  EXPECT_EQ(sorted[2], "plugin_b");
}

TEST_F(PluginTopologyTest, LinearDependency) {
  create_test_config(R"(
[plugins.plugin_a]
enabled = true

[plugins.plugin_b]
enabled = true
required = ["plugin_a"]

[plugins.plugin_c]
enabled = true
required = ["plugin_b"]
)");

  PluginManager pm;
  std::vector<std::string> plugin_names = {"plugin_a", "plugin_b", "plugin_c"};
  auto sorted = pm.sort_plugins_by_priority_and_dependencies(plugin_names);

  ASSERT_EQ(sorted.size(), 3);
  EXPECT_EQ(sorted[0], "plugin_a");
  EXPECT_EQ(sorted[1], "plugin_b");
  EXPECT_EQ(sorted[2], "plugin_c");
}

TEST_F(PluginTopologyTest, PriorityWithDependencies) {
  create_test_config(R"(
[plugins.base]
enabled = true
priority = 50

[plugins.high_priority]
enabled = true
priority = 200
required = ["base"]

[plugins.low_priority]
enabled = true
priority = 10
required = ["base"]
)");

  PluginManager pm;
  std::vector<std::string> plugin_names = {"base", "high_priority",
                                           "low_priority"};
  auto sorted = pm.sort_plugins_by_priority_and_dependencies(plugin_names);

  ASSERT_EQ(sorted.size(), 3);
  EXPECT_EQ(sorted[0], "base");
  EXPECT_EQ(sorted[1], "high_priority");
  EXPECT_EQ(sorted[2], "low_priority");
}

TEST_F(PluginTopologyTest, CircularDependencyTwoNodes) {
  create_test_config(R"(
[plugins.plugin_a]
enabled = true
required = ["plugin_b"]

[plugins.plugin_b]
enabled = true
required = ["plugin_a"]
)");

  PluginManager pm;
  std::vector<std::string> plugin_names = {"plugin_a", "plugin_b"};
  EXPECT_THROW(pm.sort_plugins_by_priority_and_dependencies(plugin_names),
               std::runtime_error);
}

TEST_F(PluginTopologyTest, CircularDependencyThreeNodes) {
  create_test_config(R"(
[plugins.plugin_a]
enabled = true
required = ["plugin_b"]

[plugins.plugin_b]
enabled = true
required = ["plugin_c"]

[plugins.plugin_c]
enabled = true
required = ["plugin_a"]
)");

  PluginManager pm;
  std::vector<std::string> plugin_names = {"plugin_a", "plugin_b", "plugin_c"};
  EXPECT_THROW(pm.sort_plugins_by_priority_and_dependencies(plugin_names),
               std::runtime_error);
}

TEST_F(PluginTopologyTest, MissingDependency) {
  create_test_config(R"(
[plugins.plugin_a]
enabled = true
required = ["non_existent"]
)");

  PluginManager pm;
  std::vector<std::string> plugin_names = {"plugin_a"};
  EXPECT_THROW(pm.sort_plugins_by_priority_and_dependencies(plugin_names),
               std::runtime_error);
}

TEST_F(PluginTopologyTest, EmptyPluginList) {
  create_test_config("");
  PluginManager pm;
  std::vector<std::string> plugin_names = {};
  auto sorted = pm.sort_plugins_by_priority_and_dependencies(plugin_names);
  EXPECT_TRUE(sorted.empty());
}
