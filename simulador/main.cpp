#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <thread>
#include <chrono>

#include <mosquitto.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct RoomConfig {
    std::string id;
    std::string topic;
    std::string status;
    double setpoint;
    std::string luz;
    double temp_min;
    double temp_max;
    double umidade_min;
    double umidade_max;
    int luz_min;
    int luz_max;
    double temp_simulada; // Temperatura forçada via simulador web
    bool presenca; // Sensor de presença humana
    double umidade_simulada; // Forçada
    int luz_simulada; // Forçada
    std::string modo_ac; // "ativo" ou "desativado"
};

static std::map<std::string, RoomConfig> SALAS;
static std::mt19937_64 RNG(std::random_device{}());
static std::string BROKER;
static int PORT;
static int INTERVALO;
static struct mosquitto* MOSQ = nullptr;

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

    return json{
        {"id_sala", sala.id},
        {"status_ac", sala.status},
        {"setpoint_ac", sala.setpoint},
        {"status_luz", sala.luz},
        {"temperatura", std::round(temperatura * 100.0) / 100.0},
        {"umidade", std::round(umidade * 100.0) / 100.0},
        {"luminosidade", luminosidade},
        {"presenca", sala.presenca},
        {"modo_ac", sala.modo_ac},
        {"timestamp", static_cast<long>(std::time(nullptr))}
    };
}

static void publish_room(const RoomConfig& sala) {
    json dados = gerar_dados(sala);
    std::string payload = dados.dump();
    int ret = mosquitto_publish(MOSQ, nullptr, sala.topic.c_str(), static_cast<int>(payload.size()), payload.c_str(), 0, true);
    if (ret != MOSQ_ERR_SUCCESS) {
        std::cerr << "Falha ao publicar mensagem MQTT para " << sala.topic << ": " << mosquitto_strerror(ret) << "\n";
    }
}

static void publish_all_rooms() {
    for (const auto& [id, sala] : SALAS) {
        publish_room(sala);
    }
}

static void on_connect(struct mosquitto* mosq, void* userdata, int rc) {
    if (rc == 0) {
        std::cout << "Conectado ao Broker MQTT em " << BROKER << ":" << PORT << "\n";
        mosquitto_subscribe(mosq, nullptr, "ac-iot/+/comando", 0);
        mosquitto_subscribe(mosq, nullptr, "ac-iot/all/comando", 0);
        std::cout << "Inscrito em ac-iot/+/comando e ac-iot/all/comando\n";
        publish_all_rooms();
    } else {
        std::cerr << "Falha de conexão MQTT: rc=" << rc << "\n";
    }
}

static void on_message(struct mosquitto* mosq, void* userdata, const struct mosquitto_message* message) {
    try {
        if (!message || !message->payload || message->payloadlen <= 0) {
            return;
        }

        std::string payload(reinterpret_cast<const char*>(message->payload), message->payloadlen);
        json dados = json::parse(payload);
        std::string topic = message->topic ? message->topic : "";
        std::vector<std::string> partes;
        std::string alvo;
        {
            size_t start = 0;
            size_t end;
            while ((end = topic.find('/', start)) != std::string::npos) {
                partes.push_back(topic.substr(start, end - start));
                start = end + 1;
            }
            if (start < topic.size()) partes.push_back(topic.substr(start));
        }

        if (partes.size() >= 2) {
            alvo = partes[1];
        }

        if (alvo.empty()) {
            return;
        }

        std::vector<std::string> salas_para_atualizar;
        if (alvo == "all") {
            for (const auto& [id, sala] : SALAS) {
                salas_para_atualizar.push_back(id);
            }
        } else if (SALAS.count(alvo)) {
            salas_para_atualizar.push_back(alvo);
        }

        for (const auto& id_sala : salas_para_atualizar) {
            auto& sala = SALAS[id_sala];
            bool mudou = false;

            if (dados.contains("comando") && dados["comando"].is_string()) {
                std::string cmd = dados["comando"].get<std::string>();
                if (cmd == "ligar" || cmd == "desligar") {
                    std::string novo_status = (cmd == "ligar") ? "ligado" : "desligado";
                    if (sala.status != novo_status) {
                        sala.status = novo_status;
                        mudou = true;
                    }
                }
            }

            if (dados.contains("setpoint")) {
                try {
                    sala.setpoint = dados["setpoint"].get<double>();
                    mudou = true;
                } catch (...) {
                }
            }

            if (dados.contains("luz") && dados["luz"].is_string()) {
                std::string cmd_luz = dados["luz"].get<std::string>();
                if (cmd_luz == "ligar" || cmd_luz == "desligar") {
                    std::string novo_status_luz = (cmd_luz == "ligar") ? "ligado" : "desligado";
                    if (sala.luz != novo_status_luz) {
                        sala.luz = novo_status_luz;
                        mudou = true;
                    }
                }
            }

            if (dados.contains("temperatura")) {
                try {
                    sala.temp_simulada = dados["temperatura"].get<double>();
                    mudou = true;
                } catch (...) {
                }
            }

            if (dados.contains("presenca")) {
                try {
                    sala.presenca = dados["presenca"].get<bool>();
                    mudou = true;
                } catch (...) {
                }
            }

            if (dados.contains("umidade")) {
                try {
                    sala.umidade_simulada = dados["umidade"].get<double>();
                    mudou = true;
                } catch (...) {
                }
            }

            if (dados.contains("luminosidade")) {
                try {
                    sala.luz_simulada = dados["luminosidade"].get<int>();
                    mudou = true;
                } catch (...) {
                }
            }

            if (dados.contains("modo_ac") && dados["modo_ac"].is_string()) {
                std::string modo = dados["modo_ac"].get<std::string>();
                if (modo == "ativo" || modo == "desativado") {
                    if (sala.modo_ac != modo) {
                        sala.modo_ac = modo;
                        mudou = true;
                    }
                }
            }

            if (mudou) {
                std::cout << "[COMANDO] " << id_sala << " atualizado: AC=" << sala.status
                          << ", Setpoint=" << sala.setpoint << "°C, Luz=" << sala.luz << "\n";
            }

            publish_room(sala);
        }
    } catch (const std::exception& exc) {
        std::cerr << "Erro ao processar mensagem MQTT: " << exc.what() << "\n";
    }
}

int main() {
    BROKER = getenv_or("MQTT_BROKER", "mosquitto");
    PORT = std::atoi(getenv_or("MQTT_PORT", "1883").c_str());
    INTERVALO = std::atoi(getenv_or("PUBLISH_INTERVAL", "60").c_str());
    if (INTERVALO <= 0) INTERVALO = 20;

    SALAS = {
        {"sala01", {"sala01", "ac-iot/sala01/sensores", "ligado", 22.0, "desligado", 20.0, 25.0, 40.0, 50.0, 300, 500, 0.0, false, 0.0, -1, "ativo"}},
        {"sala02", {"sala02", "ac-iot/sala02/sensores", "ligado", 24.0, "desligado", 25.0, 35.0, 50.0, 70.0, 800, 1000, 0.0, false, 0.0, -1, "ativo"}},
        {"sala03", {"sala03", "ac-iot/sala03/sensores", "ligado", 23.0, "desligado", 22.0, 28.0, 45.0, 60.0, 100, 800, 0.0, false, 0.0, -1, "ativo"}}
    };

    mosquitto_lib_init();
    MOSQ = mosquitto_new("simulador_esp32_multisala", true, nullptr);
    if (!MOSQ) {
        std::cerr << "Falha ao criar cliente MQTT\n";
        return EXIT_FAILURE;
    }

    mosquitto_connect_callback_set(MOSQ, on_connect);
    mosquitto_message_callback_set(MOSQ, on_message);

    while (true) {
        int rc = mosquitto_connect(MOSQ, BROKER.c_str(), PORT, 60);
        if (rc == MOSQ_ERR_SUCCESS) {
            break;
        }
        std::cerr << "Erro ao conectar em " << BROKER << ":" << PORT << " -> " << mosquitto_strerror(rc)
                  << ". Tentando novamente em 5 segundos...\n";
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    mosquitto_loop_start(MOSQ);
    std::cout << "Iniciando simulação MQTT. Publicando a cada " << INTERVALO << " segundos...\n";

    bool running = true;
    while (running) {
        publish_all_rooms();
        std::this_thread::sleep_for(std::chrono::seconds(INTERVALO));
    }

    mosquitto_loop_stop(MOSQ, true);
    mosquitto_destroy(MOSQ);
    mosquitto_lib_cleanup();
    return EXIT_SUCCESS;
}
