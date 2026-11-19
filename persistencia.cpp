// Persistencia (VM3): unico proceso que maneja la base de datos reserva.db
// ./persistencia init_db.sql   -> carga la base de datos desde cero
// ./persistencia               -> usa la BD que ya hay (si esta vacia carga init_db.sql)
//
// Ordenes que recibe (REP):
//   CONSULTA <mes>                                         -> una linea por evento del mes
//   DISPONIBLES <evento> <fecha>                           -> asientos libres, -1 si no existe
//   RESERVAR  <idSol> <evento> <fecha> <cliente> <n>       -> OK <idReserva> | SIN_CUPO
//   MODIFICAR <idSol> <evento> <fecha> <cliente> <nuevaFecha> <n>
//                                       -> OK <idReserva> <asientosAntes> | NO_EXISTE | NO_OCURRENCIA | SIN_CUPO <libres>
//   ELIMINAR  <idSol> <evento> <fecha> <cliente>           -> Reserva cancelada... | No se encontro reserva...
//
// Las escrituras llevan el idSol de la solicitud: si la misma solicitud llega dos veces
// (un reintento de Lazy Pirate), no se aplica otra vez: se devuelve la respuesta de la primera.

#include <zmq.hpp>
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <functional>
#include <optional>
#include <sqlite3.h>
#include "common.hpp"

const char* BD = "reserva.db";

// ejecuta el archivo .sql (crea las tablas y mete los eventos)
void cargarBD(const std::string& archivoSql) {
    std::ifstream archivo(archivoSql);
    if (!archivo) {
        std::cout << "No se encontro " << archivoSql << std::endl;
        exit(1);
    }
    std::stringstream contenido;
    contenido << archivo.rdbuf();

    sqlite3* db;
    sqlite3_open(BD, &db);
    sqlite3_exec(db, contenido.str().c_str(), nullptr, nullptr, nullptr);
    // la BD empieza de cero: tambien se olvidan las solicitudes ya aplicadas
    sqlite3_exec(db, "DROP TABLE IF EXISTS Solicitudes;", nullptr, nullptr, nullptr);
    sqlite3_close(db);
    std::cout << "BD cargada desde " << archivoSql << std::endl;
}

// Tabla con las solicitudes de escritura ya aplicadas y lo que se respondio.
// Es lo que evita duplicados cuando una solicitud se reenvia.
void prepararSolicitudes() {
    sqlite3* db;
    sqlite3_open(BD, &db);
    sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS Solicitudes (idSol TEXT PRIMARY KEY, respuesta TEXT);",
                 nullptr, nullptr, nullptr);
    sqlite3_close(db);
}

// cuantas filas hay en Eventos (-1 si la tabla no existe)
int contarEventos() {
    sqlite3* db;
    sqlite3_open(BD, &db);
    sqlite3_stmt* stmt;
    int n = -1;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM Eventos;", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) n = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    return n;
}

// muestra toda la BD (se pide en la sustentacion)
void imprimirEstado() {
    sqlite3* db;
    sqlite3_open(BD, &db);
    const char* sql = "SELECT idEvento, fecha, asientosLibres, asientosOcupados FROM Eventos;";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);

    std::cout << "\n----- ESTADO DE LA BD -----" << std::endl;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::cout << "Evento: " << sqlite3_column_text(stmt, 0)
                  << " | Fecha: " << sqlite3_column_text(stmt, 1)
                  << " | Libres: " << sqlite3_column_int(stmt, 2)
                  << " | Ocupados: " << sqlite3_column_int(stmt, 3) << std::endl;
    }
    sqlite3_finalize(stmt);

    // tambien la tabla de reservas
    sqlite3_prepare_v2(db, "SELECT idReservas, idEvento, fecha, cliente, numAsientos FROM Reservas;",
                       -1, &stmt, nullptr);
    std::cout << "\n----- RESERVAS -----" << std::endl;
    int n = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::cout << "Reserva " << sqlite3_column_int(stmt, 0)
                  << " | Evento: " << sqlite3_column_text(stmt, 1)
                  << " | Fecha: " << sqlite3_column_text(stmt, 2)
                  << " | Cliente: " << sqlite3_column_text(stmt, 3)
                  << " | Asientos: " << sqlite3_column_int(stmt, 4) << std::endl;
        n++;
    }
    if (n == 0) std::cout << "(no hay reservas)" << std::endl;
    std::cout << "---------------------------\n" << std::endl;
    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

// muestra como quedo un evento despues de reservar
void imprimirFila(sqlite3* db, const std::string& idEvento, const std::string& fecha) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT asientosLibres, asientosOcupados FROM Eventos WHERE idEvento=? AND fecha=?;",
                       -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, idEvento.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, fecha.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        std::cout << "  BD -> Evento: " << idEvento << " | Fecha: " << fecha
                  << " | Libres: " << sqlite3_column_int(stmt, 0)
                  << " | Ocupados: " << sqlite3_column_int(stmt, 1) << std::endl;
    }
    sqlite3_finalize(stmt);
}

// las consultas usan ? y sqlite3_bind para no meter el texto directo en el SQL (inyeccion SQL)

// eventos de un mes, ej. mes = "2026-11"
std::string consultarEventos(const std::string& mes) {
    sqlite3* db;
    sqlite3_open(BD, &db);

    const char* sql = "SELECT idEvento, fecha, asientosLibres, asientosOcupados FROM Eventos WHERE fecha LIKE ?;";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    std::string patron = mes + "%";
    sqlite3_bind_text(stmt, 1, patron.c_str(), -1, SQLITE_TRANSIENT);

    std::ostringstream oss;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string idEvento = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        std::string fecha = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        int libres = sqlite3_column_int(stmt, 2);
        int ocupados = sqlite3_column_int(stmt, 3);

        oss << "Evento: " << idEvento << " | Fecha: " << fecha
            << " | Libres: " << libres << " | Ocupados: " << ocupados << "\n";
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return oss.str();
}

// asientos libres de un evento en una fecha, -1 si no existe
std::string disponiblesEn(sqlite3* db, const std::string& idEvento, const std::string& fecha) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT asientosLibres FROM Eventos WHERE idEvento=? AND fecha=?;", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, idEvento.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, fecha.c_str(), -1, SQLITE_TRANSIENT);
    int libres = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        libres = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return std::to_string(libres);
}

std::string disponibles(const std::string& idEvento, const std::string& fecha) {
    sqlite3* db;
    sqlite3_open(BD, &db);
    std::string libres = disponiblesEn(db, idEvento, fecha);
    sqlite3_close(db);
    return libres;
}

// ---------------------------------------------------------------------------
// Escrituras sin duplicar.
// Cada escritura devuelve si se aplico y que se responde. sinDuplicar() la corre
// dentro de una transaccion y guarda la respuesta junto a su idSol:
//   - si el idSol ya estaba, no escribe nada y devuelve la respuesta guardada
//   - si se aplico, COMMIT de los cambios y de la respuesta, juntos
//   - si se rechazo, ROLLBACK (no queda nada a medias) y solo se guarda la respuesta
// ---------------------------------------------------------------------------
struct Resultado {
    bool aplicado;
    std::string respuesta;
};

std::optional<std::string> respuestaAnterior(sqlite3* db, const std::string& idSol) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT respuesta FROM Solicitudes WHERE idSol=?;", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, idSol.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<std::string> respuesta;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        respuesta = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    sqlite3_finalize(stmt);
    return respuesta;
}

void guardarSolicitud(sqlite3* db, const std::string& idSol, const std::string& respuesta) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "INSERT INTO Solicitudes (idSol, respuesta) VALUES (?, ?);", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, idSol.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, respuesta.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::string sinDuplicar(const std::string& idSol, const std::function<Resultado(sqlite3*)>& escribir) {
    sqlite3* db;
    sqlite3_open(BD, &db);
    sqlite3_exec(db, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr);

    auto anterior = respuestaAnterior(db, idSol);
    if (anterior) {
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        sqlite3_close(db);
        std::cout << "[Persistencia] Solicitud " << idSol
                  << " repetida: no se aplica otra vez, se devuelve la respuesta anterior" << std::endl;
        return *anterior;
    }

    Resultado r = escribir(db);
    if (!r.aplicado) {   // rechazada: se deshace lo que alcanzo a cambiar
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "BEGIN;", nullptr, nullptr, nullptr);
    }
    guardarSolicitud(db, idSol, r.respuesta);
    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
    sqlite3_close(db);
    return r.respuesta;
}

// suma o resta asientos libres de una ocurrencia; con 'soloSiAlcanza' no deja quedar negativos.
// Devuelve cuantas filas cambio (0 = no existe o no alcanzaban los asientos)
int moverAsientos(sqlite3* db, const std::string& idEvento, const std::string& fecha,
                  int libresQueSalen, bool soloSiAlcanza) {
    sqlite3_stmt* stmt;
    std::string sql = "UPDATE Eventos SET asientosLibres = asientosLibres - ?, "
                      "asientosOcupados = asientosOcupados + ? WHERE idEvento=? AND fecha=?";
    if (soloSiAlcanza) sql += " AND asientosLibres >= ?";
    sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, libresQueSalen);
    sqlite3_bind_int(stmt, 2, libresQueSalen);
    sqlite3_bind_text(stmt, 3, idEvento.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, fecha.c_str(), -1, SQLITE_TRANSIENT);
    if (soloSiAlcanza) sqlite3_bind_int(stmt, 5, libresQueSalen);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return sqlite3_changes(db);
}

long long insertarReserva(sqlite3* db, const std::string& idEvento, const std::string& fecha,
                          const std::string& cliente, int numAsientos) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "INSERT INTO Reservas (idEvento, fecha, cliente, numAsientos) VALUES (?, ?, ?, ?);",
                       -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, idEvento.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, fecha.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, cliente.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, numAsientos);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return sqlite3_last_insert_rowid(db);
}

// asientos que tiene un cliente reservados en un evento y fecha (0 si no tiene)
int asientosDelCliente(sqlite3* db, const std::string& idEvento, const std::string& fecha,
                       const std::string& cliente) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT IFNULL(SUM(numAsientos),0) FROM Reservas WHERE idEvento=? AND fecha=? AND cliente=?;",
                       -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, idEvento.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, fecha.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, cliente.c_str(), -1, SQLITE_TRANSIENT);
    int total = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) total = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return total;
}

void borrarReservasDelCliente(sqlite3* db, const std::string& idEvento, const std::string& fecha,
                              const std::string& cliente) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "DELETE FROM Reservas WHERE idEvento=? AND fecha=? AND cliente=?;", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, idEvento.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, fecha.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, cliente.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

// guarda la reserva. El RMC ya reviso que hay cupo, pero se vuelve a revisar en el UPDATE
// (asientosLibres >= ?) por si otro cliente reservo justo antes, asi no se venden mas de 100.
Resultado reservarAsientos(sqlite3* db, const std::string& idEvento, const std::string& fecha,
                           const std::string& cliente, int numAsientos) {
    if (moverAsientos(db, idEvento, fecha, numAsientos, true) == 0)   // no habia cupo
        return {false, "SIN_CUPO"};
    long long idReserva = insertarReserva(db, idEvento, fecha, cliente, numAsientos);
    imprimirFila(db, idEvento, fecha);
    return {true, "OK " + std::to_string(idReserva)};
}

// cambia la reserva de un cliente en un evento y fecha: a otra fecha del mismo evento
// y/o a otro numero de asientos. Primero devuelve los asientos que tenia y luego toma
// los nuevos; si no alcanzan, sinDuplicar() deshace todo y la reserva queda como estaba.
// Si el cliente tenia varias reservas para esa fecha, quedan unidas en una sola.
Resultado modificarReserva(sqlite3* db, const std::string& idEvento, const std::string& fecha,
                           const std::string& cliente, const std::string& nuevaFecha, int numAsientos) {
    int antes = asientosDelCliente(db, idEvento, fecha, cliente);
    if (antes == 0) return {false, "NO_EXISTE"};
    if (numAsientos <= 0) return {false, "ASIENTOS_INVALIDOS"};

    moverAsientos(db, idEvento, fecha, -antes, false);                 // devolver los que tenia
    if (moverAsientos(db, idEvento, nuevaFecha, numAsientos, true) == 0) {
        int libres = std::stoi(disponiblesEn(db, idEvento, nuevaFecha));
        if (libres < 0) return {false, "NO_OCURRENCIA"};
        return {false, "SIN_CUPO " + std::to_string(libres)};
    }
    borrarReservasDelCliente(db, idEvento, fecha, cliente);
    long long idReserva = insertarReserva(db, idEvento, nuevaFecha, cliente, numAsientos);
    imprimirFila(db, idEvento, nuevaFecha);
    return {true, "OK " + std::to_string(idReserva) + " " + std::to_string(antes)};
}

// borra las reservas de un cliente en un evento y fecha y devuelve los asientos
Resultado eliminarReserva(sqlite3* db, const std::string& idEvento, const std::string& fecha,
                          const std::string& cliente) {
    int totalSillas = asientosDelCliente(db, idEvento, fecha, cliente);
    if (totalSillas == 0) return {false, "No se encontro reserva para eliminar"};

    borrarReservasDelCliente(db, idEvento, fecha, cliente);
    moverAsientos(db, idEvento, fecha, -totalSillas, false);
    return {true, "Reserva cancelada. Se liberaron " + std::to_string(totalSillas) + " asientos"};
}

int main(int argc, char* argv[]) {
    auto cfg = leerConfig();

    // si le pasan un .sql lo carga, si la BD esta vacia carga init_db.sql
    if (argc > 1) cargarBD(argv[1]);
    else if (contarEventos() <= 0) cargarBD("init_db.sql");
    prepararSolicitudes();
    imprimirEstado();

    zmq::context_t context(1);
    zmq::socket_t socket(context, ZMQ_REP);
    socket.set(zmq::sockopt::rcvtimeo, PULSO_MS);   // despertar cada segundo para el watchdog
    socket.bind("tcp://*:" + cfg["PERSISTENCIA_PUERTO"]);

    std::cout << "Persistencia escuchando en puerto " << cfg["PERSISTENCIA_PUERTO"] << "..." << std::endl;

    avisarListo();   // a systemd: la BD esta cargada y ya hice el bind

    while (true) {
        avisarVivo();   // watchdog: cada vuelta, al menos una por segundo, systemd sabe que sigue bien
        zmq::message_t request;
        if (!socket.recv(request, zmq::recv_flags::none)) continue;   // no llego nada en 1 s
        std::string msg(static_cast<char*>(request.data()), request.size());
        std::cout << "[Persistencia] Mensaje recibido: " << msg << std::endl;

        std::istringstream iss(msg);
        std::string cmd;
        iss >> cmd;
        std::string respuesta;

        if (cmd == "CONSULTA") {
            std::string mes;
            iss >> mes;
            respuesta = consultarEventos(mes);
        } else if (cmd == "DISPONIBLES") {
            std::string idEvento, fecha;
            iss >> idEvento >> fecha;
            respuesta = disponibles(idEvento, fecha);
        } else if (cmd == "RESERVAR") {
            std::string idSol, idEvento, fecha, cliente;
            int numAsientos = 0;
            iss >> idSol >> idEvento >> fecha >> cliente >> numAsientos;
            respuesta = sinDuplicar(idSol, [&](sqlite3* db) {
                return reservarAsientos(db, idEvento, fecha, cliente, numAsientos); });
            imprimirEstado();   // se muestra la BD actualizada despues de cada reserva
        } else if (cmd == "MODIFICAR") {
            std::string idSol, idEvento, fecha, cliente, nuevaFecha;
            int numAsientos = 0;
            iss >> idSol >> idEvento >> fecha >> cliente >> nuevaFecha >> numAsientos;
            respuesta = sinDuplicar(idSol, [&](sqlite3* db) {
                return modificarReserva(db, idEvento, fecha, cliente, nuevaFecha, numAsientos); });
            imprimirEstado();   // y despues de cada modificacion
        } else if (cmd == "ELIMINAR") {
            std::string idSol, idEvento, fecha, cliente;
            iss >> idSol >> idEvento >> fecha >> cliente;
            respuesta = sinDuplicar(idSol, [&](sqlite3* db) {
                return eliminarReserva(db, idEvento, fecha, cliente); });
            imprimirEstado();   // y despues de cada cancelacion
        } else if (cmd == "ESTADO") {
            imprimirEstado();
            respuesta = "OK";
        } else {
            respuesta = "Comando no reconocido";
        }

        if (cmd != "CONSULTA") {
            std::cout << "[Persistencia] Respuesta: " << respuesta << std::endl;
        }
        socket.send(zmq::buffer(respuesta), zmq::send_flags::none);
    }
}
