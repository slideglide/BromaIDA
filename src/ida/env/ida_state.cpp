#include "env/ida_state.hpp"

#include <glaze/glaze.hpp>

#include <bromaida/util/log.hpp>

#include <pro.h>
#include <netnode.hpp>

static constexpr auto kNodeName = "$ BromaIDA.state";
static constexpr char kBlobTag = 'J';
constexpr int kFormat = 1;

struct Blob {
    int format = kFormat;
    std::optional<std::string> types_digest;
};

template <>
struct glz::meta<Blob> {
    using T = Blob;
    static constexpr auto value = object("__format__", &T::format, "types.digest", &T::types_digest);
};

namespace bromaida::ida {
    std::optional<std::string> load_types_digest() {
        if (!netnode::exist(kNodeName)) return {};
        netnode node;
        node.create(kNodeName);

        size_t size = 0;
        void* data = node.getblob(nullptr, &size, 0, kBlobTag);
        if (data == nullptr) return {};
        std::string json(static_cast<char const*>(data), size);
        qfree(data);
        while (!json.empty() && json.back() == '\0')
            json.pop_back();

        Blob blob;
        if (glz::read<glz::opts{.error_on_unknown_keys = false}>(blob, json) || blob.format != kFormat) return {};
        return blob.types_digest;
    }

    void save_types_digest(std::string const& digest) {
        Blob blob{.format = kFormat, .types_digest = digest};
        std::string json;
        if (glz::write_json(blob, json)) return;

        netnode node;
        node.create(kNodeName);
        node.setblob(json.data(), json.size(), 0, kBlobTag);
    }
}  // namespace bromaida::ida
