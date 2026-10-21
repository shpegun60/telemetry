/* Positive compile control and deliberate public bypass refusals. MIT. */
#include <integration/dynamic/DynamicClient.hpp>
namespace dy = telemetry::integration::dynamic;
namespace cl = telemetry::client;
using Client = dy::DynamicClient<2, 64>;

void probe(Client& client) noexcept
{
	(void)client;
#ifdef DYNAMIC_RAW
	(void)client.submitEncoded(cl::RequestView{}, [](const cl::ResultView&) noexcept {
	});
#endif
#ifdef DYNAMIC_CORE
	(void)client.core();
#endif
#ifdef DYNAMIC_UNWRAP
	(void)client.unwrap();
#endif
#ifdef DYNAMIC_TYPED
	(void)client.read(0, [](const auto&) noexcept {
	});
#endif
#ifdef DYNAMIC_POD
	(void)client.submit(telemetry::runtime::RuntimeEndpoint{}, {}, [](const auto&) noexcept {
	});
#endif
#ifdef DYNAMIC_FORGE
	dy::EndpointBinding endpoint{cl::Operation::FieldRead, 0, 0, 0, 0};
	(void)endpoint;
#endif
}

int main()
{
	return 0;
}
