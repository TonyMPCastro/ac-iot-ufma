# Node-RED Flows

Esta pasta contem os fluxos exportados do Node-RED para versionamento.

O Node-RED usa `docker/nodered/data/flows.json` em tempo de execucao. Este
arquivo em `node-red/flows.json` fica como copia versionada/exportavel dos
fluxos.

## Como acessar

1. Suba o ambiente com `docker compose up -d --build`.
2. Acesse o editor Node-RED em `http://localhost:1880`.
3. Abra a aba `InterSCity - Ver e Testar`.
4. Use o painel de debug do Node-RED para ver as respostas dos testes.

## Aba InterSCity - Ver e Testar

Esta aba permite consultar e testar a plataforma InterSCity local sem sair do
Node-RED.

### Consultas disponiveis

- `Listar capabilities`: consulta `GET /capabilities` no Resource Cataloguer.
- `Listar salas/resources`: consulta `GET /resources` no Resource Cataloguer.
- `Ultima telemetria sala01`, `sala02` e `sala03`: consulta `GET /resources/:uuid/data/last` no Data Collector.
- `Status Cataloguer` e `Status Data Collector`: verifica se os servicos InterSCity respondem pela rede Docker.

### Testes disponiveis

- `Teste MQTT sala01`, `sala02` e `sala03`: publica uma leitura em `ac-iot/<sala>/sensores`, passando pelo mesmo caminho usado pelo simulador e pelo bridge MQTT -> InterSCity.
- `POST direto sala01`, `sala02` e `sala03`: envia uma leitura diretamente para `POST /resources/:uuid/data` no Resource Adaptor.

## Como exportar mudancas

1. Acesse o editor Node-RED: `http://localhost:1880`.
2. Menu, Export, Download.
3. Salve o JSON exportado em `node-red/flows.json`.

## Evitando erro EBUSY

Nao monte `node-red/flows.json` diretamente em `/data/flows.json`. O Node-RED
salva criando um arquivo temporario e renomeando para `flows.json`; em Docker
Desktop/Windows esse rename pode falhar quando o arquivo individual esta
montado.
