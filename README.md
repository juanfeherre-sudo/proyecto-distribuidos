# Sistema de Reserva de Boletos (SRB) — Entrega 2 (en progreso)

Arquitectura de tres capas en C++ con ZeroMQ y SQLite. Implementa **consulta**, **reserva** y **cancelación**.
En la VM2 corren dos gestores (principal y respaldo) y varios RMC que se reparten el trabajo.

| Programa | Capa | Máquina | Sockets |
|---|---|---|---|
| `cliente` | Consumidores | VM1 | REQ → Gestor |
| `gestor` | Lógica de negocio | VM2 | REP :5555 a clientes, REQ bind :5556 a los RMC, PUSH → Cancelación |
| `gestor 2` | Lógica de negocio | VM2 | Respaldo: REP :5565 a clientes, REQ bind :5566 a los RMC |
| `rmc N` | Lógica de negocio | VM2 | REP con connect a :5556 y :5566, REQ → Persistencia |
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

## 3. IPs y puertos: `config.txt`
Todos los programas leen `config.txt` al arrancar, así que **el mismo archivo va en las 3 VMs** y debe estar en la carpeta desde donde se ejecuta cada programa.
- `GESTOR_IP` y `CANCELACION_IP`: IP de la VM2.
- `PERSISTENCIA_IP`: IP de la VM3.

Si cambian las VMs, se cambian esas líneas (`hostname -I` en cada VM) y se reinician los procesos; **no hace falta recompilar**.
Si hay firewall: `sudo ufw allow 5555:5557/tcp` y `sudo ufw allow 5565:5566/tcp` (VM2), y `sudo ufw allow 6666/tcp` (VM3).

## 4. Ejecutar (en este orden, cada uno en su terminal)
```bash
# VM3
./persistencia                 # usa reserva.db; si está vacía carga init_db.sql (50 eventos)
./persistencia init_db.sql     # o: borra todo y recarga desde el archivo (caso del profesor)
# VM2 (los gestores primero, porque son los que escuchan; luego los RMC se conectan a ellos)
./gestor                       # principal
./gestor 2                     # respaldo
./rmc 1
./rmc 2
./rmc 3                        # se pueden levantar los que se quieran
./cancelaciones
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

## 7. Comprobar que la VM2 quedó bien
```bash
ss -ltnp | grep -E '5555|5556|5557|5565|5566'   # los 5 puertos: 4 de gestor y 1 de cancelaciones
pgrep -a rmc                                    # un proceso por cada RMC
```
Los RMC no aparecen en `ss` porque no escuchan: se conectan a los gestores.

## 8. Qué muestra cada consola
- **Persistencia:** cada orden que recibe, y la BD completa (tabla de eventos y tabla de reservas) al arrancar y después de cada reserva o cancelación (`Evento: E5 | Fecha: 2026-11-02 | Libres: 97 | Ocupados: 3` y `Reserva 1 | Evento: E5 | Fecha: 2026-11-02 | Cliente: juan | Asientos: 3`).
- **RMC:** cada solicitud, lo que le pregunta a la persistencia y el resultado (ÉXITO o RECHAZO).
- **Gestor:** cada solicitud y respuesta; cada minuto el TPM.
- **Cliente:** el resultado y el tiempo de respuesta de cada solicitud (también en `metricas_<archivo>.csv`).
