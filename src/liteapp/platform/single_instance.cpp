#include "liteapp/platform/single_instance.h"

namespace liteapp::platform {

struct SingleInstance::Impl {};

SingleInstance::SingleInstance(std::unique_ptr<Impl> impl) : impl_{std::move(impl)} {}

Result<std::optional<SingleInstance>> SingleInstance::acquire(std::string_view) {
    return std::optional<SingleInstance>{SingleInstance{std::make_unique<Impl>()}};
}

SingleInstance::~SingleInstance() = default;
SingleInstance::SingleInstance(SingleInstance&&) noexcept = default;
SingleInstance& SingleInstance::operator=(SingleInstance&&) noexcept = default;
void SingleInstance::set_activation_handler(std::function<void()>) {}
void SingleInstance::clear_activation_handler() {}

}  // namespace liteapp::platform
