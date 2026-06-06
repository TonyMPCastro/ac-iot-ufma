#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <thread>
#include <chrono>
#include <iomanip>
#include <sstream>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// URL base do InterSCity
static std::string INTERSCITY_URL = "https://cidadesinteligentes.lsdi.ufma.br/interscity_lh";

struct RoomConfig {
    std::string id;
    std::string uuid;
    std::string status;
    double setpoint;
    std::string luz;
    double temp_min;
    double temp_max;
    double umidade_min;
    double umidade_max;
    int luz_min;
    int luz_max;
    double temp_simulada; // Temperatura forçada
    bool presenca; // Sensor de presença humana
    double umidade_simulada; // Forçada
    int luz_simulada; // Forçada
    std::string modo_ac; // "ativo" ou "desativado"
    std::string last_cmd_timestamp; // Para evitar processar o mesmo comando várias vezes
};

static std::map<std::string, RoomConfig> SALAS;
static std::mt19937_64 RNG(std::random_device{}());
static int INTERVALO;

static std::string getenv_or(const char* key, const char* def) {
    const char* value = std::getenv(key);
    return value ? std::string(value) : std::string(def);
}

static double random_double(double a, double b) {
    std::uniform_real_distribution<double> dist(a, b);
    return dist(RNG);
}

static int random_int(int a, int b) {
    std::uniform_int_distribution<int> dist(a, b);
    return dist(RNG);
}

static std::string get_iso8601_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&in_time_t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

struct MemoryStruct {
    char *memory;
    size_t size;
};

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *ptr = (char*)realloc(mem->memory, mem->size + realsize + 1);
    if(ptr == nullptr) return 0;

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

static std::string send_http_request(const std::string& url, const std::string& method, const std::string& payload = "") {
    CURL *curl = curl_easy_init();
    if (!curl) return "";

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    } else if (method == "GET") {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    }
    
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    struct MemoryStruct chunk;
    chunk.memory = (char*)malloc(1);
    chunk.size = 0;

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

    CURLcode res = curl_easy_perform(curl);
    std::string response = "";
    if (res != CURLE_OK) {
        std::cerr << "[ERRO] Falha na requisição " << method << " para " << url << ": " << curl_easy_strerror(res) << "\n";
    } else {
        long code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        response = std::string(chunk.memory, chunk.size);
        if (code >= 300) {
            if (response.find("already been taken") == std::string::npos) {
                std::cerr << "[ERRO HTTP " << code << "] Resposta: " << response << "\n";
            }
            response = ""; // Tratar como erro
        }
    }

    free(chunk.memory);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    return response;
}

static void registrar_capabilities() {
    std::vector<std::pair<std::string, std::string>> caps = {
        {"temperatura", "sensor"},
        {"umidade", "sensor"},
        {"luminosidade", "sensor"},
        {"presenca", "sensor"},
        {"status_ac", "sensor"},
        {"setpoint_ac", "sensor"},
        {"status_luz", "sensor"},
        {"modo_ac", "sensor"},
        {"cmd_status_ac", "sensor"},
        {"cmd_setpoint_ac", "sensor"},
        {"cmd_status_luz", "sensor"},
        {"cmd_modo_ac", "sensor"},
        {"cmd_presenca", "sensor"},
        {"cmd_temperatura", "sensor"},
        {"cmd_umidade", "sensor"},
        {"cmd_luminosidade", "sensor"}
    };

    std::string url = INTERSCITY_URL + "/catalog/capabilities";

    for (const auto& cap : caps) {
        json payload = {
            {"name", cap.first},
            {"description", "Capability gerada pelo simulador para " + cap.first},
            {"capability_type", cap.second}
        };
        std::cout << "Registrando capability: " << cap.first << "\n";
        send_http_request(url, "POST", payload.dump());
        std::this_thread::sleep_for(std::chrono::milliseconds(20000));
    }
}

static void registrar_resources() {
    std::string url = INTERSCITY_URL + "/catalog/resources";

    for (const auto& [id, sala] : SALAS) {
        json payload = {
            {"data", {
                {"uuid", sala.uuid},
                {"description", "Simulador da " + sala.id},
                {"capabilities", json::array({"temperatura", "umidade", "luminosidade", "presenca", "status_ac", "setpoint_ac", "status_luz", "modo_ac", "cmd_status_ac", "cmd_setpoint_ac", "cmd_status_luz", "cmd_modo_ac", "cmd_presenca", "cmd_temperatura", "cmd_umidade", "cmd_luminosidade"})},
                {"status", "active"},
                {"lat", -2.5307},
                {"lon", -44.3068}
            }}
        };
        std::cout << "Registrando resource: " << sala.id << " (UUID: " << sala.uuid << ")\n";
        send_http_request(url, "POST", payload.dump());
        std::this_thread::sleep_for(std::chrono::milliseconds(20000));
    }
}

static json gerar_dados(const RoomConfig& sala) {
    double temperatura;
    if (sala.temp_simulada > 0.0) {
        temperatura = sala.temp_simulada;
    } else if (sala.status == "desligado") {
        temperatura = random_double(sala.temp_max, sala.temp_max + 2.0);
    } else {
        temperatura = random_double(sala.setpoint - 0.5, sala.setpoint + 0.5);
    }

    double umidade = (sala.umidade_simulada > 0.0) ? sala.umidade_simulada : random_double(sala.umidade_min, sala.umidade_max);
    
    int luminosidade;
    if (sala.luz_simulada >= 0) {
        luminosidade = sala.luz_simulada;
    } else {
        luminosidade = (sala.luz == "ligado")
            ? random_int(800, 1100)
            : random_int(5, 50);
    }

    std::string ts = get_iso8601_timestamp();

    json data = {
        {"temperatura", json::array({{{"timestamp", ts}, {"value", static_cast<int>(std::round(temperatura))}}})},
        {"umidade", json::array({{{"timestamp", ts}, {"value", static_cast<int>(std::round(umidade))}}})},
        {"luminosidade", json::array({{{"timestamp", ts}, {"value", luminosidade}}})},
        {"presenca", json::array({{{"timestamp", ts}, {"value", sala.presenca ? 1 : 0}}})},
        {"status_ac", json::array({{{"timestamp", ts}, {"value", sala.status}}})},
        {"setpoint_ac", json::array({{{"timestamp", ts}, {"value", sala.setpoint}}})},
        {"status_luz", json::array({{{"timestamp", ts}, {"value", sala.luz}}})},
        {"modo_ac", json::array({{{"timestamp", ts}, {"value", sala.modo_ac}}})}
    };

    return json{{"data", data}};
}

static void poll_commands_for_room(RoomConfig& sala) {
    std::string url = INTERSCITY_URL + "/collector/resources/" + sala.uuid + "/data/last";
    std::string response = send_http_request(url, "GET");
    if (response.empty()) return;

    try {
        json j = json::parse(response);
        if (!j.contains("resources") || !j["resources"].is_array() || j["resources"].empty()) return;

        json res_obj = j["resources"][0];
        if (!res_obj.contains("capabilities")) return;

        json caps = res_obj["capabilities"];
        std::string latest_timestamp = "";
        
        auto extrair_cmd = [&](const std::string& cap_name, auto& target_var, auto parser) {
            if (caps.contains(cap_name) && caps[cap_name].is_array() && !caps[cap_name].empty()) {
                json leitura = caps[cap_name][0];
                std::string ts = "";
                if (leitura.contains("date") && leitura["date"].is_string()) {
                    ts = leitura["date"].get<std::string>();
                } else if (leitura.contains("timestamp") && leitura["timestamp"].is_string()) {
                    ts = leitura["timestamp"].get<std::string>();
                }
                
                if (!ts.empty() && ts > sala.last_cmd_timestamp) {
                    try {
                        target_var = parser(leitura["value"]);
                        if (ts > latest_timestamp) latest_timestamp = ts;
                        std::cout << "[COMANDO] " << sala.id << " -> " << cap_name << " atualizado (" << ts << ").\n";
                    } catch (...) {}
                }
            }
        };

        // Parsers
        auto parse_str = [](const json& val) { return val.get<std::string>(); };
        auto parse_double = [](const json& val) { 
            if (val.is_number()) return val.get<double>(); 
            if (val.is_string()) return std::stod(val.get<std::string>());
            return 0.0;
        };
        auto parse_bool = [](const json& val) { 
            if (val.is_boolean()) return val.get<bool>();
            if (val.is_number()) return val.get<int>() != 0;
            if (val.is_string()) return val.get<std::string>() == "true" || val.get<std::string>() == "1";
            return false;
        };

        extrair_cmd("cmd_status_ac", sala.status, parse_str);
        extrair_cmd("cmd_setpoint_ac", sala.setpoint, parse_double);
        extrair_cmd("cmd_status_luz", sala.luz, parse_str);
        extrair_cmd("cmd_modo_ac", sala.modo_ac, parse_str);
        extrair_cmd("cmd_presenca", sala.presenca, parse_bool);
        extrair_cmd("cmd_temperatura", sala.temp_simulada, parse_double);
        extrair_cmd("cmd_umidade", sala.umidade_simulada, parse_double);
        
        auto parse_int = [](const json& val) {
            if (val.is_number()) return val.get<int>();
            if (val.is_string()) return std::stoi(val.get<std::string>());
            return 0;
        };
        extrair_cmd("cmd_luminosidade", sala.luz_simulada, parse_int);

        if (!latest_timestamp.empty() && latest_timestamp > sala.last_cmd_timestamp) {
            sala.last_cmd_timestamp = latest_timestamp;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Erro ao fazer parse dos comandos: " << e.what() << "\n";
    }
}

static void process_all_rooms() {
    for (auto& [id, sala] : SALAS) {
        // 1. Consulta novos comandos no Collector
        poll_commands_for_room(sala);
        
        // 2. Publica o estado atualizado no Adaptor
        json payload = gerar_dados(sala);
        std::string url = INTERSCITY_URL + "/adaptor/resources/" + sala.uuid + "/data";
        
        std::cout << "[" << get_iso8601_timestamp() << "] Enviando dados da sala " << id << "...\n";
        send_http_request(url, "POST", payload.dump());
        
        // Rate limiting de 50ms (20 req/s máximo) para não derrubar o Kong
        std::this_thread::sleep_for(std::chrono::milliseconds(20000));
    }
}

int main() {
    INTERVALO = std::atoi(getenv_or("PUBLISH_INTERVAL", "60").c_str());
    if (INTERVALO <= 0) INTERVALO = 20;

    std::string custom_url = getenv_or("INTERSCITY_URL", "");
    if (!custom_url.empty()) {
        INTERSCITY_URL = custom_url;
    }

    int TOTAL_SALAS = std::atoi(getenv_or("TOTAL_SALAS", "10").c_str());
    if (TOTAL_SALAS <= 0) TOTAL_SALAS = 10;

    SALAS.clear();
    for (int i = 1; i <= TOTAL_SALAS; ++i) {
        char id_buf[16];
        snprintf(id_buf, sizeof(id_buf), "sala%03d", i);
        char uuid_buf[64];
        snprintf(uuid_buf, sizeof(uuid_buf), "00000000-0000-0000-0000-%012d", i);
        
        RoomConfig sala;
        sala.id = id_buf;
        sala.uuid = uuid_buf;
        sala.status = "ligado";
        sala.setpoint = random_double(20.0, 24.0);
        sala.luz = "desligado";
        sala.temp_min = 20.0;
        sala.temp_max = 28.0;
        sala.umidade_min = 40.0;
        sala.umidade_max = 70.0;
        sala.luz_min = 100;
        sala.luz_max = 1000;
        sala.temp_simulada = 0.0;
        sala.presenca = false;
        sala.umidade_simulada = 0.0;
        sala.luz_simulada = -1;
        sala.modo_ac = "ativo";
        sala.last_cmd_timestamp = "";
        
        SALAS[sala.id] = sala;
    }

    curl_global_init(CURL_GLOBAL_ALL);

    std::cout << "Inicializando integração com InterSCity...\n";
    std::cout << "URL: " << INTERSCITY_URL << "\n";
    
    registrar_capabilities();
    registrar_resources();

    std::cout << "Iniciando simulação. Verificando comandos e publicando a cada " << INTERVALO << " segundos...\n";

    bool running = true;
    while (running) {
        process_all_rooms();
        std::this_thread::sleep_for(std::chrono::seconds(INTERVALO));
    }

    curl_global_cleanup();
    return EXIT_SUCCESS;
}
