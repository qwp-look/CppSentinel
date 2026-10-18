#include "test_main.hpp"

#include "cppsentinel/core/TargetEnumerator.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace cppsentinel;
namespace fs = std::filesystem;

namespace
{

fs::path targetRoot()
{
    return fs::temp_directory_path() / "cppsentinel_targets";
}

void writeFile(const fs::path& path)
{
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    out << "// generated for test\n";
}

} // namespace

TEST(target_single_file_is_kept_as_is)
{
    const auto root = targetRoot();
    const auto file = root / "single.cpp";
    writeFile(file);

    const auto targets = enumerateTargets(file, root);
    REQUIRE(targets.files.size() == 1);
    REQUIRE(targets.files[0] == file);
}

TEST(target_directory_walks_cpp_files_only)
{
    const auto root = targetRoot() / "project";
    writeFile(root / "src" / "b.cpp");
    writeFile(root / "src" / "a.hpp");
    writeFile(root / "src" / "notes.txt");
    writeFile(root / "include" / "c.h");

    const auto targets = enumerateTargets(root, root);
    REQUIRE(targets.files.size() == 3);
    // Sorted, deterministic order.
    REQUIRE(targets.files[0].filename().string() == "c.h");
    REQUIRE(targets.files[1].filename().string() == "a.hpp");
    REQUIRE(targets.files[2].filename().string() == "b.cpp");
}

TEST(target_directory_skips_build_and_hidden_directories)
{
    const auto root = targetRoot() / "skip";
    writeFile(root / "src" / "kept.cpp");
    writeFile(root / "build" / "generated.cpp");
    writeFile(root / "build-debug" / "generated2.cpp");
    writeFile(root / ".cache" / "hidden.cpp");

    const auto targets = enumerateTargets(root, root);
    REQUIRE(targets.files.size() == 1);
    REQUIRE(targets.files[0].filename().string() == "kept.cpp");
}

TEST(target_directory_applies_exclude_globs)
{
    const auto root = targetRoot() / "exclude";
    writeFile(root / "src" / "kept.cpp");
    writeFile(root / "third_party" / "vendored.cpp");
    writeFile(root / "src" / "generated_a.cpp");

    const auto targets =
        enumerateTargets(root, root, {"third_party/**", "**/generated_*"});
    REQUIRE(targets.files.size() == 1);
    REQUIRE(targets.files[0].filename().string() == "kept.cpp");
    REQUIRE(targets.excluded == 2);
}

TEST(target_missing_path_yields_nothing)
{
    const auto targets = enumerateTargets(targetRoot() / "does-not-exist",
                                          targetRoot());
    REQUIRE(targets.files.empty());
}
