#include <chrono>
#include <thread>

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

template <typename T>
static std::shared_ptr<T> waitServiceDefault(int retries = 5)
{
	std::shared_ptr<T> kService = nullptr;
	const auto kServiceDesc = std::string() + T::descriptor + "/default";

	// If not declared, just return null
	if (AServiceManager_isDeclared(kServiceDesc.c_str())) {
		do {
			kService = getService<T>(kServiceDesc);
			if (kService == nullptr) {
				std::this_thread::sleep_for(std::chrono::seconds(5));
				--retries;
			} else {
				break;
			}
		} while (retries > 0);
	}
	return kService;
}
