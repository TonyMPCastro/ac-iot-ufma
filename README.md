# 🌡️ ac-iot-ufma

**Monitorização e Controlo Inteligente de Ar-Condicionado via IoT em Salas de Aula**

Projeto acadêmico desenvolvido na **UFMA** (Universidade Federal do Maranhão) que utiliza IoT para monitorar temperatura, umidade e presença em salas de aula, controlando automaticamente aparelhos de ar-condicionado por infravermelho — com o objetivo de promover eficiência energética e conforto térmico.

---

## 📐 Arquitetura do Sistema (Nova Arquitetura Nuvem-First)

A arquitetura evoluiu de um modelo focado em MQTT local para uma integração direta com a plataforma em nuvem InterSCity via **APIs REST**. Todo o tráfego de telemetria e comandos agora flui centralizadamente através da UFMA.

```
┌───────────────────────────┐         HTTP REST (POST)       ┌────────────────────────┐
│ Plataforma de Simulação   │ ─────────────────────────────► │   InterSCity (UFMA)    │
│ C++ (Motor de Física)     │ ◄───────────────────────────── │      (API REST)        │
│ HTML/JS (Painel de Testes)│         HTTP REST (GET)        │                        │
└───────────────────────────┘                                └──────────┬─────────────┘
                                                                        │
                                                                        │ HTTP REST
                                                                        ▼
                                                             ┌────────────────────────┐
                                                             │      Node-RED          │
                                                             │   (Motor de Regras     │
                                                             │    + Automação)        │
                                                             │     porta 1880         │
                                                             └────────────────────────┘
```

### Componentes Atualizados

| Componente | Tecnologia | Responsabilidade |
|---|---|---|
| **Plataforma IoT (Nuvem)** | InterSCity (UFMA) | Receber telemetria de todas as salas, hospedar capacidades (temperatura, luz, presença, etc) e armazenar o estado histórico. |
| **Simulador de IoT (Backend)** | C++ (Docker) | Simula uma "Sala Física". Implementa inércia térmica realística, onde a temperatura sobe sem ar-condicionado e cai em direção ao setpoint quando ligado. Publica e consome dados REST do InterSCity a cada 3~10 segundos. |
| **Painel de Controle UI** | HTML5 + JS puro | Permite simulação manual. Lê os sensores da nuvem e permite "forçar" atuadores e sensores no simulador por REST. |
| **Middleware / Automação** | Node-RED (Docker) | Faz polling da nuvem e possui fluxos para ligar/desligar automaticamente o AC com base em presença, e timers de 1 minuto para desligar quando vazia. |

---

## 📁 Estrutura do Repositório

```
ac-iot-ufma/
├── docker-compose.yml          # Orquestração dos containers (Simulador + Node-RED)
├── docker/
│   └── nodered/
│       └── data/               # Volume persistente com os fluxos JSON atualizados
├── simulador/                  # Motor de Simulação C++ Integrado via CURL (REST)
│   ├── main.cpp                # Lógica central: Integração REST e Motor Físico Térmico
│   └── Dockerfile              # Build multi-stage C++
├── simulador-web/              # Painel de controle Web e Visualização de Dados
│   └── index.html              # Usa AJAX/Fetch para ler e escrever na Nuvem InterSCity
├── node-red/                   # Backups dos fluxos do Node-RED para versionamento
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

```cmd
git clone https://github.com/TonyMPCastro/ac-iot-ufma.git
cd ac-iot-ufma
```

### 2. Iniciar a infraestrutura Docker

Inicie os contêineres do Simulador C++ e do Node-RED utilizando o docker-compose:

```cmd
docker-compose up -d --build
```

O contêiner `ac_iot_simulador` se conectará imediatamente à API do InterSCity da UFMA e começará a publicar dados termodinâmicos da Sala 01 (UUID `00000000-0000-0000-0000-000000000001`).

### 3. Verificar o status e Logs do Simulador

O Simulador C++ agora possui um motor de física que responde a comandos na rede. Para observar a "Física" agindo na sala (ex: temperatura caindo após o AC ligar):

```cmd
docker logs -f ac_iot_simulador
```

### 4. Acessar as Interfaces e Painéis

*   **Painel de Controle Web:** Abra o arquivo `simulador-web/index.html` diretamente no seu navegador. Os dados carregam direto da nuvem da UFMA e você pode forçar estados nos sensores arrastando os botões deslizantes.
*   **Automação Node-RED:** Acesse [http://localhost:1880](http://localhost:1880). A aba **Automação** possui o cérebro que monitora o InterSCity a cada 5 segundos e decide se o AC deve ser ligado ou desligado. E a aba **Dashboard** disponibiliza a interface de usuário do Node-RED em [http://localhost:1880/ui](http://localhost:1880/ui).

---

## 🎮 O Motor de Simulação Térmica (C++)

O coração do nosso simulador foi completamente reescrito para responder ao ambiente real:
*   **Inércia Térmica:** O ar-condicionado não reduz a temperatura para 23°C magicamente de um segundo para o outro. A temperatura decai 0.5°C por segundo de aproximação se ligado, e sobe 0.3°C caso a sala esteja quente e o ar desligado.
*   **Buscador Inteligente:** A cada 3 segundos, o motor C++ pergunta à UFMA: "O usuário me enviou algum comando?". Se enviou um novo `cmd_setpoint_ac`, o simulador recalcula a física com base na nova temperatura alvo.
*   **Recuperação de Crash:** Se o C++ for reiniciado, ele não perde os dados. Ele bate na nuvem e diz "Onde eu parei?", recuperando todos os valores da UFMA antes de ligar a física novamente.

Para desligar o simulador provisoriamente:
```cmd
cmd /c docker-compose stop simulador
```

---

## 📝 Roadmap

- [x] **Etapa 01** — Preparação do Ambiente
- [x] **Etapa 02** — Estruturação do Repositório
- [x] **Etapa 03** — Plataforma de Simulação C++ 
- [x] **Etapa 04** — Motor Físico Contínuo de Termodinâmica
- [x] **Etapa 05** — Migração do Tráfego MQTT para APIs REST (InterSCity)
- [x] **Etapa 06** — Fluxos Node-RED reconstruídos e Automação Reativa
- [ ] **Etapa 08** — Testes end-to-end finais

---

## 📄 Licença

Este projeto está licenciado sob a licença MIT — veja o arquivo [LICENSE](LICENSE) para detalhes.

---

## 👥 Autores

Projeto desenvolvido como trabalho acadêmico na **Universidade Federal do Maranhão (UFMA)**.
