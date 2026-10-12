# Sistema de Reserva de Boletos (SRB) — Entrega 1

Arquitectura base de tres capas en C++ con ZeroMQ y SQLite. Implementa **consulta** y **reserva**.

| Programa | Capa | Máquina | Sockets |
|---|---|---|---|
| `cliente` | Consumidores | VM1 | REQ → Gestor |
| `gestor` | Lógica de negocio | VM2 | REP :5555, REQ → RMC, PUSH → Cancelación |
| `rmc` | Lógica de negocio | VM2 | REP :5556, REQ → Persistencia |
| `cancelaciones` | Lógica de negocio | VM2 | PULL :5557, REQ → Persistencia |
| `persistencia` | Almacenamiento | VM3 | REP :6666, SQLite `reserva.db` |

## 1. Instalar (en las 3 VMs)
```bash
sudo apt install g++ make libzmq3-dev libsqlite3-dev
ls /usr/include/zmq.hpp || wget https://raw.githubusercontent.com/zeromq/cppzmq/master/zmq.hpp
```

## 2. Compilar (en las 3 VMs)
```bash
make
```

## 3. IPs
Las IPs están escritas en cada programa:
- `cliente.cpp` se conecta al gestor en `10.43.100.44:5555` (VM2).
- `rmc.cpp` y `cancelaciones.cpp` se conectan a la persistencia en `10.43.99.231:6666` (VM3).
- `gestor.cpp` se conecta al RMC (`localhost:5556`) y a cancelaciones (`localhost:5557`), que están en su misma VM.

Si cambian las VMs, se cambian esas líneas y se vuelve a compilar con `make`.
Si hay firewall: `sudo ufw allow 5555:5557/tcp` (VM2) y `sudo ufw allow 6666/tcp` (VM3).

## 4. Ejecutar (en este orden, cada uno en su terminal)
```bash
# VM3
./persistencia                 # usa reserva.db; si está vacía carga init_db.sql (50 eventos)
./persistencia init_db.sql     # o: borra todo y recarga desde el archivo (caso del profesor)
# VM2
./rmc
./cancelaciones
./gestor
# VM1
./cliente                               # menú: consulta, reserva, eliminar
./cliente operaciones_ejemplo.txt 20    # operaciones desde archivo, 20 por minuto
```
Para varios clientes simultáneos se lanzan varios procesos `./cliente`, cada uno con su archivo.

## 5. Datos
- `init_db.sql`: crea las tablas y carga 50 eventos de noviembre de 2026, con 100 asientos cada ocurrencia. E1–E20 tienen dos fechas. Para el caso de prueba del profesor, se copia el archivo, se cambian los `INSERT` y se ejecuta `./persistencia caso.sql`.
- `operaciones_ejemplo.txt`: una operación por línea, con el mismo formato de los mensajes (sección 6).

## 6. Mensajes
| De → A | Mensaje | Respuesta |
|---|---|---|
| Cliente → Gestor → RMC | `CONSULTA <AAAA-MM>` | `EXITO` + lista, o `RECHAZO: ...` |
| Cliente → Gestor → RMC | `RESERVA <evento> <AAAA-MM-DD> <cliente> <n>` | `EXITO: reserva ...` o `RECHAZO: ...` |
| Cliente → Gestor → Cancelación | `ELIMINAR <evento> <fecha> <cliente>` | `RECIBIDO` inmediato (asíncrono) |
| RMC → Persistencia | `CONSULTA <mes>` · `DISPONIBLES <evento> <fecha>` · `RESERVAR <evento> <fecha> <cliente> <n>` | lista · asientos libres (-1 si no existe) · `OK <id>` o `SIN_CUPO` |
| Cancelación → Persistencia | `ELIMINAR <evento> <fecha> <cliente>` | asientos liberados |

## 7. Qué muestra cada consola
- **Persistencia:** cada orden que recibe, y la BD completa (tabla de eventos y tabla de reservas) al arrancar y después de cada reserva o cancelación (`Evento: E5 | Fecha: 2026-11-02 | Libres: 97 | Ocupados: 3` y `Reserva 1 | Evento: E5 | Fecha: 2026-11-02 | Cliente: juan | Asientos: 3`).
- **RMC:** cada solicitud, lo que le pregunta a la persistencia y el resultado (ÉXITO o RECHAZO).
- **Gestor:** cada solicitud y respuesta; cada minuto el TPM.
- **Cliente:** el resultado y el tiempo de respuesta de cada solicitud (también en `metricas_<archivo>.csv`).
