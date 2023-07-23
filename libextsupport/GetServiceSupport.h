#include <type_traits>
#include <string>

#include <android/binder_interface_utils.h>
#include <android/binder_manager.h>

template <typename T, std::enable_if_t<std::is_base_of_v<ndk::ICInterface, T>, bool> = true>
static inline std::shared_ptr<T> getService(const std::string& name)
{
	auto binder = AServiceManager_checkService(name.c_str());
	if (!binder) return nullptr;
	return T::fromBinder(ndk::SpAIBinder(binder));
}

template <typename T>
static inline std::shared_ptr<T> getServiceDefault(void)
{
	return getService<T>(std::string() + T::descriptor + "/default");
}
