FROM python:3.9-slim

WORKDIR /app

# Copia os requisitos e instala
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# Copia o script do simulador
COPY simulador.py .

# Executa o simulador
CMD ["python", "simulador.py"]