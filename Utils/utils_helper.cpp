#include "utils_helpers.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include "Windows.h"
#endif

namespace utils {
	size_t get_page_size_helper() {
#if defined(_WIN32) || defined(_WIN64)
		SYSTEM_INFO sys_info;
		GetSystemInfo(&sys_info);
		return static_cast<size_t>(sys_info.dwPageSize);
#else
		long page_size = sysconf(_SC_PAGESIZE);
		return static_cast<size_t>(page_size);
#endif
	}
}