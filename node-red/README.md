# Node-RED Flows

Esta pasta contém os fluxos exportados do Node-RED para versionamento.

Os fluxos são editados via interface web (http://localhost:1880) e persistidos
automaticamente no volume Docker `docker/nodered/data/`.

## Como exportar fluxos

1. Acesse o editor Node-RED: http://localhost:1880
2. Menu ☰ → Export → Download
3. Salve o arquivo `.json` nesta pasta para versionamento.

## Integração InterSCity via Node-RED

Este repositório já inclui um fluxo que encaminha telemetria MQTT diretamente para a API InterSCity.

### Passos

1. Abra o Node-RED em `http://localhost:1880`.
2. Importe o arquivo `node-red/flows.json` ou carregue o fluxo existente.
3. Ajuste as variáveis de ambiente do fluxo `Automação - 5 Salas`:
   - `INTERSCITY_API_URL`
   - `INTERSCITY_API_TOKEN`
   - `INTERSCITY_TELEMETRY_PATH`
4. Ative o fluxo e verifique os nós `Debug InterSCity` para confirmar o envio.

### O que faz o fluxo

- recebe mensagens MQTT em `ac-iot/+/sensores`
- converte os dados para o formato esperado pela API
- faz POST para `API_URL + TELEMETRY_PATH`
- mostra a resposta no debug do Node-RED
