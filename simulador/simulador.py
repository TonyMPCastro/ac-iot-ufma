import paho.mqtt.client as mqtt
import time
import json
import random
import os

# ---------------------------------------------------------
# Configurações do Broker MQTT
# ---------------------------------------------------------
BROKER = os.environ.get("MQTT_BROKER", "mosquitto")
PORT = int(os.environ.get("MQTT_PORT", 1883))
INTERVALO = int(os.environ.get("PUBLISH_INTERVAL", 60))

# ---------------------------------------------------------
# Configuração das Salas Simuladas
# ---------------------------------------------------------
SALAS = {
    "sala01": {
        "id": "sala01",
        "topico": "ac-iot/sala01/sensores",
        "status": "ligado",
        "setpoint": 22.0,
        "luz": "desligado",
        "temp_min": 20.0, "temp_max": 25.0, 
        "umidade_min": 40.0, "umidade_max": 50.0,
        "luz_min": 300, "luz_max": 500
    },
    "sala02": {
        "id": "sala02",
        "topico": "ac-iot/sala02/sensores",
        "status": "ligado",
        "setpoint": 24.0,
        "luz": "desligado",
        "temp_min": 25.0, "temp_max": 35.0,
        "umidade_min": 50.0, "umidade_max": 70.0,
        "luz_min": 800, "luz_max": 1000
    },
    "sala03": {
        "id": "sala03",
        "topico": "ac-iot/sala03/sensores",
        "status": "ligado",
        "setpoint": 23.0,
        "luz": "desligado",
        "temp_min": 22.0, "temp_max": 28.0,
        "umidade_min": 45.0, "umidade_max": 60.0,
        "luz_min": 100, "luz_max": 800
    }
}

def on_connect(client, userdata, flags, rc):
    """Callback de conexão com o broker MQTT."""
    if rc == 0:
        print(f"Conectado com sucesso ao Broker MQTT em {BROKER}:{PORT}", flush=True)
        # Inscreve nos tópicos de comando individuais e globais
        client.subscribe("ac-iot/+/comando")
        client.subscribe("ac-iot/all/comando")
        print("Inscrito em ac-iot/+/comando e ac-iot/all/comando.", flush=True)
        
        # Publica o estado inicial de todas as salas para descoberta imediata no painel
        print("Publicando estado inicial de todas as salas...", flush=True)
        for id_sala, config in SALAS.items():
            dados = gerar_dados(config)
            client.publish(config["topico"], json.dumps(dados), retain=True)
    else:
        print(f"Falha ao conectar, código de retorno: {rc}", flush=True)

def on_message(client, userdata, msg):
    """Callback chamado quando uma mensagem MQTT é recebida."""
    try:
        payload = msg.payload.decode('utf-8')
        dados = json.loads(payload)
        
        partes = msg.topic.split("/")
        if len(partes) >= 2:
            alvo = partes[1]
            
            # Lista de salas que serão afetadas
            salas_para_atualizar = []
            if alvo == "all":
                salas_para_atualizar = list(SALAS.keys())
            elif alvo in SALAS:
                salas_para_atualizar = [alvo]
            
            for id_sala in salas_para_atualizar:
                mudou = False
                
                # Controle de AC (Ligar/Desligar)
                if "comando" in dados:
                    cmd = dados["comando"]
                    if cmd in ["ligar", "desligar"]:
                        novo_status = "ligado" if cmd == "ligar" else "desligado"
                        if SALAS[id_sala]["status"] != novo_status:
                            SALAS[id_sala]["status"] = novo_status
                            mudou = True
                
                # Controle de Temperatura (Setpoint)
                if "setpoint" in dados:
                    SALAS[id_sala]["setpoint"] = float(dados["setpoint"])
                    mudou = True
                
                # Controle de Luz
                if "luz" in dados:
                    cmd_luz = dados["luz"]
                    print(f"[DEBUG] Recebido comando luz='{cmd_luz}' para {id_sala}", flush=True)
                    if cmd_luz in ["ligar", "desligar"]:
                        novo_status_luz = "ligado" if cmd_luz == "ligar" else "desligado"
                        if SALAS[id_sala]["luz"] != novo_status_luz:
                            SALAS[id_sala]["luz"] = novo_status_luz
                            mudou = True

                if mudou:
                    print(f"[COMANDO] {id_sala.upper()} atualizado: AC={SALAS[id_sala]['status']}, Setpoint={SALAS[id_sala]['setpoint']}°C, Luz={SALAS[id_sala]['luz']}", flush=True)
                
                # Sempre publica o estado atualizado (ou mantido) para sincronizar o painel web
                dados_atualizados = gerar_dados(SALAS[id_sala])
                client.publish(SALAS[id_sala]["topico"], json.dumps(dados_atualizados), retain=True)
            
            if alvo != "all" and alvo not in SALAS:
                print(f"[AVISO] Sala '{alvo}' não encontrada.", flush=True)
    except Exception as e:
        print(f"Erro ao processar mensagem: {e}", flush=True)

def gerar_dados(sala):
    """Gera dados realistas (randômicos) com base no estado da sala."""
    # Simulação simples de temperatura baseada no setpoint se estiver ligado
    if sala["status"] == "desligado":
        # Se desligado, a temperatura tende ao ambiente (mais alta no simulador)
        temperatura = round(random.uniform(sala["temp_max"], sala["temp_max"] + 2.0), 2)
    else:
        # Se ligado, a temperatura oscila em torno do setpoint
        temperatura = round(random.uniform(sala["setpoint"] - 0.5, sala["setpoint"] + 0.5), 2)
        
    umidade = round(random.uniform(sala["umidade_min"], sala["umidade_max"]), 2)
    
    # Se a luz estiver ligada, a luminosidade é alta. Se desligada, é quase zero.
    if sala["luz"] == "ligado":
        luminosidade = int(random.uniform(800, 1100))
    else:
        # Quando desligada, fica bem escuro (ambiente real)
        luminosidade = int(random.uniform(5, 50))
    
    return {
        "id_sala": sala["id"],
        "status_ac": sala["status"],
        "setpoint_ac": sala["setpoint"],
        "status_luz": sala["luz"],
        "temperatura": temperatura,
        "umidade": umidade,
        "luminosidade": luminosidade,
        "timestamp": int(time.time())
    }

def main():
    client = mqtt.Client(client_id="simulador_esp32_multisala")
    client.on_connect = on_connect
    client.on_message = on_message
    
    # Aguarda o broker ficar disponível, tentando conectar repetidamente
    while True:
        try:
            print(f"Tentando conectar ao broker {BROKER}:{PORT}...")
            client.connect(BROKER, PORT, 60)
            break
        except Exception as e:
            print(f"Erro ao conectar: {e}. Tentando novamente em 5 segundos...")
            time.sleep(5)

    # Inicia a thread de rede do MQTT
    client.loop_start()

    print(f"Iniciando simulação. Enviando dados a cada {INTERVALO} segundos...")
    try:
        while True:
            for sala in SALAS.values():
                # Gera o dicionário de dados da sala
                dados = gerar_dados(sala)
                
                # Converte para string JSON
                payload = json.dumps(dados)
                
                # Publica no MQTT com flag retain=True para que novos clientes recebam o último estado
                client.publish(sala["topico"], payload, retain=True)
                print(f"[{sala['topico']}] {payload}")
                
            # Aguarda o intervalo definido para enviar os próximos dados
            time.sleep(INTERVALO)
            
    except KeyboardInterrupt:
        print("\nSimulação interrompida pelo usuário.")
    finally:
        client.loop_stop()
        client.disconnect()
        print("Desconectado do Broker MQTT.")

if __name__ == "__main__":
    main()
