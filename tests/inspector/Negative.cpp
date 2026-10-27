/* Positive construction control and deliberate public API refusals. MIT. */
#include <integration/inspector/InspectorModel.hpp>

namespace cl = telemetry::client;
namespace im = telemetry::integration::inspector;

class IdleBackend final : public cl::Backend {
public:
	cl::Status start(cl::RequestView, std::uint32_t) noexcept override
	{
		return cl::Status::NotAdmitted;
	}

	void process(std::uint32_t) noexcept override
	{}

	bool completed() const noexcept override
	{
		return false;
	}

	cl::ResultView result() const noexcept override
	{
		return {};
	}

	void releaseResult() noexcept override
	{}

	void cancel(cl::Status) noexcept override
	{}

	bool ready() const noexcept override
	{
		return false;
	}

	bool recoveryRequired() const noexcept override
	{
		return false;
	}

	std::uint32_t session() const noexcept override
	{
		return 0;
	}

	bool openSession(std::uint32_t) noexcept override
	{
		return false;
	}

	bool confirmSessionClosed(std::uint32_t) noexcept override
	{
		return false;
	}

	void close() noexcept override
	{}

	cl::Packet nextPacket() noexcept override
	{
		return {};
	}

	bool prepareHandoff(std::uint32_t, std::uint32_t) noexcept override
	{
		return false;
	}

	bool onSendAccepted(std::uint32_t, std::uint32_t) noexcept override
	{
		return false;
	}

	bool onReply(cl::Input, std::uint32_t, std::uint32_t) noexcept override
	{
		return false;
	}
};

int main()
{
	IdleBackend backend;
	const cl::Clock clock{nullptr, +[](void*) noexcept {
		                      return std::uint32_t{0};
	                      }};
	im::InspectorModel<2, 64, 64> model{backend, clock};
	telemetry::integration::dynamic::EndpointBinding field;
	(void)field;
#ifdef INSPECTOR_WRITE
	(void)model.writeField(field, cl::Input{}, [](const auto&) noexcept {
	});
#endif
#ifdef INSPECTOR_TYPED_WRITE
	(void)model.write(0, std::uint32_t{0}, [](const auto&) noexcept {
	});
#endif
#ifdef INSPECTOR_COMMAND
	(void)model.command(field, cl::Input{}, [](const auto&) noexcept {
	});
#endif
#ifdef INSPECTOR_SERVICE
	(void)model.service(field, cl::Input{}, [](const auto&) noexcept {
	});
#endif
#ifdef INSPECTOR_SUBMIT
	(void)model.submit(field, cl::Input{}, [](const auto&) noexcept {
	});
#endif
#ifdef INSPECTOR_RAW
	(void)model.submitEncoded(cl::RequestView{}, [](const auto&) noexcept {
	});
#endif
#ifdef INSPECTOR_CORE
	(void)model.core();
#endif
#ifdef INSPECTOR_UNWRAP
	(void)model.unwrap();
#endif
#ifdef INSPECTOR_CLIENT
	(void)model.client();
#endif
#ifdef INSPECTOR_BACKEND
	(void)model.backend();
#endif
#ifdef INSPECTOR_FORGE
	im::NodeHandle forged;
	forged.owner_ = &model;
#endif
	model.close();
	model.process(0);
	return model.drained() ? 0 : 1;
}
