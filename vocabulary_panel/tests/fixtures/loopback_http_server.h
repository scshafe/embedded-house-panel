#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace vocabulary_panel::test_fixture
{

/** Loopback-only HTTP fixture with an ephemeral in-memory TLS identity. */
class LoopbackHttpServer final
{
      public:
	enum class Transport {
		http,
		https,
	};
	enum class TlsCertificateIdentity {
		loopback_ip,
		localhost_dns,
	};

	LoopbackHttpServer(
	    Transport transport, int response_status_code, std::string response_body,
	    std::string redirect_location = {},
	    TlsCertificateIdentity tls_certificate_identity = TlsCertificateIdentity::loopback_ip,
	    unsigned response_delay_ms = 0);
	~LoopbackHttpServer();

	LoopbackHttpServer(const LoopbackHttpServer &) = delete;
	LoopbackHttpServer &operator=(const LoopbackHttpServer &) = delete;

	std::string base_url() const;
	std::uint32_t accepted_connection_count() const noexcept;
	bool tls_handshake_attempted() const noexcept;
	std::string trusted_ca_certificate_pem() const;
	std::string server_error() const;

      private:
	class Implementation;
	std::unique_ptr<Implementation> implementation_;
};

} // namespace vocabulary_panel::test_fixture
