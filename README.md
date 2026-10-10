# Sistema de Reserva de Boletos (SRB) — Entrega 1

Arquitectura base de tres capas en C++ con ZeroMQ y SQLite. Implementa **consulta** y **reserva**.

| Programa | Capa | Máquina | Socket(s) |
|---|---|---|---|
| `cliente` | Consumidores | VM1 | REQ → Gestor |
| `gestor` | Lógica de negocio | VM2 | REP :5555 (clientes), REQ → RMC, PUSH → Cancelación |
| `rmc` | Lógica de negocio | VM2 | REP :5556 (gestor), REQ → Persistencia |
| `cancelacion` | Lógica de negocio | VM2 | PULL :5557, PUB :5558 (esqueleto, 2ª entrega) |
| `persistencia` | Almacenamiento | VM3 | REP :6666, SQLite (`reserva.db`) |

## 1. Instalar dependencias (en las 3 VMs, Ubuntu)

```bash
sudo apt update
sudo apt install g++ make libzmq3-dev libsqlite3-dev
sudo apt install cppzmq-dev   # si no existe, ver nota abajo
```

Si `cppzmq-dev` no existe en su versión de Ubuntu, descargue el encabezado en la carpeta del proyecto:
`wget https://raw.githubusercontent.com/zeromq/cppzmq/master/zmq.hpp`

## 2. Compilar (en las 3 VMs)

```bash
make
```

## 3. Configurar las IPs

Edite `config.txt` en las 3 VMs con la IP real de cada una (`ip a`):
`GESTOR_IP` = IP de la VM2, `PERSISTENCIA_IP` = IP de la VM3. `RMC_IP` y `CANCELACION_IP` pueden quedar en `127.0.0.1` porque están en la misma VM que el gestor.

Abra los puertos en el firewall si está activo: `sudo ufw allow 5555:5558/tcp` (VM2) y `sudo ufw allow 6666/tcp` (VM3).

## 4. Ejecutar (en este orden, cada uno en su propia terminal)

```bash
# VM3
./persistencia eventos.txt     # carga los eventos desde cero e imprime el estado inicial
                               # (sin argumento, usa la BD que ya existe)
# VM2
./rmc
./cancelacion
./gestor

# VM1
./cliente C1                                 # menú interactivo
./cliente C1 operaciones_ejemplo.txt 20      # operaciones desde archivo, 20 por minuto
```

Para varios clientes simultáneos, lance varios procesos con ids distintos (`C1`, `C2`, ...).

## 5. Archivos de datos

- `eventos.txt`: eventos iniciales, una línea por ocurrencia: `E5 2026-11-02 100`. Los 50 eventos de noviembre de 2026; E1–E20 tienen dos ocurrencias. Para el caso de prueba del profesor, se reemplaza este archivo y se reinicia `./persistencia eventos.txt`.
- `operaciones_ejemplo.txt`: operaciones del cliente, una por línea: `CONSULTA 2026-11` o `RESERVA E5 2026-11-02 3`.

## 6. Qué se ve en cada consola

- **Persistencia:** el estado completo de la BD al arrancar, cada orden recibida y cómo queda la ocurrencia tras cada reserva. La orden `ESTADO` vuelve a imprimir toda la BD.
- **RMC:** cada solicitud, lo que le pregunta a la persistencia y si responde ÉXITO o RECHAZO.
- **Gestor:** cada solicitud y respuesta, y cada minuto el TPM (también en `tpm.csv`).
- **Cliente:** cada solicitud, su resultado y el tiempo de respuesta (también en `metricas_<id>.csv`).

## 7. Protocolo de mensajes (texto separado por espacios)

| De → A | Mensaje | Respuesta |
|---|---|---|
| Cliente → Gestor → RMC | `CONSULTA <AAAA-MM>` | `EXITO` + lista, o `RECHAZO <motivo>` |
| Cliente → Gestor → RMC | `RESERVA <evento> <AAAA-MM-DD> <cliente> <n>` | `EXITO reserva <id>...` o `RECHAZO <motivo>` |
| Cliente → Gestor → Cancelación | `CANCELACION ...` | `RECIBIDO ...` (2ª entrega) |
| RMC → Persistencia | `EVENTOS <AAAA-MM>` | una línea por ocurrencia |
| RMC → Persistencia | `DISPONIBLES <evento> <fecha>` | asientos libres, o `-1` si no existe |
| RMC → Persistencia | `REGISTRAR <evento> <fecha> <cliente> <n>` | `OK <idReserva>`, `SIN_CUPO` o `NO_EXISTE` |
