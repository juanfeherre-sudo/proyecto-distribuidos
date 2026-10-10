// persistencia.cpp  —  corre en la VM3 (capa de almacenamiento)
//
// Es el ÚNICO proceso que toca la base de datos. No decide nada de negocio:
// solo ejecuta órdenes simples que le manda el RMC y responde lo que diga la BD.
//
// Órdenes que entiende (texto separado por espacios):
//   EVENTOS <mes>                                 -> lista las ocurrencias de ese mes (ej. 2026-11)
//   DISPONIBLES <idEvento> <fecha>                -> número de asientos libres, o -1 si no existe
//   REGISTRAR <idEvento> <fecha> <cliente> <n>    -> OK <idReserva>  |  SIN_CUPO  |  NO_EXISTE
//   ESTADO                                        -> imprime toda la BD en la consola
//
// Uso:  ./persistencia                 (abre la BD existente)
//       ./persistencia eventos.txt     (borra la BD y la carga desde el archivo: caso de prueba)

#include "common.hpp"
#include <sqlite3.h>

static sqlite3* db = nullptr;   // conexión a la BD, abierta una sola vez al arrancar

// ---------------------------------------------------------------------------
// Utilidades de SQLite
// ---------------------------------------------------------------------------

// Ejecuta SQL sin parámetros (crear tablas, BEGIN, COMMIT...).
void ejecutar(const std::string& sql) {
    char* error = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
        log("Persistencia", std::string("Error SQL: ") + error);
        sqlite3_free(error);
    }
}

// Crea las tablas si no existen.
// Un evento puede tener 1 o 2 ocurrencias en el mes, por eso la llave es (idEvento, fecha):
// el mismo E5 puede aparecer dos veces con fechas distintas, cada una con sus 100 asientos.
void crearTablas() {
    ejecutar("CREATE TABLE IF NOT EXISTS Eventos ("
             " idEvento TEXT NOT NULL,"
             " fecha TEXT NOT NULL,"
             " asientosLibres INTEGER NOT NULL,"
             " asientosOcupados INTEGER NOT NULL DEFAULT 0,"
             " PRIMARY KEY (idEvento, fecha),"
             " CHECK (asientosLibres >= 0));");
    ejecutar("CREATE TABLE IF NOT EXISTS Reservas ("
             " idReserva INTEGER PRIMARY KEY AUTOINCREMENT,"
             " idEvento TEXT NOT NULL,"
             " fecha TEXT NOT NULL,"
             " cliente TEXT NOT NULL,"
             " numAsientos INTEGER NOT NULL);");
}

// Borra todo y carga los eventos de un archivo con líneas: "<idEvento> <fecha> <asientos>".
// Sirve para cargar rápido el caso de prueba que trae el profesor a la sustentación.
void cargarEventos(const std::string& ruta) {
    std::ifstream archivo(ruta);
    if (!archivo) {
        log("Persistencia", "No se pudo abrir " + ruta);
        std::exit(1);
    }
    ejecutar("DELETE FROM Reservas; DELETE FROM Eventos; DELETE FROM sqlite_sequence;");

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "INSERT INTO Eventos (idEvento, fecha, asientosLibres, asientosOcupados) VALUES (?, ?, ?, 0);",
                       -1, &stmt, nullptr);
    ejecutar("BEGIN;");
    std::string linea;
    int cargados = 0;
    while (std::getline(archivo, linea)) {
        auto p = partir(linea);
        if (p.size() < 3 || p[0][0] == '#') continue;
        // Los ? se llenan con bind: así un texto raro nunca se interpreta como SQL (evita inyección SQL).
        sqlite3_bind_text(stmt, 1, p[0].c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, p[1].c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, std::stoi(p[2]));
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
        cargados++;
    }
    ejecutar("COMMIT;");
    sqlite3_finalize(stmt);
    log("Persistencia", "Cargadas " + std::to_string(cargados) + " ocurrencias desde " + ruta);
}

// Imprime el estado completo de la BD (lo exige la sustentación).
void imprimirEstado() {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT idEvento, fecha, asientosLibres, asientosOcupados FROM Eventos "
                           "ORDER BY CAST(SUBSTR(idEvento, 2) AS INTEGER), fecha;", -1, &stmt, nullptr);
    std::cout << "\n=========== ESTADO DE LA BD ===========\n";
    std::cout << std::left << std::setw(8) << "Evento" << std::setw(13) << "Fecha"
              << std::setw(8) << "Libres" << "Ocupados\n";
    int filas = 0, totalOcupados = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::cout << std::left << std::setw(8) << sqlite3_column_text(stmt, 0)
                  << std::setw(13) << sqlite3_column_text(stmt, 1)
                  << std::setw(8) << sqlite3_column_int(stmt, 2)
                  << sqlite3_column_int(stmt, 3) << "\n";
        totalOcupados += sqlite3_column_int(stmt, 3);
        filas++;
    }
    sqlite3_finalize(stmt);
    std::cout << filas << " ocurrencias, " << totalOcupados << " asientos ocupados en total\n";
    std::cout << "=======================================\n" << std::endl;
}

// Imprime cómo quedó UNA ocurrencia después de un cambio.
void imprimirFila(const std::string& idEvento, const std::string& fecha) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT asientosLibres, asientosOcupados FROM Eventos WHERE idEvento=? AND fecha=?;",
                       -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, idEvento.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, fecha.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        log("Persistencia", "  BD -> " + idEvento + " " + fecha + ": libres=" +
            std::to_string(sqlite3_column_int(stmt, 0)) + " ocupados=" +
            std::to_string(sqlite3_column_int(stmt, 1)));
    }
    sqlite3_finalize(stmt);
}

// ---------------------------------------------------------------------------
// Operaciones (una por orden que puede mandar el RMC)
// ---------------------------------------------------------------------------

// EVENTOS <mes>: todas las ocurrencias cuya fecha empieza por el mes ("2026-11").
// Responde una línea por ocurrencia: "E5 2026-11-10 libres=80 ocupados=20"
std::string eventosDelMes(const std::string& mes) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT idEvento, fecha, asientosLibres, asientosOcupados FROM Eventos "
                           "WHERE fecha LIKE ? ORDER BY fecha, idEvento;", -1, &stmt, nullptr);
    std::string patron = mes + "%";                         // LIKE '2026-11%'
    sqlite3_bind_text(stmt, 1, patron.c_str(), -1, SQLITE_TRANSIENT);
    std::ostringstream oss;
    int n = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        oss << sqlite3_column_text(stmt, 0) << " " << sqlite3_column_text(stmt, 1)
            << " libres=" << sqlite3_column_int(stmt, 2)
            << " ocupados=" << sqlite3_column_int(stmt, 3) << "\n";
        n++;
    }
    sqlite3_finalize(stmt);
    log("Persistencia", "  " + std::to_string(n) + " ocurrencias en " + mes);
    return oss.str();
}

// DISPONIBLES <idEvento> <fecha>: asientos libres de esa ocurrencia, -1 si no existe.
std::string disponibles(const std::string& idEvento, const std::string& fecha) {
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT asientosLibres FROM Eventos WHERE idEvento=? AND fecha=?;", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, idEvento.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, fecha.c_str(), -1, SQLITE_TRANSIENT);
    int libres = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) libres = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return std::to_string(libres);
}

// REGISTRAR <idEvento> <fecha> <cliente> <n>: descuenta los asientos y guarda la reserva.
// Todo va dentro de una transacción (BEGIN ... COMMIT): o se hacen las dos cosas, o ninguna.
// El UPDATE vuelve a verificar "asientosLibres >= n": si entre la pregunta del RMC
// y esta orden otro cliente se llevó los asientos, no se vende de más (integridad de datos).
std::string registrar(const std::string& idEvento, const std::string& fecha,
                      const std::string& cliente, int n) {
    if (disponibles(idEvento, fecha) == "-1") return "NO_EXISTE";

    ejecutar("BEGIN;");
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "UPDATE Eventos SET asientosLibres = asientosLibres - ?, "
                           "asientosOcupados = asientosOcupados + ? "
                           "WHERE idEvento=? AND fecha=? AND asientosLibres >= ?;", -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, n);
    sqlite3_bind_int(stmt, 2, n);
    sqlite3_bind_text(stmt, 3, idEvento.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, fecha.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, n);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (sqlite3_changes(db) == 0) {          // ninguna fila cambió: no había cupo
        ejecutar("ROLLBACK;");
        return "SIN_CUPO";
    }

    sqlite3_prepare_v2(db, "INSERT INTO Reservas (idEvento, fecha, cliente, numAsientos) VALUES (?, ?, ?, ?);",
                       -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, idEvento.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, fecha.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, cliente.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, n);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    long long idReserva = sqlite3_last_insert_rowid(db);
    ejecutar("COMMIT;");

    imprimirFila(idEvento, fecha);           // mostrar cómo quedó la BD tras el cambio
    return "OK " + std::to_string(idReserva);
}

// ---------------------------------------------------------------------------
// Programa principal: abrir BD, cargar datos y atender órdenes para siempre.
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    auto cfg = leerConfig();

    sqlite3_open(cfg["BD_ARCHIVO"].c_str(), &db);
    crearTablas();
    if (argc > 1) cargarEventos(argv[1]);    // ./persistencia eventos.txt -> recarga desde cero
    imprimirEstado();                        // estado INICIAL de la BD

    zmq::context_t contexto(1);
    zmq::socket_t socket(contexto, zmq::socket_type::rep);   // REP: recibe una orden, responde, repite
    socket.bind("tcp://*:" + cfg["PERSISTENCIA_PUERTO"]);   // escucha en todas las interfaces de la VM3
    log("Persistencia", "Escuchando en el puerto " + cfg["PERSISTENCIA_PUERTO"]);

    while (true) {
        std::string orden = recibir(socket);
        log("Persistencia", "<- " + orden);
        auto p = partir(orden);
        std::string respuesta;

        try {
            if (p.size() == 2 && p[0] == "EVENTOS") {
                respuesta = eventosDelMes(p[1]);
            } else if (p.size() == 3 && p[0] == "DISPONIBLES") {
                respuesta = disponibles(p[1], p[2]);
            } else if (p.size() == 5 && p[0] == "REGISTRAR") {
                respuesta = registrar(p[1], p[2], p[3], std::stoi(p[4]));
            } else if (p.size() == 1 && p[0] == "ESTADO") {
                imprimirEstado();
                respuesta = "OK";
            } else {
                respuesta = "ERROR orden no reconocida";
            }
        } catch (const std::exception&) {      // ej. std::stoi con "abc": responder en vez de caerse
            respuesta = "ERROR formato invalido";
        }

        // Una respuesta de varias líneas (EVENTOS) se resume en el log para no llenar la pantalla.
        log("Persistencia", "-> " + (respuesta.find('\n') == std::string::npos ? respuesta : "(lista de eventos)"));
        enviar(socket, respuesta);             // REP obliga a responder antes del siguiente recv
    }
}
