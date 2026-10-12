// Persistencia (VM3): unico proceso que maneja la base de datos reserva.db
// ./persistencia init_db.sql   -> carga la base de datos desde cero

#include <zmq.hpp>
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <sqlite3.h>

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
    sqlite3_close(db);
    std::cout << "BD cargada desde " << archivoSql << std::endl;
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
std::string disponibles(const std::string& idEvento, const std::string& fecha) {
    sqlite3* db;
    sqlite3_open(BD, &db);
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT asientosLibres FROM Eventos WHERE idEvento=? AND fecha=?;", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, idEvento.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, fecha.c_str(), -1, SQLITE_TRANSIENT);
    int libres = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        libres = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return std::to_string(libres);
}

// guarda la reserva. El RMC ya reviso que hay cupo, pero se vuelve a revisar en el UPDATE
// (asientosLibres >= ?) por si otro cliente reservo justo antes, asi no se venden mas de 100.
// Va en una transaccion: o se hacen el UPDATE y el INSERT, o ninguno.
std::string reservarAsientos(const std::string& idEvento, const std::string& fecha,
                             const std::string& cliente, int numAsientos) {
    sqlite3* db;
    sqlite3_open(BD, &db);
    sqlite3_exec(db, "BEGIN;", nullptr, nullptr, nullptr);

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "UPDATE Eventos SET asientosLibres = asientosLibres - ?, "
                           "asientosOcupados = asientosOcupados + ? "
                           "WHERE idEvento=? AND fecha=? AND asientosLibres >= ?;", -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, numAsientos);
    sqlite3_bind_int(stmt, 2, numAsientos);
    sqlite3_bind_text(stmt, 3, idEvento.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, fecha.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, numAsientos);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (sqlite3_changes(db) == 0) {   // no cambio ninguna fila: no habia cupo
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        sqlite3_close(db);
        return "SIN_CUPO";
    }

    sqlite3_prepare_v2(db, "INSERT INTO Reservas (idEvento, fecha, cliente, numAsientos) VALUES (?, ?, ?, ?);",
                       -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, idEvento.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, fecha.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, cliente.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, numAsientos);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    long long idReserva = sqlite3_last_insert_rowid(db);
    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);

    imprimirFila(db, idEvento, fecha);
    sqlite3_close(db);
    return "OK " + std::to_string(idReserva);
}

// borra las reservas de un cliente en un evento y fecha, y devuelve los asientos (2a entrega)
std::string eliminarReserva(const std::string& idEvento, const std::string& fecha, const std::string& cliente) {
    sqlite3* db;
    sqlite3_open(BD, &db);

    int totalSillas = 0;
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT SUM(numAsientos) FROM Reservas WHERE idEvento=? AND fecha=? AND cliente=?;",
                       -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, idEvento.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, fecha.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, cliente.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        totalSillas = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    if (totalSillas == 0) {
        sqlite3_close(db);
        return "No se encontro reserva para eliminar";
    }

    sqlite3_prepare_v2(db, "DELETE FROM Reservas WHERE idEvento=? AND fecha=? AND cliente=?;", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, idEvento.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, fecha.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, cliente.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    sqlite3_prepare_v2(db, "UPDATE Eventos SET asientosLibres = asientosLibres + ?, "
                           "asientosOcupados = asientosOcupados - ? WHERE idEvento=? AND fecha=?;", -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, totalSillas);
    sqlite3_bind_int(stmt, 2, totalSillas);
    sqlite3_bind_text(stmt, 3, idEvento.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, fecha.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    sqlite3_close(db);
    return "Reserva cancelada. Se liberaron " + std::to_string(totalSillas) + " asientos";
}

int main(int argc, char* argv[]) {

    // si le pasan un .sql lo carga, si la BD esta vacia carga init_db.sql
    if (argc > 1) cargarBD(argv[1]);
    else if (contarEventos() <= 0) cargarBD("init_db.sql");
    imprimirEstado();

    zmq::context_t context(1);
    zmq::socket_t socket(context, ZMQ_REP);
    socket.bind("tcp://*:6666");

    std::cout << "Persistencia escuchando en puerto 6666..." << std::endl;

    while (true) {
        zmq::message_t request;
        auto result = socket.recv(request, zmq::recv_flags::none);
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
            std::string idEvento, fecha, cliente;
            int numAsientos = 0;
            iss >> idEvento >> fecha >> cliente >> numAsientos;
            respuesta = reservarAsientos(idEvento, fecha, cliente, numAsientos);
            imprimirEstado();   // se muestra la BD actualizada despues de cada reserva
        } else if (cmd == "ELIMINAR") {
            std::string idEvento, fecha, cliente;
            iss >> idEvento >> fecha >> cliente;
            respuesta = eliminarReserva(idEvento, fecha, cliente);
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
