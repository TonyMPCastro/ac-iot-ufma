#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <random>
#include <map>
#include <cmath>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

static std::string INTERSCITY_URL = "http://interscity-api-gateway:8000";

struct RoomConfig {
    std::string id;
    std::string uuid;
    std::string status; // "ligado" ou "desligado"
    double setpoint;
    std::string luz; // "ligado" ou "desligado"
    
    // Configurações de limites e física
    double temp_min;
    double temp_max;
    double umidade_min;
    double umidade_max;
    int luz_min;
    int luz_max;
    
    // Estado Físico Atual
    double temp_atual;
    double umidade_atual;
    int luz_atual;
    
    // Override manual (0 ou -1 = Auto)
    double temp_simulada;
    double umidade_simulada;
    int luz_simulada;
    
    bool presenca; // Sensor de presença humana
    
    std::string modo_ac; // "ativo" ou "desativado"
    std::string last_cmd_timestamp; // Para evitar processar o mesmo comando várias vezes
};

static std::map<std::string, RoomConfig> SALAS;
static std::mt19937_64 RNG(std::random_device{}());
static int POLL_INTERVAL;
static int PUBLISH_INTERVAL;

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
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    auto timer = std::chrono::system_clock::to_time_t(now);
    std::tm bt = *std::gmtime(&timer);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &bt);
    char final_buf[128];
    snprintf(final_buf, sizeof(final_buf), "%s.%03dZ", buf, (int)ms.count());
    return std::string(final_buf);
}

struct MemoryStruct {
    char *memory;
    size_t size;
};

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;
    char *ptr = (char*)realloc(mem->memory, mem->size + realsize + 1);
    if(ptr == NULL) return 0;
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    return realsize;
}

static std::string send_http_request(const std::string& url, const std::string& method, const std::string& payload = "") {
    CURL *curl = curl_easy_init();
    if (!curl) return "";

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    
    // TIMEOUT PARA EVITAR TRAVAMENTOS (HANG)
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    if (!payload.empty()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
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
        {"cmd_status_ac", "actuator"},
        {"cmd_setpoint_ac", "actuator"},
        {"cmd_status_luz", "actuator"},
        {"cmd_modo_ac", "actuator"},
        {"cmd_presenca", "actuator"},
        {"cmd_temperatura", "actuator"},
        {"cmd_umidade", "actuator"},
        {"cmd_luminosidade", "actuator"}
    };

    std::string url = INTERSCITY_URL + "/catalog/capabilities";

    for (const auto& cap : caps) {
        json payload = {
            {"name", cap.first},
            {"description", "Capacidade " + cap.first},
            {"capability_type", cap.second}
        };
        std::cout << "Registrando capability: " << cap.first << "\n";
        send_http_request(url, "POST", payload.dump());
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
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
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

static void recuperar_estado_inicial(RoomConfig& sala) {
    std::cout << "[" << get_iso8601_timestamp() << "] Recuperando estado inicial da " << sala.id << "...\n";
    std::string url = INTERSCITY_URL + "/collector/resources/" + sala.uuid + "/data/last";
    std::string response = send_http_request(url, "GET");
    if (response.empty()) return;

    try {
        json j = json::parse(response);
        if (!j.contains("resources") || !j["resources"].is_array() || j["resources"].empty()) return;

        json res_obj = j["resources"][0];
        if (!res_obj.contains("capabilities")) return;

        json caps = res_obj["capabilities"];
        
        auto extract = [&](const std::string& key, auto& target, auto parser) {
            if (caps.contains(key) && caps[key].is_array() && !caps[key].empty()) {
                try { target = parser(caps[key][0]["value"]); } catch(...) {}
            }
        };

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

        extract("temperatura", sala.temp_atual, parse_double);
        extract("umidade", sala.umidade_atual, parse_double);
        extract("luminosidade", sala.luz_atual, parse_double);
        extract("status_ac", sala.status, parse_str);
        extract("setpoint_ac", sala.setpoint, parse_double);
        extract("status_luz", sala.luz, parse_str);
        extract("modo_ac", sala.modo_ac, parse_str);
        extract("presenca", sala.presenca, parse_bool);

        // Prevenir reprocessamento de comandos antigos
        std::string max_ts = "";
        for (auto& el : caps.items()) {
            if (el.value().is_array() && !el.value().empty()) {
                json leitura = el.value()[0];
                if (leitura.contains("date") && leitura["date"].is_string()) {
                    std::string ts = leitura["date"].get<std::string>();
                    if (ts > max_ts) max_ts = ts;
                } else if (leitura.contains("timestamp") && leitura["timestamp"].is_string()) {
                    std::string ts = leitura["timestamp"].get<std::string>();
                    if (ts > max_ts) max_ts = ts;
                }
            }
        }
        if (!max_ts.empty()) sala.last_cmd_timestamp = max_ts;

    } catch (...) {
        // Ignora erros no boot
    }
}

static json gerar_dados(const RoomConfig& sala) {
    std::string ts = get_iso8601_timestamp();
    json data = {
        {"temperatura", json::array({{{"timestamp", ts}, {"value", static_cast<int>(std::round(sala.temp_atual))}}})},
        {"umidade", json::array({{{"timestamp", ts}, {"value", static_cast<int>(std::round(sala.umidade_atual))}}})},
        {"luminosidade", json::array({{{"timestamp", ts}, {"value", sala.luz_atual}}})},
        {"presenca", json::array({{{"timestamp", ts}, {"value", sala.presenca ? 1 : 0}}})},
        {"status_ac", json::array({{{"timestamp", ts}, {"value", sala.status}}})},
        {"setpoint_ac", json::array({{{"timestamp", ts}, {"value", sala.setpoint}}})},
        {"status_luz", json::array({{{"timestamp", ts}, {"value", sala.luz}}})},
        {"modo_ac", json::array({{{"timestamp", ts}, {"value", sala.modo_ac}}})}
    };
    return json{{"data", data}};
}

static void simular_fisica(RoomConfig& sala) {
    // 1. Dinâmica da Presença Aleatória (Alunos entrando e saindo)
    if (!sala.presenca) {
        if (random_int(1, 100) <= 5) {
            sala.presenca = true;
            std::cout << "[FÍSICA] Alunos entraram na " << sala.id << "\n";
        }
    } else {
        if (random_int(1, 100) <= 2) {
            sala.presenca = false;
            std::cout << "[FÍSICA] Alunos saíram da " << sala.id << "\n";
        }
    }

    // 2. Dinâmica da Temperatura (Inércia Térmica)
    if (sala.temp_simulada > 0.0) {
        sala.temp_atual = sala.temp_simulada;
    } else {
        double target_temp = sala.temp_max; // Ambiente sem AC esquenta
        
        if (sala.status == "ligado" && sala.modo_ac == "ativo") {
            target_temp = sala.setpoint;
        }

        // Calor orgânico
        if (sala.presenca) target_temp += 1.5;

        // Resfriamento ou aquecimento gradual
        if (sala.temp_atual > target_temp) {
            sala.temp_atual -= 0.2; // Esfria mais rápido
            if (sala.temp_atual < target_temp) sala.temp_atual = target_temp;
        } else if (sala.temp_atual < target_temp) {
            sala.temp_atual += 0.1; // Esquenta mais devagar
            if (sala.temp_atual > target_temp) sala.temp_atual = target_temp;
        }
    }

    // 3. Dinâmica da Umidade
    if (sala.umidade_simulada > 0.0) {
        sala.umidade_atual = sala.umidade_simulada;
    } else {
        double target_umidade = sala.presenca ? sala.umidade_max : (sala.umidade_min + sala.umidade_max)/2.0;
        if (sala.status == "ligado") target_umidade -= 10.0; // AC seca o ar

        if (sala.umidade_atual > target_umidade) sala.umidade_atual -= 0.5;
        else if (sala.umidade_atual < target_umidade) sala.umidade_atual += 0.5;
    }

    // 4. Dinâmica da Luz
    if (sala.luz_simulada >= 0) {
        sala.luz_atual = sala.luz_simulada;
    } else {
        if (sala.luz == "ligado") {
            sala.luz_atual = random_int(800, 1100);
        } else {
            sala.luz_atual = random_int(5, 50);
        }
    }
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

        auto parse_str = [](const json& val) { return val.get<std::string>(); };
        auto parse_status = [](const json& val) {
            std::string s = val.get<std::string>();
            if (s == "ligar") return std::string("ligado");
            if (s == "desligar") return std::string("desligado");
            return s;
        };
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
        auto parse_int = [](const json& val) {
            if (val.is_number()) return val.get<int>();
            if (val.is_string()) return std::stoi(val.get<std::string>());
            return 0;
        };

        extrair_cmd("cmd_status_ac", sala.status, parse_status);
        extrair_cmd("cmd_setpoint_ac", sala.setpoint, parse_double);
        extrair_cmd("cmd_status_luz", sala.luz, parse_status);
        extrair_cmd("cmd_modo_ac", sala.modo_ac, parse_str);
        extrair_cmd("cmd_presenca", sala.presenca, parse_bool);
        
        // Forçar injeção direta de estado físico (se vier do HTML)
        extrair_cmd("cmd_temperatura", sala.temp_simulada, parse_double);
        extrair_cmd("cmd_umidade", sala.umidade_simulada, parse_double);
        extrair_cmd("cmd_luminosidade", sala.luz_simulada, parse_int);

        if (!latest_timestamp.empty() && latest_timestamp > sala.last_cmd_timestamp) {
            sala.last_cmd_timestamp = latest_timestamp;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Erro ao fazer parse dos comandos: " << e.what() << "\n";
    }
}

static void publicar_sensores(const RoomConfig& sala) {
    json payload = gerar_dados(sala);
    std::string url = INTERSCITY_URL + "/adaptor/resources/" + sala.uuid + "/data";
    std::cout << "[" << get_iso8601_timestamp() << "] Enviando (POST) estado atualizado da " << sala.id << " para a rede...\n";
    send_http_request(url, "POST", payload.dump());
}

int main() {
    std::cout << std::unitbuf; // Desabilita buffer para os logs do Docker aparecerem na hora

    POLL_INTERVAL = std::atoi(getenv_or("POLL_INTERVAL", "5").c_str());
    PUBLISH_INTERVAL = std::atoi(getenv_or("PUBLISH_INTERVAL", "30").c_str());

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
        
        sala.temp_atual = 25.0; // Inicia default
        sala.umidade_atual = 50.0;
        sala.luz_atual = 50;
        
        sala.temp_simulada = 0.0;
        sala.umidade_simulada = 0.0;
        sala.luz_simulada = -1;
        
        sala.presenca = false;
        sala.modo_ac = "ativo";
        sala.last_cmd_timestamp = "";
        
        SALAS[sala.id] = sala;
    }

    curl_global_init(CURL_GLOBAL_ALL);

    std::cout << "Inicializando integração com InterSCity...\n";
    std::cout << "URL: " << INTERSCITY_URL << "\n";
    
    registrar_capabilities();
    registrar_resources();
    
    for (auto& [id, sala] : SALAS) {
        recuperar_estado_inicial(sala);
    }

    std::cout << "Iniciando simulação física.\n";
    std::cout << "-> Física avaliada a cada: " << POLL_INTERVAL << "s\n";
    std::cout << "-> Dados publicados na nuvem a cada: " << PUBLISH_INTERVAL << "s\n";

    auto last_publish = std::chrono::steady_clock::now() - std::chrono::seconds(PUBLISH_INTERVAL); // forçar publicar no início

    bool running = true;
    while (running) {
        auto now = std::chrono::steady_clock::now();
        bool should_publish = std::chrono::duration_cast<std::chrono::seconds>(now - last_publish).count() >= PUBLISH_INTERVAL;

        for (auto& [id, sala] : SALAS) {
            poll_commands_for_room(sala);
            simular_fisica(sala);
            
            if (should_publish) {
                publicar_sensores(sala);
            }
        }
        
        if (should_publish) {
            last_publish = now;
        }

        std::this_thread::sleep_for(std::chrono::seconds(POLL_INTERVAL));
    }

    curl_global_cleanup();
    return 0;
}
