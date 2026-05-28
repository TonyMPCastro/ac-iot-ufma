#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <mosquitto.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;
using json = nlohmann::json;

struct HttpUrl {
    std::string host;
    std::string port;
    std::string base_path;
};

struct HttpResponse {
    int status = 0;
    std::string body;
};

struct CapabilityConfig {
    std::string name;
    std::string description;
};

struct RoomConfig {
    std::string id;
    std::string uuid;
    std::string description;
    double lat;
    double lon;
};

static std::map<std::string, std::string> dot_env;
static HttpUrl cataloguer_url;
static HttpUrl adaptor_url;
static HttpUrl collector_url;
static std::string mqtt_topic;

static const std::vector<CapabilityConfig> capabilities = {
    {"temperatura", "Temperatura da sala"},
    {"umidade", "Umidade relativa da sala"},
    {"luminosidade", "Luminosidade da sala"},
    {"presenca", "Presenca detectada na sala"},
    {"status_ac", "Estado do ar-condicionado"},
    {"setpoint_ac", "Setpoint do ar-condicionado"},
    {"status_luz", "Estado da iluminacao"},
    {"modo_ac", "Modo de automacao do ar-condicionado"}
};

static const std::map<std::string, RoomConfig> rooms = {
    {"sala01", {"sala01", "00000000-0000-4000-8000-000000000101", "Sala 01 - AC IoT UFMA", -2.5589, -44.3095}},
    {"sala02", {"sala02", "00000000-0000-4000-8000-000000000102", "Sala 02 - AC IoT UFMA", -2.5588, -44.3094}},
    {"sala03", {"sala03", "00000000-0000-4000-8000-000000000103", "Sala 03 - AC IoT UFMA", -2.5587, -44.3093}}
};

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
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
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

static bool parse_http_url(const std::string& url, HttpUrl& out) {
    if (url.rfind("http://", 0) != 0) {
        return false;
    }

    auto start = url.find("//") + 2;
    auto path_pos = url.find('/', start);
    std::string host_port;
    if (path_pos == std::string::npos) {
        host_port = url.substr(start);
        out.base_path = "/";
    } else {
        host_port = url.substr(start, path_pos - start);
        out.base_path = url.substr(path_pos);
    }

    auto port_pos = host_port.find(':');
    if (port_pos == std::string::npos) {
        out.host = host_port;
        out.port = "80";
    } else {
        out.host = host_port.substr(0, port_pos);
        out.port = host_port.substr(port_pos + 1);
    }

    return !out.host.empty() && !out.port.empty();
}

static std::string join_path(const std::string& a, const std::string& b) {
    if (a.empty() || a == "/") {
        return b.empty() ? "/" : b;
    }
    if (b.empty() || b == "/") {
        return a;
    }
    if (a.back() == '/' && b.front() == '/') {
        return a + b.substr(1);
    }
    if (a.back() != '/' && b.front() != '/') {
        return a + "/" + b;
    }
    return a + b;
}

static HttpResponse send_http(const HttpUrl& url,
                              http::verb method,
                              const std::string& path,
                              const json* payload = nullptr) {
    HttpResponse result;

    try {
        net::io_context ioc;
        tcp::resolver resolver{ioc};
        beast::tcp_stream stream{ioc};
        stream.expires_after(std::chrono::seconds(10));

        auto const resolved = resolver.resolve(url.host, url.port);
        stream.connect(resolved);

        http::request<http::string_body> req{method, join_path(url.base_path, path), 11};
        req.set(http::field::host, url.host + ":" + url.port);
        req.set(http::field::user_agent, "ac-iot-interscity-bridge/" BOOST_BEAST_VERSION_STRING);
        req.set(http::field::accept, "application/json");

        if (payload != nullptr) {
            req.set(http::field::content_type, "application/json");
            req.body() = payload->dump();
            req.prepare_payload();
        }

        http::write(stream, req);

        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);

        result.status = res.result_int();
        result.body = res.body();

        beast::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);
    } catch (const std::exception& exc) {
        result.status = 0;
        result.body = exc.what();
    }

    return result;
}

static bool is_success(int status) {
    return status >= 200 && status < 300;
}

static std::string iso8601_utc(std::time_t timestamp) {
    std::tm tm_value{};
#if defined(_WIN32)
    gmtime_s(&tm_value, &timestamp);
#else
    gmtime_r(&timestamp, &tm_value);
#endif
    std::ostringstream out;
    out << std::put_time(&tm_value, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

static std::string timestamp_from_payload(const json& payload) {
    if (payload.contains("timestamp")) {
        try {
            if (payload["timestamp"].is_number_integer()) {
                return iso8601_utc(static_cast<std::time_t>(payload["timestamp"].get<long long>()));
            }
            if (payload["timestamp"].is_number_float()) {
                return iso8601_utc(static_cast<std::time_t>(payload["timestamp"].get<double>()));
            }
        } catch (...) {
        }
    }

    return iso8601_utc(std::time(nullptr));
}

static std::string room_from_topic(const std::string& topic) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    std::size_t end = 0;
    while ((end = topic.find('/', start)) != std::string::npos) {
        parts.push_back(topic.substr(start, end - start));
        start = end + 1;
    }
    parts.push_back(topic.substr(start));

    return parts.size() >= 2 ? parts[1] : "";
}

static void wait_for_cataloguer() {
    for (int attempt = 1; attempt <= 120; ++attempt) {
        auto response = send_http(cataloguer_url, http::verb::get, "/capabilities");
        if (response.status == 200) {
            std::cout << "Cataloguer disponivel em " << cataloguer_url.host << ":" << cataloguer_url.port << "\n";
            return;
        }

        std::cerr << "Aguardando Resource Cataloguer (" << attempt
                  << "/120): status=" << response.status
                  << " body=" << response.body << "\n";
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    throw std::runtime_error("Resource Cataloguer nao ficou disponivel a tempo");
}

static void wait_for_data_collector() {
    for (int attempt = 1; attempt <= 120; ++attempt) {
        auto response = send_http(collector_url, http::verb::get, "/resources/data");
        if (response.status == 200) {
            std::cout << "Data Collector disponivel em " << collector_url.host << ":" << collector_url.port << "\n";
            return;
        }

        std::cerr << "Aguardando Data Collector (" << attempt
                  << "/120): status=" << response.status
                  << " body=" << response.body << "\n";
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    throw std::runtime_error("Data Collector nao ficou disponivel a tempo");
}

static bool ensure_capability(const CapabilityConfig& capability) {
    for (int attempt = 1; attempt <= 30; ++attempt) {
        auto get_response = send_http(cataloguer_url, http::verb::get, "/capabilities/" + capability.name);
        if (get_response.status == 200) {
            std::cout << "Capability ja cadastrada: " << capability.name << "\n";
            return true;
        }

        if (get_response.status == 0 || get_response.status >= 500) {
            std::cerr << "Falha ao consultar capability " << capability.name
                      << ": status=" << get_response.status
                      << " body=" << get_response.body << "\n";
            std::this_thread::sleep_for(std::chrono::seconds(3));
            continue;
        }

        json payload = {
            {"name", capability.name},
            {"description", capability.description},
            {"capability_type", "sensor"}
        };
        auto post_response = send_http(cataloguer_url, http::verb::post, "/capabilities", &payload);
        if (is_success(post_response.status)) {
            std::cout << "Capability cadastrada: " << capability.name << "\n";
            return true;
        }

        if (post_response.status == 400 && post_response.body.find("taken") != std::string::npos) {
            std::cout << "Capability ja existia: " << capability.name << "\n";
            return true;
        }

        std::cerr << "Falha ao cadastrar capability " << capability.name
                  << " (" << attempt << "/30): status=" << post_response.status
                  << " body=" << post_response.body << "\n";
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }

    return false;
}

static json room_registration_payload(const RoomConfig& room) {
    std::vector<std::string> capability_names;
    for (const auto& capability : capabilities) {
        capability_names.push_back(capability.name);
    }

    return json{
        {"data", {
            {"uuid", room.uuid},
            {"description", room.description},
            {"lat", room.lat},
            {"lon", room.lon},
            {"status", "active"},
            {"capabilities", capability_names}
        }}
    };
}

static bool ensure_room(const RoomConfig& room) {
    const auto payload = room_registration_payload(room);
    const auto resource_path = "/resources/" + room.uuid;

    for (int attempt = 1; attempt <= 30; ++attempt) {
        auto get_response = send_http(cataloguer_url, http::verb::get, resource_path);
        if (get_response.status == 200) {
            auto put_response = send_http(cataloguer_url, http::verb::put, resource_path, &payload);
            if (is_success(put_response.status)) {
                std::cout << "Recurso atualizado: " << room.id << " (" << room.uuid << ")\n";
                return true;
            }

            std::cerr << "Falha ao atualizar recurso " << room.id
                      << ": status=" << put_response.status
                      << " body=" << put_response.body << "\n";
        } else if (get_response.status == 404) {
            auto post_response = send_http(cataloguer_url, http::verb::post, "/resources", &payload);
            if (is_success(post_response.status)) {
                std::cout << "Recurso cadastrado: " << room.id << " (" << room.uuid << ")\n";
                return true;
            }

            if (post_response.status == 422) {
                auto put_response = send_http(cataloguer_url, http::verb::put, resource_path, &payload);
                if (is_success(put_response.status)) {
                    std::cout << "Recurso recuperado via update: " << room.id << "\n";
                    return true;
                }
            }

            std::cerr << "Falha ao cadastrar recurso " << room.id
                      << ": status=" << post_response.status
                      << " body=" << post_response.body << "\n";
        } else {
            std::cerr << "Falha ao consultar recurso " << room.id
                      << ": status=" << get_response.status
                      << " body=" << get_response.body << "\n";
        }

        std::this_thread::sleep_for(std::chrono::seconds(3));
    }

    return false;
}

static void ensure_intercity_catalog() {
    wait_for_cataloguer();
    wait_for_data_collector();

    for (const auto& capability : capabilities) {
        if (!ensure_capability(capability)) {
            throw std::runtime_error("Nao foi possivel cadastrar capability: " + capability.name);
        }
    }

    for (const auto& item : rooms) {
        if (!ensure_room(item.second)) {
            throw std::runtime_error("Nao foi possivel cadastrar recurso: " + item.first);
        }
    }
}

static void add_capability_value(json& data, const std::string& capability, const json& value, const std::string& date) {
    data["data"][capability] = json::array({
        {
            {"value", value},
            {"date", date}
        }
    });
}

static bool post_room_telemetry(const RoomConfig& room, const json& payload) {
    const std::string date = timestamp_from_payload(payload);
    json intercity_payload = {{"data", json::object()}};

    const std::vector<std::string> field_names = {
        "temperatura",
        "umidade",
        "luminosidade",
        "presenca",
        "status_ac",
        "setpoint_ac",
        "status_luz",
        "modo_ac"
    };

    for (const auto& field : field_names) {
        if (payload.contains(field) && !payload[field].is_null()) {
            add_capability_value(intercity_payload, field, payload[field], date);
        }
    }

    if (intercity_payload["data"].empty()) {
        std::cerr << "Payload sem campos InterSCity validos para " << room.id << "\n";
        return false;
    }

    const auto path = "/resources/" + room.uuid + "/data";
    for (int attempt = 1; attempt <= 3; ++attempt) {
        auto response = send_http(adaptor_url, http::verb::post, path, &intercity_payload);
        if (is_success(response.status)) {
            std::cout << "Telemetria enviada ao InterSCity: " << room.id << "\n";
            return true;
        }

        if (response.status == 404) {
            std::cerr << "Recurso nao encontrado no adaptor, recadastrando " << room.id << "\n";
            ensure_room(room);
        }

        std::cerr << "Falha ao enviar telemetria " << room.id
                  << " (" << attempt << "/3): status=" << response.status
                  << " body=" << response.body << "\n";
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    return false;
}

static void on_connect(struct mosquitto* mosq, void*, int rc) {
    if (rc == 0) {
        std::cout << "Conectado ao broker MQTT. Assinando " << mqtt_topic << "\n";
        mosquitto_subscribe(mosq, nullptr, mqtt_topic.c_str(), 0);
    } else {
        std::cerr << "Falha de conexao MQTT: rc=" << rc << "\n";
    }
}

static void on_disconnect(struct mosquitto*, void*, int rc) {
    if (rc != 0) {
        std::cerr << "Conexao MQTT perdida. O cliente tentara reconectar.\n";
    }
}

static void on_message(struct mosquitto*, void*, const struct mosquitto_message* message) {
    if (!message || !message->payload || message->payloadlen <= 0) {
        return;
    }

    try {
        const std::string topic = message->topic ? message->topic : "";
        const std::string body(static_cast<const char*>(message->payload), message->payloadlen);
        const json payload = json::parse(body);

        std::string room_id;
        if (payload.contains("id_sala") && payload["id_sala"].is_string()) {
            room_id = payload["id_sala"].get<std::string>();
        }
        if (room_id.empty()) {
            room_id = room_from_topic(topic);
        }

        auto room_it = rooms.find(room_id);
        if (room_it == rooms.end()) {
            std::cerr << "Sala ignorada pelo bridge InterSCity: " << room_id << "\n";
            return;
        }

        post_room_telemetry(room_it->second, payload);
    } catch (const std::exception& exc) {
        std::cerr << "Erro ao processar mensagem MQTT para InterSCity: " << exc.what() << "\n";
    }
}

int main() {
    try {
        load_dotenv();

        const auto cataloguer_url_text = get_env("INTERSCITY_CATALOGUER_URL", "http://interscity-resource-cataloguer:3000");
        const auto adaptor_url_text = get_env("INTERSCITY_ADAPTOR_URL", "http://interscity-resource-adaptor:3000");
        const auto collector_url_text = get_env("INTERSCITY_COLLECTOR_URL", "http://interscity-data-collector:3000");
        const auto mqtt_broker = get_env("MQTT_BROKER", "mosquitto");
        const auto mqtt_port = std::stoi(get_env("MQTT_PORT", "1883"));
        mqtt_topic = get_env("MQTT_TOPIC", "ac-iot/+/sensores");

        if (!parse_http_url(cataloguer_url_text, cataloguer_url)) {
            throw std::runtime_error("INTERSCITY_CATALOGUER_URL invalida: " + cataloguer_url_text);
        }
        if (!parse_http_url(adaptor_url_text, adaptor_url)) {
            throw std::runtime_error("INTERSCITY_ADAPTOR_URL invalida: " + adaptor_url_text);
        }
        if (!parse_http_url(collector_url_text, collector_url)) {
            throw std::runtime_error("INTERSCITY_COLLECTOR_URL invalida: " + collector_url_text);
        }

        ensure_intercity_catalog();

        mosquitto_lib_init();
        mosquitto* mosq = mosquitto_new("ac_iot_interscity_bridge", true, nullptr);
        if (!mosq) {
            throw std::runtime_error("Nao foi possivel criar cliente MQTT");
        }

        mosquitto_connect_callback_set(mosq, on_connect);
        mosquitto_disconnect_callback_set(mosq, on_disconnect);
        mosquitto_message_callback_set(mosq, on_message);
        mosquitto_reconnect_delay_set(mosq, 2, 30, true);

        while (true) {
            int rc = mosquitto_connect(mosq, mqtt_broker.c_str(), mqtt_port, 60);
            if (rc == MOSQ_ERR_SUCCESS) {
                break;
            }

            std::cerr << "Erro ao conectar no MQTT " << mqtt_broker << ":" << mqtt_port
                      << " -> " << mosquitto_strerror(rc)
                      << ". Tentando novamente em 5 segundos.\n";
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }

        std::cout << "Bridge MQTT -> InterSCity iniciado.\n";
        int loop_rc = mosquitto_loop_forever(mosq, -1, 1);
        std::cerr << "Loop MQTT finalizado: " << mosquitto_strerror(loop_rc) << "\n";

        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
        return loop_rc == MOSQ_ERR_SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& exc) {
        std::cerr << "Falha fatal no bridge InterSCity: " << exc.what() << "\n";
        return EXIT_FAILURE;
    }
}
