#include <bromaida/files.hpp>

#include <algorithm>

#include <asp/iter.hpp>
#include <fmt/format.h>

#include <bromaida/text.hpp>

namespace bromaida {
    geode::Result<std::vector<Path>, std::string> list_files(PathView folder, std::string_view extension) {
        auto listing = asp::fs::iterDir(folder);
        if (listing.isErr()) {
            return geode::Err(fmt::format("cannot read '{}': {}", folder.string(), listing.unwrapErr().message()));
        }

        auto files = std::move(listing)
                         .unwrap()
                         .filter([](auto const& item) { return item.isOk(); })
                         .map([](auto&& item) { return std::move(item).unwrap(); })
                         .filter([&](asp::fs::DirectoryEntry const& entry) {
                             auto found = entry.path.extension();
                             return !entry.isDir() && found && cpp::lower_ascii(*found) == extension;
                         })
                         .map([](asp::fs::DirectoryEntry const& entry) { return entry.path; })
                         .collect<std::vector<Path>>();
        std::ranges::sort(files);
        return geode::Ok(std::move(files));
    }

    geode::Result<void, std::string> write_atomic(PathView file, std::string_view content) {
        if (auto parent = file.parent()) {
            if (auto made = asp::fs::createDirAll(*parent); made.isErr()) {
                return geode::Err(fmt::format("cannot create {}: {}", parent->string(), made.unwrapErr().message()));
            }
        }

        auto temp = file.toPath();
        temp += ".tmp";
        if (auto written = asp::fs::write(temp, content); written.isErr()) {
            return geode::Err(fmt::format("cannot write {}: {}", temp.string(), written.unwrapErr().message()));
        }
        if (auto renamed = asp::fs::rename(temp, file); renamed.isErr()) {
            return geode::Err(fmt::format("cannot replace {}: {}", file.string(), renamed.unwrapErr().message()));
        }
        return geode::Ok();
    }
}  // namespace bromaida
