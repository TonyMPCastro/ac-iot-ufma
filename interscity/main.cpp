#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/signal_set.hpp>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <algorithm>
#include <cctype>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = net::ip::tcp;
using json = nlohmann::json;

struct RemoteUrl {
    bool https = false;
    std::string host;
    std::string port;
    std::string path;
};

static std::map<std::string, std::string> dot_env;

static std::string trim(const std::string& str) {
    auto begin = str.find_first_not_of(" \t\r\n");
    auto end = str.find_last_not_of(" \t\r\n");
    return (begin == std::string::npos) ? std::string() : str.substr(begin, end - begin + 1);
}

static void load_dotenv(const std::string& file_path = ".env") {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        return;
    }
    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        auto pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        std::string key = trim(line.substr(0, pos));
        std::string value = trim(line.substr(pos + 1));
        if (!key.empty() && dot_env.find(key) == dot_env.end()) {
            if (!value.empty() && value.front() == '"' && value.back() == '"' && value.size() >= 2) {
                value = value.substr(1, value.size() - 2);
            }
            dot_env[key] = value;
        }
    }
}

static std::string get_env(const char* name, const char* def = "") {
    const char* value = std::getenv(name);
    if (value) {
        return std::string(value);
    }
    auto it = dot_env.find(name);
    return (it != dot_env.end()) ? it->second : std::string(def);
}

static bool parse_url(const std::string& url, RemoteUrl& out) {
    if (url.rfind("https://", 0) == 0) {
        out.https = true;
    } else if (url.rfind("http://", 0) == 0) {
        out.https = false;
    } else {
        return false;
    }

    auto start = url.find("//") + 2;
    auto path_pos = url.find('/', start);
    if (path_pos == std::string::npos) {
        out.host = url.substr(start);
        out.path = "/";
    } else {
        out.host = url.substr(start, path_pos - start);
        out.path = url.substr(path_pos);
    }

    auto port_pos = out.host.find(':');
    if (port_pos != std::string::npos) {
        out.port = out.host.substr(port_pos + 1);
        out.host = out.host.substr(0, port_pos);
    } else {
        out.port = out.https ? "443" : "80";
    }

    if (out.path.empty()) {
        out.path = "/";
    }
    return true;
}

static std::string join_path(const std::string& a, const std::string& b) {
    if (a.empty()) return b.empty() ? std::string("/") : b;
    if (b.empty()) return a;
    if (a.back() == '/' && b.front() == '/') {
        return a + b.substr(1);
    }
    if (a.back() != '/' && b.front() != '/') {
        return a + "/" + b;
    }
    return a + b;
}

static bool send_remote_telemetry(const RemoteUrl& remote,
                                  const std::string& token,
                                  const json& payload,
                                  long& status_code,
                                  std::string& response_body,
                                  std::string& error_message) {
    try {
        net::io_context ioc;
        auto target = remote.path;
        http::request<http::string_body> req{http::verb::post, target, 11};
        req.set(http::field::host, remote.host);
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
        req.set(http::field::content_type, "application/json");
        if (!token.empty()) {
            req.set(http::field::authorization, "Bearer " + token);
        }
        req.body() = payload.dump();
        req.prepare_payload();

        if (remote.https) {
            ssl::context ctx{ssl::context::sslv23_client};
            ctx.set_default_verify_paths();
            ctx.set_verify_mode(ssl::verify_peer);
            tcp::resolver resolver{ioc};
            beast::ssl_stream<beast::tcp_stream> stream{ioc, ctx};

            auto const results = resolver.resolve(remote.host, remote.port);
            beast::get_lowest_layer(stream).connect(results);
            stream.handshake(ssl::stream_base::client);

            http::write(stream, req);
            beast::flat_buffer buffer;
            http::response<http::string_body> res;
            http::read(stream, buffer, res);

            status_code = res.result_int();
            response_body = res.body();

            beast::error_code ec;
            stream.shutdown(ec);
            if (ec && ec != net::error::eof) {
                throw beast::system_error{ec};
            }
        } else {
            tcp::resolver resolver{ioc};
            beast::tcp_stream stream{ioc};
            auto const results = resolver.resolve(remote.host, remote.port);
            stream.connect(results);
            http::write(stream, req);
            beast::flat_buffer buffer;
            http::response<http::string_body> res;
            http::read(stream, buffer, res);
            status_code = res.result_int();
            response_body = res.body();
            beast::error_code ec;
            stream.socket().shutdown(tcp::socket::shutdown_both, ec);
        }

        return true;
    } catch (const std::exception& exc) {
        error_message = exc.what();
        return false;
    }
}

int main() {
    load_dotenv();
    const auto local_host = get_env("LOCAL_BIND_HOST", "0.0.0.0");
    const auto local_port_str = get_env("LOCAL_BIND_PORT", "5000");
    const auto remote_api_url = get_env("REMOTE_API_URL", "");
    const auto remote_api_path = get_env("REMOTE_API_PATH", "/telemetry");
    const auto api_token = get_env("API_TOKEN", "");

    if (remote_api_url.empty()) {
        std::cerr << "REMOTE_API_URL não está configurado\n";
        return EXIT_FAILURE;
    }

    RemoteUrl remote;
    if (!parse_url(remote_api_url, remote)) {
        std::cerr << "REMOTE_API_URL inválida: " << remote_api_url << "\n";
        return EXIT_FAILURE;
    }
    remote.path = join_path(remote.path, remote_api_path);

    unsigned short local_port = 5000;
    try {
        local_port = static_cast<unsigned short>(std::stoi(local_port_str));
    } catch (...) {
        std::cerr << "LOCAL_BIND_PORT inválido: " << local_port_str << "\n";
        return EXIT_FAILURE;
    }

    net::io_context ioc{1};
    tcp::acceptor acceptor{ioc, {net::ip::make_address(local_host), local_port}};
    std::cout << "InterSCity C++ server ouvindo em " << local_host << ":" << local_port << "\n";
    std::cout << "Encaminhando para " << remote_api_url << remote_api_path << "\n";

    while (true) {
        try {
            tcp::socket socket{ioc};
            acceptor.accept(socket);
            beast::flat_buffer buffer;
            http::request<http::string_body> req;
            http::read(socket, buffer, req);

            http::response<http::string_body> res;
            res.version(req.version());
            res.set(http::field::server, "ac-iot-interscity-cpp");

            if (req.method() == http::verb::get && req.target() == "/health") {
                res.result(http::status::ok);
                res.set(http::field::content_type, "application/json");
                res.body() = R"({"status":"ok"})";
                res.prepare_payload();
                http::write(socket, res);
            } else if (req.method() == http::verb::post && req.target() == "/telemetry") {
                try {
                    json payload = json::parse(req.body());
                    long status_code = 0;
                    std::string remote_body;
                    std::string error_message;
                    bool sent = send_remote_telemetry(remote, api_token, payload, status_code, remote_body, error_message);
                    if (!sent) {
                        json error_json = {
                            {"error", "falha ao encaminhar"},
                            {"details", error_message}
                        };
                        res.result(http::status::bad_gateway);
                        res.set(http::field::content_type, "application/json");
                        res.body() = error_json.dump();
                    } else {
                        json response_json = {
                            {"status", "sent"},
                            {"remote_status", status_code},
                            {"remote_body", remote_body}
                        };
                        res.result(http::status::ok);
                        res.set(http::field::content_type, "application/json");
                        res.body() = response_json.dump();
                    }
                } catch (const json::parse_error& exc) {
                    res.result(http::status::bad_request);
                    res.set(http::field::content_type, "application/json");
                    res.body() = json({{"error", "JSON inválido"}, {"details", exc.what()}}).dump();
                }
                res.prepare_payload();
                http::write(socket, res);
            } else {
                res.result(http::status::not_found);
                res.set(http::field::content_type, "application/json");
                res.body() = R"({"error":"rota não encontrada"})";
                res.prepare_payload();
                http::write(socket, res);
            }

            beast::error_code ec;
            socket.shutdown(tcp::socket::shutdown_send, ec);
        } catch (const std::exception& exc) {
            std::cerr << "Erro no servidor: " << exc.what() << "\n";
        }
    }

    return EXIT_SUCCESS;
}
