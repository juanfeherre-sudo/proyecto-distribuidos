// persistencia.cpp  -  corre en la VM3 (capa de almacenamiento)
//
// Unico proceso que toca la base de datos (reserva.db). No toma decisiones de negocio:
// ejecuta las ordenes que le mandan el RMC y el servicio de cancelacion.
//
// Ordenes que recibe:
//   CONSULTA <mes>                                -> eventos de ese mes (ej. 2026-11)
//   DISPONIBLES <idEvento> <fecha>                -> asientos libres, o -1 si no existe
//   RESERVAR <idEvento> <fecha> <cliente> <n>     -> OK <idReserva> | SIN_CUPO
//   ELIMINAR <idEvento> <cliente>                 -> libera las reservas del cliente (2a entrega)
//   ESTADO                                        -> imprime toda la BD
//
// Uso: ./persistencia               usa reserva.db (si esta vacia, carga init_db.sql)
//      ./persistencia caso.sql      borra todo y carga ese archivo (caso de prueba del profesor)

#include <zmq.hpp>
#include <iostream>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <string>
#include <sqlite3.h>
#include "config.h"

const char* BD = "reserva.db";

// --- Utilidades de la BD ---

// Ejecuta un archivo .sql completo (crear tablas + insertar eventos).
void cargarBD(const std::string& archivoSql) {
    std::ifstream archivo(archivoSql);
    if (!archivo) {
        std::cerr << "[Persistencia] No se encontro " << archivoSql << std::endl;
        exit(1);
    }
    std::stringstream contenido;
    contenido << archivo.rdbuf();

    sqlite3* db;
    sqlite3_open(BD, &db);
    char* error = nullptr;
    if (sqlite3_exec(db, contenido.str().c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
        std::cerr << "[Persistencia] Error cargando " << archivoSql << ": " << error << std::endl;
        sqlite3_free(error);
    }
    sqlite3_close(db);
    std::cout << "[Persistencia] BD cargada desde " << archivoSql << std::endl;
}

// Cuantas ocurrencias hay en la BD (-1 si la tabla no existe todavia).
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

// Imprime la BD completa: se pide en la sustentacion (estado inicial y como va quedando).
void imprimirEstado() {
    sqlite3* db;
    sqlite3_open(BD, &db);
    const char* sql = "SELECT idEvento, fecha, asientosLibres, asientosOcupados FROM Eventos "
                      "ORDER BY CAST(SUBSTR(idEvento, 2) AS INTEGER), fecha;";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);

    std::cout << "\n=========== ESTADO DE LA BD ===========" << std::endl;
    std::cout << std::left << std::setw(8) << "Evento" << std::setw(13) << "Fecha"
              << std::setw(8) << "Libres" << "Ocupados" << std::endl;
    int filas = 0, ocupadosTotal = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::cout << std::left << std::setw(8) << sqlite3_column_text(stmt, 0)
                  << std::setw(13) << sqlite3_column_text(stmt, 1)
                  << std::setw(8) << sqlite3_column_int(stmt, 2)
                  << sqlite3_column_int(stmt, 3) << std::endl;
        ocupadosTotal += sqlite3_column_int(stmt, 3);
        filas++;
    }
    std::cout << filas << " ocurrencias, " << ocupadosTotal << " asientos ocupados en total" << std::endl;
    std::cout << "=======================================\n" << std::endl;
    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

// Muestra como quedo una ocurrencia despues de un cambio.
void imprimirFila(sqlite3* db, const std::string& idEvento, const std::string& fecha) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT asientosLibres, asientosOcupados FROM Eventos WHERE idEvento=? AND fecha=?;",
                       -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, idEvento.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, fecha.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        std::cout << "[Persistencia]   BD -> " << idEvento << " " << fecha
                  << ": libres=" << sqlite3_column_int(stmt, 0)
                  << " ocupados=" << sqlite3_column_int(stmt, 1) << std::endl;
    }
    sqlite3_finalize(stmt);
}

// --- Funciones de persistencia ---
// Todas usan ? + sqlite3_bind_* en vez de concatenar texto en el SQL (evita inyeccion SQL).

// Eventos de un mes: todas las ocurrencias cuya fecha empieza por el mes ("2026-11").
std::string consultarEventos(const std::string& mes) {
    sqlite3* db;
    sqlite3_open(BD, &db);

    const char* sql = "SELECT idEvento, fecha, asientosLibres, asientosOcupados FROM Eventos "
                      "WHERE fecha LIKE ? ORDER BY fecha, idEvento;";
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

// Asientos libres de una ocurrencia (evento + fecha). -1 si esa ocurrencia no existe.
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

// Registra la reserva. La decision de si hay cupo la toma el RMC; aqui solo se garantiza
// que los datos queden consistentes:
//  - BEGIN/COMMIT: se descuentan asientos y se inserta la reserva, o no se hace nada.
//  - "AND asientosLibres >= ?": si otro cliente se llevo los asientos entre la pregunta
//    del RMC y esta orden, no se vende de mas.
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

    if (sqlite3_changes(db) == 0) {
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

// Libera todas las reservas de un cliente en un evento (se usa en la 2a entrega).
// Suma los asientos por fecha, para devolverlos a la ocurrencia correcta.
std::string eliminarReserva(const std::string& idEvento, const std::string& cliente) {
    sqlite3* db;
    sqlite3_open(BD, &db);
    sqlite3_exec(db, "BEGIN;", nullptr, nullptr, nullptr);

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT fecha, SUM(numAsientos) FROM Reservas WHERE idEvento=? AND cliente=? GROUP BY fecha;",
                       -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, idEvento.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, cliente.c_str(), -1, SQLITE_TRANSIENT);

    int total = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string fecha = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        int n = sqlite3_column_int(stmt, 1);
        sqlite3_stmt* upd;
        sqlite3_prepare_v2(db, "UPDATE Eventos SET asientosLibres = asientosLibres + ?, "
                               "asientosOcupados = asientosOcupados - ? WHERE idEvento=? AND fecha=?;",
                           -1, &upd, nullptr);
        sqlite3_bind_int(upd, 1, n);
        sqlite3_bind_int(upd, 2, n);
        sqlite3_bind_text(upd, 3, idEvento.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(upd, 4, fecha.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(upd);
        sqlite3_finalize(upd);
        total += n;
    }
    sqlite3_finalize(stmt);

    if (total == 0) {
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        sqlite3_close(db);
        return "No se encontro reserva para eliminar";
    }

    sqlite3_prepare_v2(db, "DELETE FROM Reservas WHERE idEvento=? AND cliente=?;", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, idEvento.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, cliente.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
    sqlite3_close(db);
    return "Reserva eliminada correctamente. Se liberaron " + std::to_string(total) + " asientos";
}

// --- Servidor de persistencia ---
int main(int argc, char* argv[]) {
    auto cfg = leerConfig();

    // Cargar los datos: con argumento se recarga desde ese .sql; si la BD esta vacia, desde init_db.sql
    if (argc > 1) cargarBD(argv[1]);
    else if (contarEventos() <= 0) cargarBD("init_db.sql");
    imprimirEstado();   // estado INICIAL de la BD

    zmq::context_t context(1);
    zmq::socket_t socket(context, ZMQ_REP);
    socket.bind("tcp://*:" + cfg["PERSISTENCIA_PUERTO"]);

    std::cout << "[Persistencia] Escuchando en puerto " << cfg["PERSISTENCIA_PUERTO"] << "..." << std::endl;

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
        } else if (cmd == "ELIMINAR") {
            std::string idEvento, cliente;
            iss >> idEvento >> cliente;
            respuesta = eliminarReserva(idEvento, cliente);
            imprimirEstado();
        } else if (cmd == "ESTADO") {
            imprimirEstado();
            respuesta = "OK";
        } else {
            respuesta = "Comando no reconocido";
        }

        std::cout << "[Persistencia] Respuesta: "
                  << (respuesta.find('\n') == std::string::npos ? respuesta : "(lista de eventos)") << std::endl;
        socket.send(zmq::buffer(respuesta), zmq::send_flags::none);
    }
}
