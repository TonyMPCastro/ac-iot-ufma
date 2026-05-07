# Códigos IR

Catálogo de códigos infravermelhos para modelos de ar-condicionado.

## Formato

Cada arquivo `.json` contém os códigos raw IR para um modelo específico:

```json
{
  "marca": "Samsung",
  "modelo": "AR09TSHZ",
  "codigos": {
    "ligar":    [9000, 4500, 560, 560, ...],
    "desligar": [9000, 4500, 560, 1690, ...],
    "temp_22":  [9000, 4500, 560, 560, ...],
    "temp_24":  [9000, 4500, 560, 560, ...]
  }
}
```
