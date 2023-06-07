#include <type_traits>

#include <android/binder_interface_utils.h>
#include <android/binder_manager.h>

template <typename T, std::enable_if_t<std::is_base_of_v<ndk::ICInterface, T>, bool> = true>
static inline std::shared_ptr<T> getService(const char* name)
{
	auto binder = AServiceManager_checkService(name);
	if (!binder) return nullptr;
	return T::fromBinder(ndk::SpAIBinder(binder));
}
