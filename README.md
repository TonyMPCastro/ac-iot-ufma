# 🌡️ ac-iot-ufma

**Monitorização e Controlo Inteligente de Ar-Condicionado via IoT em Salas de Aula**

Projeto acadêmico desenvolvido na **UFMA** (Universidade Federal do Maranhão) que utiliza IoT para monitorar temperatura, umidade e presença em salas de aula, controlando automaticamente aparelhos de ar-condicionado por infravermelho — com o objetivo de promover eficiência energética e conforto térmico.

---

## 📐 Arquitetura do Sistema

```
┌──────────────┐        MQTT         ┌──────────────────┐       HTTP        ┌─────────────┐
│   ESP32      │ ──────────────────► │  Mosquitto       │                   │  InterSCity │
│  (Sensores   │     porta 1883      │  (Broker MQTT)   │                   │  (API REST) │
│   + IR LED)  │ ◄────────────────── │                  │                   │             │
└──────────────┘    Comandos IR      └────────┬─────────┘                   └──────▲──────┘
                                              │                                    │
                                              │ MQTT                               │ HTTP
                                              ▼                                    │
                                     ┌──────────────────┐                          │
                                     │   Node-RED        │──────────────────────────┘
                                     │  (Motor de Regras │
                                     │   + Dashboard)    │
                                     │   porta 1880      │
                                     └──────────────────┘
```

### Componentes

| Componente | Tecnologia | Execução |
|---|---|---|
| **Sensores + Atuador** | ESP32 + DHT22 + PIR + IR LED | Firmware local (PlatformIO / Wokwi) |
| **Broker MQTT** | Eclipse Mosquitto 2.x | Docker |
| **Middleware / Dashboard** | Node-RED + node-red-dashboard | Docker |
| **Plataforma IoT** | InterSCity | API externa |

---

## 📁 Estrutura do Repositório

```
ac-iot-ufma/
├── docker-compose.yml          # Orquestração dos containers
├── docker/
│   ├── mosquitto/
│   │   └── config/
│   │       └── mosquitto.conf  # Configuração do broker MQTT
│   └── nodered/
│       └── data/               # Volume persistente (flows.json)
├── firmware/
│   └── esp32/
│       ├── src/
│       │   └── main.cpp        # Código principal do ESP32
│       ├── chips/              # Chips customizados Wokwi
│       ├── platformio.ini      # Configuração PlatformIO
│       ├── diagram.json        # Diagrama do circuito Wokwi
│       └── wokwi.toml          # Config do simulador Wokwi
├── node-red/                   # Fluxos exportados para versionamento
├── interscity/                 # Integração com API InterSCity
├── ir-codes/                   # Catálogo de códigos IR por modelo de AC
├── tests/                      # Scripts de teste e validação
├── docs/                       # Documentação e diagramas
├── .gitignore
├── LICENSE
└── README.md
```

---

## ⚙️ Requisitos

### Obrigatórios

- [Docker Desktop](https://www.docker.com/products/docker-desktop/) (Windows/Mac/Linux)
- [Git](https://git-scm.com/)

### Para desenvolvimento do Firmware (opcional na etapa inicial)

- [VS Code](https://code.visualstudio.com/)
- Extensão [PlatformIO IDE](https://platformio.org/install/ide?install=vscode)
- Extensão [Wokwi Simulator](https://marketplace.visualstudio.com/items?itemName=Wokwi.wokwi-vscode)

### Versões testadas

| Ferramenta | Versão |
|---|---|
| Docker Compose | v2.x+ |
| Mosquitto | 2.0.x |
| Node-RED | 4.x |
| PlatformIO Core | 6.x |
| ESP32 Arduino | 2.x |

---

## 🚀 Como Executar o Projeto

### 1. Clonar o repositório

```bash
git clone https://github.com/seu-usuario/ac-iot-ufma.git
cd ac-iot-ufma
```

### 2. Iniciar a infraestrutura Docker

```bash
docker compose up -d
```

Isso levanta dois containers:
- **ac_iot_mosquitto** — Broker MQTT na porta `1883` (e WebSocket na `9001`)
- **ac_iot_nodered** — Node-RED na porta `1880`

### 3. Verificar se os containers estão saudáveis

```bash
docker compose ps
```

Você deve ver ambos os serviços com status `Up` (e o Mosquitto como `healthy`).

### 4. Acessar o Node-RED Dashboard

Abra no navegador:

```
http://localhost:1880
```

### 5. Instalar `node-red-dashboard` (primeira vez)

1. No editor Node-RED, clique no menu **☰** (canto superior direito).
2. Selecione **Manage Palette**.
3. Na aba **Install**, pesquise por `node-red-dashboard`.
4. Clique em **Install**.

O dashboard ficará acessível em: `http://localhost:1880/ui`

---

## 🧪 Teste Rápido — Validar a Stack

### Teste via terminal (publicar mensagem MQTT)

Abra **dois terminais**.

**Terminal 1** — Inscrever-se para receber mensagens:

```bash
docker exec ac_iot_mosquitto mosquitto_sub -h localhost -t "ac-iot/#" -v
```

**Terminal 2** — Publicar uma mensagem de teste:

```bash
docker exec ac_iot_mosquitto mosquitto_pub -h localhost -t "ac-iot/teste" -m "{\"temperatura\":25.5,\"presenca\":true}"
```

**Resultado esperado no Terminal 1:**

```
ac-iot/teste {"temperatura":25.5,"presenca":true}
```

✅ Se a mensagem apareceu, o broker MQTT está funcionando perfeitamente!

### Teste via script automatizado (Windows)

```bash
tests\test_mqtt_pub.bat
```

---

## 📊 Tópicos MQTT

| Tópico | Direção | Descrição |
|---|---|---|
| `ac-iot/sala01/sensores` | ESP32 → Node-RED | Dados de temperatura, umidade e presença |
| `ac-iot/sala01/comando` | Node-RED → ESP32 | Comandos para ligar/desligar AC |
| `ac-iot/sala01/status` | ESP32 → Node-RED | Status de conexão do dispositivo |

---

## 🛑 Parar os Containers

```bash
docker compose down
```

Para remover também os volumes (dados persistidos):

```bash
docker compose down -v
```

---

## 🔧 Desenvolvimento do Firmware

O firmware do ESP32 está em `firmware/esp32/` e é desenvolvido localmente com **PlatformIO**.

### Compilar

```bash
cd firmware/esp32
pio run
```

### Simular com Wokwi

1. Abra a pasta `firmware/esp32/` no VS Code.
2. Pressione `Ctrl+Shift+P` → **Wokwi: Start Simulator**.
3. O ESP32 virtual se conectará ao broker MQTT local.

> **Nota:** Para o simulador Wokwi acessar o Docker, o broker deve estar acessível em `host.docker.internal` ou `localhost`.

---

## 📝 Roadmap

- [x] **Etapa 01** — Preparação do Ambiente
- [x] **Etapa 02** — Estruturação do Repositório
- [ ] **Etapa 03** — Firmware ESP32 (sensores + MQTT)
- [ ] **Etapa 04** — Fluxos Node-RED (regras de automação)
- [ ] **Etapa 05** — Dashboard de monitoramento
- [ ] **Etapa 06** — Integração InterSCity
- [ ] **Etapa 07** — Testes end-to-end e documentação final

---

## 📄 Licença

Este projeto está licenciado sob a licença MIT — veja o arquivo [LICENSE](LICENSE) para detalhes.

---

## 👥 Autores

Projeto desenvolvido como trabalho acadêmico na **Universidade Federal do Maranhão (UFMA)**.
