#include "persistencia.h"
#include <iostream>
#include <sqlite3.h>
#include <sstream>

std::string consultarEventos() {
    sqlite3* db;
    sqlite3_open("reserva.db", &db);

    const char* sql = "SELECT idEvento, fecha, asientosLibres, asientosOcupados FROM Eventos;";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);

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

std::string reservarAsientos(const std::string& idEvento, const std::string& cliente, int numAsientos) {
    sqlite3* db;
    sqlite3_open("reserva.db", &db);

    std::string sqlCheck = "SELECT asientosLibres FROM Eventos WHERE idEvento='" + idEvento + "';";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, sqlCheck.c_str(), -1, &stmt, nullptr);
    int libres = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        libres = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    if (libres >= numAsientos) {
        std::string sqlUpdate = "UPDATE Eventos SET asientosLibres=asientosLibres-" + std::to_string(numAsientos) +
                                ", asientosOcupados=asientosOcupados+" + std::to_string(numAsientos) +
                                " WHERE idEvento='" + idEvento + "';";
        sqlite3_exec(db, sqlUpdate.c_str(), nullptr, nullptr, nullptr);

        std::string sqlInsert = "INSERT INTO Reservas (idEvento, cliente, numAsientos) VALUES ('" +
                                idEvento + "', '" + cliente + "', " + std::to_string(numAsientos) + ");";
        sqlite3_exec(db, sqlInsert.c_str(), nullptr, nullptr, nullptr);

        sqlite3_close(db);
        return "Reserva exitosa";
    } else {
        sqlite3_close(db);
        return "No hay suficientes asientos";
    }
}

