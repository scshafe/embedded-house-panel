#include "fixtures/loopback_http_server.h"

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

namespace vocabulary_panel::test_fixture
{
namespace
{

std::string status_reason(const int status_code)
{
	switch (status_code) {
	case 200:
		return "OK";
	case 302:
		return "Found";
	default:
		throw std::invalid_argument("TEST_FIXTURE.HTTP_STATUS_UNSUPPORTED");
	}
}

std::string socket_error(const std::string &operation)
{
	return operation + ": " + std::strerror(errno);
}

void add_certificate_extension(X509 *certificate, const int extension_nid,
			       const char *extension_value)
{
	X509_EXTENSION *extension = X509V3_EXT_conf_nid(nullptr, nullptr, extension_nid,
							const_cast<char *>(extension_value));
	if (extension == nullptr)
		throw std::runtime_error("TEST_FIXTURE.TLS_EXTENSION_CREATE_FAILED");
	const int add_result = X509_add_ext(certificate, extension, -1);
	X509_EXTENSION_free(extension);
	if (add_result != 1)
		throw std::runtime_error("TEST_FIXTURE.TLS_EXTENSION_ADD_FAILED");
}

std::string certificate_pem(X509 *certificate)
{
	std::unique_ptr<BIO, decltype(&BIO_free)> certificate_bio(BIO_new(BIO_s_mem()), BIO_free);
	if (!certificate_bio || PEM_write_bio_X509(certificate_bio.get(), certificate) != 1)
		throw std::runtime_error("TEST_FIXTURE.TLS_CERTIFICATE_PEM_FAILED");
	BUF_MEM *certificate_buffer = nullptr;
	BIO_get_mem_ptr(certificate_bio.get(), &certificate_buffer);
	if (certificate_buffer == nullptr || certificate_buffer->data == nullptr ||
	    certificate_buffer->length == 0)
		throw std::runtime_error("TEST_FIXTURE.TLS_CERTIFICATE_PEM_EMPTY");
	return {certificate_buffer->data, certificate_buffer->length};
}

} // namespace

class LoopbackHttpServer::Implementation final
{
      public:
	Implementation(const Transport transport, const int response_status_code,
		       std::string response_body, std::string redirect_location,
		       const TlsCertificateIdentity tls_certificate_identity,
		       const unsigned response_delay_ms)
	    : transport_(transport), response_status_code_(response_status_code),
	      response_body_(std::move(response_body)),
	      redirect_location_(std::move(redirect_location)),
	      response_delay_ms_(response_delay_ms)
	{
		status_reason(response_status_code_);
		if (response_status_code_ == 302 && redirect_location_.empty())
			throw std::invalid_argument("TEST_FIXTURE.REDIRECT_LOCATION_REQUIRED");
		if (transport_ == Transport::https)
			initialize_tls(tls_certificate_identity);
		initialize_listening_socket();
		server_thread_ = std::thread([this] { serve(); });
	}

	~Implementation()
	{
		stop_requested_.store(true);
		if (listening_socket_ >= 0)
			::shutdown(listening_socket_, SHUT_RDWR);
		if (server_thread_.joinable())
			server_thread_.join();
		if (listening_socket_ >= 0)
			::close(listening_socket_);
		if (tls_context_ != nullptr)
			SSL_CTX_free(tls_context_);
	}

	std::string base_url() const
	{
		return std::string(transport_ == Transport::https ? "https" : "http") +
		       "://127.0.0.1:" + std::to_string(port_);
	}

	std::uint32_t accepted_connection_count() const noexcept
	{
		return accepted_connection_count_.load();
	}

	bool tls_handshake_attempted() const noexcept { return tls_handshake_attempted_.load(); }

	std::string trusted_ca_certificate_pem() const { return trusted_ca_certificate_pem_; }

	std::string server_error() const
	{
		std::lock_guard<std::mutex> lock(error_mutex_);
		return server_error_;
	}

      private:
	void initialize_tls(const TlsCertificateIdentity tls_certificate_identity)
	{
		std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> tls_context(
		    SSL_CTX_new(TLS_server_method()), SSL_CTX_free);
		if (!tls_context)
			throw std::runtime_error("TEST_FIXTURE.TLS_CONTEXT_CREATE_FAILED");
		if (SSL_CTX_set_min_proto_version(tls_context.get(), TLS1_2_VERSION) != 1)
			throw std::runtime_error("TEST_FIXTURE.TLS_MINIMUM_VERSION_FAILED");

		std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> key_context(
		    EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr), EVP_PKEY_CTX_free);
		EVP_PKEY *generated_private_key = nullptr;
		if (!key_context || EVP_PKEY_keygen_init(key_context.get()) <= 0 ||
		    EVP_PKEY_CTX_set_ec_paramgen_curve_nid(key_context.get(),
							   NID_X9_62_prime256v1) <= 0 ||
		    EVP_PKEY_keygen(key_context.get(), &generated_private_key) <= 0) {
			throw std::runtime_error("TEST_FIXTURE.TLS_PRIVATE_KEY_CREATE_FAILED");
		}
		std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> private_key(
		    generated_private_key, EVP_PKEY_free);

		std::unique_ptr<X509, decltype(&X509_free)> certificate(X509_new(), X509_free);
		if (!certificate || X509_set_version(certificate.get(), 2) != 1 ||
		    ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()), 1) != 1 ||
		    X509_gmtime_adj(X509_get_notBefore(certificate.get()), -60) == nullptr ||
		    X509_gmtime_adj(X509_get_notAfter(certificate.get()), 3600) == nullptr ||
		    X509_set_pubkey(certificate.get(), private_key.get()) != 1) {
			throw std::runtime_error("TEST_FIXTURE.TLS_CERTIFICATE_CREATE_FAILED");
		}

		X509_NAME *subject = X509_get_subject_name(certificate.get());
		const unsigned char common_name[] = "localhost";
		if (subject == nullptr ||
		    X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC, common_name, -1, -1,
					       0) != 1 ||
		    X509_set_issuer_name(certificate.get(), subject) != 1) {
			throw std::runtime_error("TEST_FIXTURE.TLS_SUBJECT_CREATE_FAILED");
		}

		add_certificate_extension(certificate.get(), NID_basic_constraints,
					  "critical,CA:TRUE");
		add_certificate_extension(certificate.get(), NID_key_usage,
					  "critical,digitalSignature,keyCertSign");
		add_certificate_extension(certificate.get(), NID_ext_key_usage, "serverAuth");
		add_certificate_extension(certificate.get(), NID_subject_alt_name,
					  tls_certificate_identity ==
						  TlsCertificateIdentity::loopback_ip
					      ? "IP:127.0.0.1"
					      : "DNS:localhost");
		if (X509_sign(certificate.get(), private_key.get(), EVP_sha256()) <= 0 ||
		    SSL_CTX_use_certificate(tls_context.get(), certificate.get()) != 1 ||
		    SSL_CTX_use_PrivateKey(tls_context.get(), private_key.get()) != 1 ||
		    SSL_CTX_check_private_key(tls_context.get()) != 1) {
			throw std::runtime_error("TEST_FIXTURE.TLS_IDENTITY_INSTALL_FAILED");
		}
		trusted_ca_certificate_pem_ = certificate_pem(certificate.get());
		tls_context_ = tls_context.release();
	}

	void initialize_listening_socket()
	{
		listening_socket_ = ::socket(AF_INET, SOCK_STREAM, 0);
		if (listening_socket_ < 0)
			throw std::runtime_error(socket_error("socket"));
		const int reuse_address = 1;
		if (::setsockopt(listening_socket_, SOL_SOCKET, SO_REUSEADDR, &reuse_address,
				 sizeof(reuse_address)) != 0)
			throw std::runtime_error(socket_error("setsockopt"));

		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		address.sin_port = htons(0);
		if (::bind(listening_socket_, reinterpret_cast<sockaddr *>(&address),
			   sizeof(address)) != 0)
			throw std::runtime_error(socket_error("bind"));
		if (::listen(listening_socket_, 4) != 0)
			throw std::runtime_error(socket_error("listen"));
		socklen_t address_size = sizeof(address);
		if (::getsockname(listening_socket_, reinterpret_cast<sockaddr *>(&address),
				  &address_size) != 0)
			throw std::runtime_error(socket_error("getsockname"));
		port_ = ntohs(address.sin_port);
	}

	void serve()
	{
		pollfd descriptor{listening_socket_, POLLIN, 0};
		while (!stop_requested_.load()) {
			const int poll_result = ::poll(&descriptor, 1, 100);
			if (poll_result == 0)
				continue;
			if (poll_result < 0) {
				if (errno != EINTR && !stop_requested_.load())
					set_server_error(socket_error("poll"));
				continue;
			}
			if ((descriptor.revents & POLLIN) == 0)
				continue;
			const int connection = ::accept(listening_socket_, nullptr, nullptr);
			if (connection < 0) {
				if (!stop_requested_.load())
					set_server_error(socket_error("accept"));
				continue;
			}
			accepted_connection_count_.fetch_add(1);
			serve_connection(connection);
			::close(connection);
			break;
		}
	}

	void serve_connection(const int connection)
	{
		if (transport_ == Transport::https) {
			SSL *tls = SSL_new(tls_context_);
			if (tls == nullptr) {
				set_server_error("SSL_new failed");
				return;
			}
			SSL_set_fd(tls, connection);
			tls_handshake_attempted_.store(true);
			if (SSL_accept(tls) == 1) {
				const std::string request = read_tls_request(tls);
				if (!request.empty())
					write_tls_response(tls);
				SSL_shutdown(tls);
			}
			SSL_free(tls);
			return;
		}
		const std::string request = read_plain_request(connection);
		if (!request.empty())
			write_plain_response(connection);
	}

	std::string response() const
	{
		std::ostringstream output;
		output << "HTTP/1.1 " << response_status_code_ << ' '
		       << status_reason(response_status_code_) << "\r\n"
		       << "Connection: close\r\n"
		       << "Content-Type: application/json\r\n"
		       << "Content-Length: " << response_body_.size() << "\r\n";
		if (!redirect_location_.empty())
			output << "Location: " << redirect_location_ << "\r\n";
		output << "\r\n" << response_body_;
		return output.str();
	}

	static std::string read_plain_request(const int connection)
	{
		std::string request;
		char buffer[1024];
		while (request.find("\r\n\r\n") == std::string::npos) {
			const ssize_t bytes_read = ::recv(connection, buffer, sizeof(buffer), 0);
			if (bytes_read <= 0)
				break;
			request.append(buffer, static_cast<std::size_t>(bytes_read));
		}
		return request;
	}

	static std::string read_tls_request(SSL *tls)
	{
		std::string request;
		char buffer[1024];
		while (request.find("\r\n\r\n") == std::string::npos) {
			const int bytes_read = SSL_read(tls, buffer, sizeof(buffer));
			if (bytes_read <= 0)
				break;
			request.append(buffer, static_cast<std::size_t>(bytes_read));
		}
		return request;
	}

	void write_plain_response(const int connection) const
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(response_delay_ms_));
		const std::string value = response();
		std::size_t offset = 0;
		while (offset < value.size()) {
			const ssize_t bytes_written =
			    ::send(connection, value.data() + offset, value.size() - offset, 0);
			if (bytes_written <= 0)
				return;
			offset += static_cast<std::size_t>(bytes_written);
		}
	}

	void write_tls_response(SSL *tls) const
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(response_delay_ms_));
		const std::string value = response();
		std::size_t offset = 0;
		while (offset < value.size()) {
			const int bytes_written = SSL_write(
			    tls, value.data() + offset, static_cast<int>(value.size() - offset));
			if (bytes_written <= 0)
				return;
			offset += static_cast<std::size_t>(bytes_written);
		}
	}

	void set_server_error(std::string error)
	{
		std::lock_guard<std::mutex> lock(error_mutex_);
		server_error_ = std::move(error);
	}

	Transport transport_;
	int response_status_code_;
	std::string response_body_;
	std::string redirect_location_;
	unsigned response_delay_ms_;
	int listening_socket_{-1};
	std::uint16_t port_{0};
	SSL_CTX *tls_context_{nullptr};
	std::string trusted_ca_certificate_pem_;
	std::thread server_thread_;
	std::atomic<bool> stop_requested_{false};
	std::atomic<std::uint32_t> accepted_connection_count_{0};
	std::atomic<bool> tls_handshake_attempted_{false};
	mutable std::mutex error_mutex_;
	std::string server_error_;
};

LoopbackHttpServer::LoopbackHttpServer(const Transport transport, const int response_status_code,
				       std::string response_body, std::string redirect_location,
				       const TlsCertificateIdentity tls_certificate_identity,
				       const unsigned response_delay_ms)
    : implementation_(std::make_unique<Implementation>(
	  transport, response_status_code, std::move(response_body), std::move(redirect_location),
	  tls_certificate_identity, response_delay_ms))
{
}

LoopbackHttpServer::~LoopbackHttpServer() = default;

std::string LoopbackHttpServer::base_url() const { return implementation_->base_url(); }

std::uint32_t LoopbackHttpServer::accepted_connection_count() const noexcept
{
	return implementation_->accepted_connection_count();
}

bool LoopbackHttpServer::tls_handshake_attempted() const noexcept
{
	return implementation_->tls_handshake_attempted();
}

std::string LoopbackHttpServer::trusted_ca_certificate_pem() const
{
	return implementation_->trusted_ca_certificate_pem();
}

std::string LoopbackHttpServer::server_error() const { return implementation_->server_error(); }

} // namespace vocabulary_panel::test_fixture
