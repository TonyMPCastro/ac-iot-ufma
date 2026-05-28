# InterSCity bridge

Servico C++ que integra o sistema AC IoT UFMA com uma instancia local da
plataforma InterSCity.

O bridge:

- assina o topico MQTT `ac-iot/+/sensores`;
- garante o cadastro das capabilities usadas pelo simulador;
- cadastra `sala01`, `sala02` e `sala03` como resources do InterSCity;
- envia cada leitura para o Resource Adaptor em `POST /resources/:uuid/data`.

## Como executar

O servico sobe junto com a pilha principal:

```bash
docker compose up -d --build
```

Para acompanhar o bridge:

```bash
docker compose logs -f interscity
```

## Endpoints uteis

- Resource Cataloguer: `http://localhost:3000`
- Resource Adaptor: `http://localhost:3002`
- Data Collector: `http://localhost:4000`
- Actuator Controller: `http://localhost:5000`
- Resource Discoverer: `http://localhost:3004`
- Kong: `http://localhost:8000`
- RabbitMQ Management: `http://localhost:15672`

## Configuracao

O `docker-compose.yml` ja define os valores padrao. Para rodar o bridge fora do
Compose, copie `config.example.env` para `.env` e ajuste os hosts conforme o
ambiente.
