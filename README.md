# Sistema de Reserva de Boletos (SRB) — Entrega 2

Arquitectura de tres capas en C++ con ZeroMQ y SQLite. Implementa **consulta**, **reserva** y **modificación** (síncronas) y **cancelación** (asíncrona, con notificación al cliente).
En la VM2 corren dos gestores (principal y respaldo) y varios RMC que se reparten el trabajo. Ningún proceso espera para siempre (Lazy Pirate), un reintento nunca se aplica dos veces (`idSol`) y systemd relanza lo que se cae (Watchdog).

| Programa | Capa | Máquina | Sockets |
|---|---|---|---|
| `cliente` | Consumidores | VM1 | REQ → Gestor (5555 o 5565), SUB ← notificaciones (:5558) |
| `gestor` | Lógica de negocio | VM2 | REP :5555 a clientes, REQ bind :5556 a los RMC, PUSH → Cancelación |
| `gestor 2` | Lógica de negocio | VM2 | Respaldo: REP :5565 a clientes, REQ bind :5566 a los RMC |
| `rmc N` | Lógica de negocio | VM2 | REP con connect a :5556 y :5566, REQ → Persistencia |
| `cancelaciones` | Lógica de negocio | VM2 | PULL :5557, REQ → Persistencia, PUB :5558 a los clientes |
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
Si hay firewall: `sudo ufw allow 5555:5558/tcp` y `sudo ufw allow 5565:5566/tcp` (VM2), y `sudo ufw allow 6666/tcp` (VM3).

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
./cliente                               # una solicitud por línea: RESERVA E1 2026-11-18 laura 2
./cliente operaciones_ejemplo.txt 20    # operaciones desde archivo, 20 por minuto
./cliente carga C1 50 50 0 60 20        # carga al azar: cliente C1, 50 % consulta, 50 % reserva,
                                        # 0 % modificación, 60 operaciones, 20 por minuto
```
Para varios clientes simultáneos se lanzan varios procesos `./cliente`, cada uno con su archivo o su nombre de carga (el CSV de métricas se llama como el archivo o el nombre, así no se pisan).

**Modo carga (experimentos):** las consultas son de noviembre de 2026; las reservas, de 1 a 3 asientos en una ocurrencia al azar; las modificaciones cambian una reserva que ese cliente ya tiene, a otra fecha del mismo evento o a otro número de asientos (si todavía no tiene ninguna, esa operación sale como reserva). Ejemplo, escenario B de la Tabla 1: tres procesos `./cliente carga Cn 100 0 0 ...` y dos `./cliente carga Cn 0 50 50 ...`.

## 5. Datos
- `init_db.sql`: crea las tablas y carga 50 eventos de noviembre de 2026, con 100 asientos cada ocurrencia. E1–E20 tienen dos fechas. Para el caso de prueba del profesor, se copia el archivo, se cambian los `INSERT` y se ejecuta `./persistencia caso.sql`.
- `operaciones_ejemplo.txt`: una operación por línea, con el mismo formato que se escribe en la terminal (sección 6).
- `reserva.db` tiene, además de `Eventos` y `Reservas`, la tabla `Solicitudes` (`idSol`, respuesta): las escrituras ya aplicadas, para no repetirlas.

## 6. Mensajes
En la terminal (y en los archivos) se escribe la solicitud sin `idSol`; el cliente le agrega uno único después del tipo (`RESERVA a3f9c1-7 E1 ...`) y lo conserva en los reintentos.

| De → A | Mensaje | Respuesta |
|---|---|---|
| Cliente → Gestor → RMC | `CONSULTA <idSol> <AAAA-MM>` | `EXITO` + lista, o `RECHAZO: ...` |
| Cliente → Gestor → RMC | `RESERVA <idSol> <evento> <AAAA-MM-DD> <cliente> <n>` | `EXITO: reserva ...` o `RECHAZO: ...` |
| Cliente → Gestor → RMC | `MODIFICACION <idSol> <evento> <fecha> <cliente> <nuevaFecha> <n>` | `EXITO: reserva ... ahora es de ...` o `RECHAZO: ...` (la reserva queda como estaba) |
| Cliente → Gestor → Cancelación | `ELIMINAR <idSol> <evento> <fecha> <cliente>` | `RECIBIDO` inmediato (asíncrono) |
| Cancelación → Clientes (PUB) | `<cliente> CANCELACION <evento> <fecha>: <resultado>` | — (el cliente está suscrito al tema `"<cliente> "`) |
| RMC → Persistencia | `CONSULTA <mes>` · `DISPONIBLES <evento> <fecha>` | lista · asientos libres (-1 si no existe) |
| RMC → Persistencia | `RESERVAR <idSol> <evento> <fecha> <cliente> <n>` | `OK <id>` o `SIN_CUPO` |
| RMC → Persistencia | `MODIFICAR <idSol> <evento> <fecha> <cliente> <nuevaFecha> <n>` | `OK <id> <asientosAntes>`, `NO_EXISTE`, `NO_OCURRENCIA` o `SIN_CUPO <libres>` |
| Cancelación → Persistencia | `ELIMINAR <idSol> <evento> <fecha> <cliente>` | asientos liberados, o que no había reserva |

La **modificación** cambia la reserva de ese cliente en ese evento y fecha a otra fecha del mismo evento (o la misma) y/o a otro número de asientos. Se hace en una transacción: primero devuelve los asientos que tenía y luego toma los nuevos; si no alcanzan, se deshace todo.

## 7. Comprobar que la VM2 quedó bien
```bash
ss -ltnp | grep -E '5555|5556|5557|5565|5566'   # los 5 puertos: 4 de gestor y 1 de cancelaciones
pgrep -a rmc                                    # un proceso por cada RMC
```
Los RMC no aparecen en `ss` porque no escuchan: se conectan a los gestores.

## 8. Tolerancia a fallas (Lazy Pirate)
Ningún REQ espera para siempre: si no llega la respuesta a tiempo, se reenvía la solicitud (tiempos e intentos en `config.txt`).

| Si se cae... | Qué pasa | Cuando vuelve |
|---|---|---|
| el gestor principal | el cliente espera 5 s y reenvía la misma solicitud al gestor 2 | los clientes nuevos vuelven a usarlo; los que ya cambiaron siguen en el 2 |
| un RMC | el gestor espera 2 s y reenvía la solicitud, que le llega a otro RMC | se conecta solo a los gestores y vuelve a recibir trabajo |
| la persistencia | el RMC responde `RECHAZO: la persistencia no responde` en 1 s, sin colgarse | todo sigue funcionando sin reiniciar nada más |
| cancelaciones | el gestor deja las cancelaciones en cola (PUSH) | las procesa al volver |

**Sin duplicados (`idSol`):** reenviar es seguro porque cada escritura lleva el `idSol` que puso el cliente. La persistencia guarda en la tabla `Solicitudes` cada escritura aplicada con su respuesta, en la misma transacción; si llega otra vez (un reintento, o un RMC que se congeló con solicitudes en cola y despierta después), no la aplica y devuelve la respuesta de la primera vez. En el log aparece `Solicitud <idSol> repetida: no se aplica otra vez`.

## 9. Supervisor: systemd relanza los procesos (Watchdog)
Lazy Pirate aguanta la caída; **systemd** vuelve a arrancar el proceso. Cada programa es un servicio con `Restart=always` (si muere, se relanza en 1 s) y `WatchdogSec` (si se traba y deja de avisar `WATCHDOG=1` durante 5 s, o 10 s el gestor, systemd lo mata y lo relanza). Corridos a mano en una terminal, los programas funcionan igual que antes.

```bash
make
sudo ./servicios/instalar.sh vm2      # en la VM2: gestor@1, gestor@2, rmc@1..3, cancelaciones
sudo ./servicios/instalar.sh vm3      # en la VM3: persistencia
sudo ./servicios/instalar.sh quitar   # volver a correr todo a mano
```

| Para... | Comando |
|---|---|
| ver todo en vivo: estado, PID, reinicios y, en la VM3, la BD | `./servicios/estado.sh` (necesita `sqlite3` para la BD) |
| ver si están vivos y su PID | `systemctl status 'srb-*'` |
| ver los mensajes de uno en vivo | `journalctl -fu srb-gestor@1` |
| matar uno (prueba) | `sudo kill <PID>` o `sudo kill -9 <PID>`: a 1 s aparece con otro PID |
| trabar uno (prueba del watchdog) | `sudo kill -STOP <PID>`: a los 5 s systemd lo mata y lo relanza |
| detenerlo de verdad | `sudo systemctl stop srb-rmc@2` (con `stop` no se relanza) |

Mientras los servicios estén activos no se corren los programas a mano en esa VM: chocarían por los puertos. Si se cambia `config.txt` o se recompila, `sudo systemctl restart 'srb-*'`.

## 10. Qué muestra cada consola
- **Persistencia:** cada orden que recibe, y la BD completa (tabla de eventos y tabla de reservas) al arrancar y después de cada reserva o cancelación (`Evento: E5 | Fecha: 2026-11-02 | Libres: 97 | Ocupados: 3` y `Reserva 1 | Evento: E5 | Fecha: 2026-11-02 | Cliente: juan | Asientos: 3`).
- **RMC:** cada solicitud, lo que le pregunta a la persistencia y el resultado (ÉXITO o RECHAZO).
- **Gestor:** cada solicitud y respuesta; cada minuto el TPM, que también se guarda en `tpm_gestor1.csv` (o `tpm_gestor2.csv`): `hora,consultas,reservas,modificaciones`.
- **Cancelaciones:** cada cancelación, su resultado y la notificación publicada.
- **Cliente:** el resultado y el tiempo de respuesta de cada solicitud (también en `metricas_<archivo o nombre>.csv`: `operacion,ms,resultado`) y las notificaciones como `[Notificacion] ...`.
