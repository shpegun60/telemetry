/* Borrowed lifetime rejection probes. MIT. */
#include "Fixture.hpp"
using namespace fixture;
using Packed = std::remove_cv_t<decltype(packed)>;
using Provider = std::remove_cv_t<decltype(packedFile)>;
using Values = std::remove_cv_t<decltype(values)>;

struct Proxy {
	operator ts::Workspace&() const noexcept
	{
		return workspace;
	}
};
#if CASE == 1
rs::DescriptorFile bad{Packed{}};
#elif CASE == 2
Provider bad{Packed{}};
#elif CASE == 3
Provider bad{{}};
#elif CASE == 4
rs::DescriptorFile bad{rs::Descriptor{model}};
#elif CASE == 5
using Streaming = std::remove_cv_t<decltype(streamedFile)>;
Streaming bad{rs::Descriptor{model}};
#elif CASE == 6
rs::ValuesFile bad{descriptor, ts::Workspace{{}}};
#elif CASE == 7
Values bad{descriptor, ts::Workspace{{}}};
#elif CASE == 8
Values bad{descriptor, {}};
#elif CASE == 9
rs::ValuesFile bad{descriptor, Proxy{}};
#elif CASE == 10
Proxy proxy;
Values bad{descriptor, proxy};
#elif CASE == 11
const ts::Workspace constant{{}};
Values bad{descriptor, constant};
#elif CASE == 12
auto bad = resource::file("/values", Values{descriptor, workspace});
#endif
