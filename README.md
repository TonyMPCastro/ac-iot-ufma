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
└──────┬───────┘    Comandos IR      └────────┬─────────┘                   └──────▲──────┘
       │                                      │                                    │
       │                                      │ MQTT                               │ HTTP
       │                                      ▼                                    │
       │                             ┌──────────────────┐                          │
       │                             │   Node-RED        │─────────────────────────┘
       │                             │  (Motor de Regras │
       │                             │   + Dashboard)    │
       │                             │   porta 1880      │
       │                             └──────────────────┘
       │
       ▼ WebSockets (porta 9001)
┌───────────────────────────┐
│ Plataforma de Simulação   │
│ (Painel Web e C++)        │
└───────────────────────────┘
```

### Componentes

| Componente | Tecnologia | Execução |
|---|---|---|
| **Sensores + Atuador** | ESP32 + DHT22 + PIR + IR LED | Firmware local (PlatformIO / Wokwi) |
| **Simulador (Backend)** | C++ | Docker |
| **Simulador (Frontend)** | HTML + CSS + JS (Paho MQTT) | Navegador Web |
| **Broker MQTT** | Eclipse Mosquitto 2.x | Docker |
| **Middleware / Dashboard** | Node-RED + node-red-dashboard | Docker |
| **Plataforma IoT** | InterSCity + bridge MQTT/REST | Docker |

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
├── simulador/                  # Simulador C++ que gera 3 salas e envia MQTT
├── simulador-web/              # Interface Web para simulação manual via WebSockets
│   └── index.html
├── firmware/
│   └── esp32/
│       ├── src/
│       │   └── main.cpp        # Código principal do ESP32
│       ├── chips/              # Chips customizados Wokwi
│       ├── platformio.ini      # Configuração PlatformIO
│       ├── diagram.json        # Diagrama do circuito Wokwi
│       └── wokwi.toml          # Config do simulador Wokwi
├── node-red/                   # Fluxos exportados para versionamento
├── interscity/                 # Bridge MQTT -> InterSCity local
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

---

## 🚀 Como Executar o Projeto

Certifique-se de ter o **Docker Desktop** instalado e rodando em sua máquina antes de começar.

### 1. Clonar o repositório

```bash
git clone https://github.com/TonyMPCastro/ac-iot-ufma.git
cd ac-iot-ufma
```

### 2. Iniciar a infraestrutura Docker

Escolha o terminal de acordo com seu sistema operacional:

#### 🪟 Windows
Use o **PowerShell** ou o **Windows Terminal**:
```powershell
docker compose up -d --build
```

#### 🍎 macOS
Use o **Terminal** nativo ou **iTerm2**:
```bash
docker compose up -d --build
```

#### Plataforma InterSCity local

O comando principal tambem sobe a pilha local do InterSCity: Resource Cataloguer, Resource Adaptor, Data Collector, Actuator Controller, Resource Discoverer, Kong, RabbitMQ, Mongo, Postgres e Redis.

O bridge `interscity` assina `ac-iot/+/sensores`, cadastra as salas no InterSCity e encaminha a telemetria para o Resource Adaptor.

Endpoints principais:
- Resource Cataloguer: `http://localhost:3000`
- Resource Adaptor: `http://localhost:3002`
- Data Collector: `http://localhost:4000`
- Actuator Controller: `http://localhost:5000`
- Resource Discoverer: `http://localhost:3004`
- Kong: `http://localhost:8000`
- RabbitMQ Management: `http://localhost:15672`

---

### 3. Verificar o status dos serviços

Após rodar o comando acima, verifique se todos os containers estão ativos e se o Mosquitto está com status `healthy`:

```bash
docker-compose ps
```

### 4. Acessar as Interfaces

*   **Painel de Controle Web:** Abra o arquivo `simulador-web/index.html` diretamente no seu navegador preferido (Chrome, Edge ou Safari).
*   **Node-RED (Fluxos):** Acesse [http://localhost:1880](http://localhost:1880) para gerenciar as automações.

---

## 🎮 Plataforma de Simulação

O projeto agora conta com uma plataforma completa de simulação para testar a comunicação MQTT sem depender de hardware físico real:

### 1. Simulador Automático (C++)

Por padrão, ao rodar `docker compose up -d`, o contêiner `simulador` começa a rodar e envia leituras aleatórias para 3 salas diferentes a cada 20 segundos nos tópicos `ac-iot/salaXX/sensores`.

Para ver os logs do simulador e as mensagens que estão sendo geradas, você pode rodar:

```bash
docker compose logs -f simulador
```

### 2. Painel Web de Simulação Manual

Além do simulador automático, você pode utilizar um **Painel Web Visual** para acompanhar os dados em tempo real e intervir manualmente enviando novos valores.

**Como acessar o Painel Web:**
1. Navegue até a pasta `simulador-web` no seu explorador de arquivos.
2. Abra o arquivo **`index.html`** no seu navegador padrão (Chrome, Firefox, Edge, etc.).
3. O painel se conectará automaticamente ao broker Mosquitto via WebSockets (Porta 9001).

No painel, você verá as leituras sendo atualizadas e poderá usar os sliders para alterar os valores de Temperatura, Umidade e Luminosidade. Após ajustar, clique em **"Publicar Simulação Manual"** e a nova leitura será enviada ao broker MQTT!

**Alternando para o modo "Apenas Manual":**
Se você quiser desligar as atualizações automáticas do simulador C++, pare o contêiner do simulador:

```cmd
cmd /c docker compose stop simulador
```

Para reativar a simulação automática:

```cmd
cmd /c docker compose start simulador
```

---

## 🧪 Tópicos MQTT do Simulador

| Tópico | Direção | Descrição |
|---|---|---|
| `ac-iot/sala01/sensores` | Simulador → Broker | Dados gerados da Sala 01 |
| `ac-iot/sala02/sensores` | Simulador → Broker | Dados gerados da Sala 02 |
| `ac-iot/sala03/sensores` | Simulador → Broker | Dados gerados da Sala 03 |

*Formato do Payload (JSON):*
```json
{
  "id_sala": "sala01",
  "status_ac": "ligado",
  "temperatura": 25.5,
  "umidade": 60.0,
  "luminosidade": 500,
  "timestamp": 1642903200
}
```

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

## 📝 Roadmap

- [x] **Etapa 01** — Preparação do Ambiente
- [x] **Etapa 02** — Estruturação do Repositório
- [x] **Etapa 03** — Plataforma de Simulação (C++ e Web UI)
- [ ] **Etapa 04** — Firmware ESP32 (sensores + MQTT)
- [ ] **Etapa 05** — Fluxos Node-RED (regras de automação)
- [ ] **Etapa 06** — Dashboard de monitoramento
- [ ] **Etapa 07** — Integração InterSCity
- [ ] **Etapa 08** — Testes end-to-end e documentação final

---

## 📄 Licença

Este projeto está licenciado sob a licença MIT — veja o arquivo [LICENSE](LICENSE) para detalhes.

---

## 👥 Autores

Projeto desenvolvido como trabalho acadêmico na **Universidade Federal do Maranhão (UFMA)**.
