# InterSCity

Arquivos de integração com a plataforma **InterSCity** (API externa).

Contém:
- Serviço Docker local em C++ para receber telemetria do Node-RED.
- Proxy HTTP para encaminhar payloads ao InterSCity remoto.
- Exemplo de configuração `.env`.

## Como rodar localmente em Docker

1. Copie o exemplo de configuração:

```bash
copy interscity\config.example.env interscity\.env
```

2. Ajuste `API_TOKEN` e, se necessário, `REMOTE_API_URL`.

3. Inicie o serviço com Docker Compose:

```bash
docker compose up -d interscity
```

4. Verifique o serviço:

```bash
docker compose logs -f interscity
```

5. No Node-RED, aponte o nó HTTP para `http://interscity:5000/telemetry`.
