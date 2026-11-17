#include "ida_state.hpp"

#include <glaze/glaze.hpp>

#include <bromaida/log.hpp>

#include <pro.h>
#include <netnode.hpp>

static constexpr auto kNodeName = "$ BromaIDA.state";
static constexpr char kBlobTag = 'J';  // private label of the blob array holding the JSON
constexpr int kFormat = 1;

namespace {

struct Blob {
    int format = kFormat;
    std::optional<std::string> types_digest;
    std::optional<std::string> types_platform;
};
}  // namespace

template <>
struct glz::meta<Blob> {
    using T = Blob;
    static constexpr auto value =
        object("__format__", &T::format, "types.digest", &T::types_digest, "types.platform", &T::types_platform);
};

namespace bromaida::ida {

IdbState IdbState::load() {
    if (!netnode::exist(kNodeName)) return {};
    netnode node;
    node.create(kNodeName);

    size_t size = 0;
    void* data = node.getblob(nullptr, &size, 0, kBlobTag);  // allocated for us
    if (data == nullptr) return {};
    std::string json(static_cast<char const*>(data), size);
    qfree(data);
    while (!json.empty() && json.back() == '\0')
        json.pop_back();

    Blob blob;
    if (glz::read<glz::opts{.error_on_unknown_keys = false}>(blob, json) || blob.format != kFormat) return {};
    return {blob.types_digest, blob.types_platform};
}

void IdbState::save() const {
    Blob blob{.format = kFormat, .types_digest = types_digest, .types_platform = types_platform};
    std::string json;
    if (glz::write_json(blob, json)) return;

    netnode node;
    node.create(kNodeName);
    node.setblob(json.data(), json.size(), 0, kBlobTag);
}

void IdbState::clear() {
    if (!netnode::exist(kNodeName)) return;
    netnode node;
    node.create(kNodeName);
    node.kill();
}

}  // namespace bromaida::ida
