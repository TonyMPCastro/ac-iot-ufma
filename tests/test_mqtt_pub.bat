@echo off
REM ============================================
REM Teste de Validação MQTT - ac-iot-ufma
REM Publica uma mensagem JSON no broker Mosquitto
REM ============================================
REM
REM IMPORTANTE: No Windows (CMD/PowerShell), aspas dentro
REM de argumentos Docker causam conflito de escape.
REM A solução é usar "echo ... | docker exec -i ... -l"
REM onde -l lê a mensagem do stdin (pipe).
REM ============================================

echo.
echo === Teste de Publicacao MQTT ===
echo.

REM Verifica se o Docker está rodando
docker ps >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERRO] Docker nao esta em execucao. Inicie o Docker Desktop primeiro.
    pause
    exit /b 1
)

REM Verifica se o container do Mosquitto está ativo
docker ps --filter "name=ac_iot_mosquitto" --format "{{.Names}}" | findstr "ac_iot_mosquitto" >nul
if %errorlevel% neq 0 (
    echo [ERRO] Container 'ac_iot_mosquitto' nao esta rodando.
    echo Execute: docker compose up -d
    pause
    exit /b 1
)

echo [INFO] Broker Mosquitto ativo.
echo [INFO] Publicando mensagem de teste no topico 'ac-iot/sala01/sensores'...
echo.

REM Publica mensagem JSON via pipe (evita problemas de escape no Windows)
echo {"temperatura":26.5,"presenca":true,"corrente":1.5}| docker exec -i ac_iot_mosquitto mosquitto_pub -h localhost -t ac-iot/sala01/sensores -l

if %errorlevel% equ 0 (
    echo.
    echo [OK] Mensagem publicada com sucesso!
    echo.
    echo [DICA] Para confirmar a recepcao, abra OUTRO terminal e execute:
    echo        docker exec ac_iot_mosquitto mosquitto_sub -h localhost -t "ac-iot/#" -v
) else (
    echo.
    echo [ERRO] Falha ao publicar mensagem.
)

echo.
pause
