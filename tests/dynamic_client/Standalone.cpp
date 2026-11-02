/* No Model, reflection, Resource, transport or backend include is required. MIT. */
#include <integration/dynamic/DynamicClient.hpp>
#include <type_traits>
using Client = telemetry::integration::dynamic::DynamicClient<2, 64>;
static_assert(!std::is_copy_constructible_v<Client>);

static void dynamicCompletion(const telemetry::integration::dynamic::DynamicResult&) noexcept
{}

extern "C" bool dynamic_submit(Client& client,
                               const telemetry::integration::dynamic::EndpointBinding& endpoint,
                               telemetry::client::Input payload) noexcept
{
	return bool(client.submit(endpoint, payload, dynamicCompletion));
}

extern "C" bool dynamic_handoff(Client& client, std::uint32_t session,
                                std::uint32_t request) noexcept
{
	client.process(0);
	return client.prepareHandoff(session, request);
}

int main()
{
	return sizeof(Client) > sizeof(void*) ? 0 : 1;
}
