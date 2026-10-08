#include "cancelaciones.h"
#include <sqlite3.h>
#include <iostream>

std::string eliminarReserva(const std::string& idEvento, const std::string& cliente) {
    sqlite3* db;
    sqlite3_open("reserva.db", &db);

    int totalSillas = 0;

    // 1. Sumar todas las reservas del cliente en ese evento
    std::string querySum = "SELECT SUM(numAsientos) FROM Reservas WHERE idEvento=? AND cliente=?;";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, querySum.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, idEvento.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, cliente.c_str(), -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        totalSillas = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    if (totalSillas > 0) {
        // 2. Eliminar todas las reservas de ese cliente en ese evento
        std::string queryDelete = "DELETE FROM Reservas WHERE idEvento=? AND cliente=?;";
        sqlite3_prepare_v2(db, queryDelete.c_str(), -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, idEvento.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, cliente.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        // 3. Actualizar el evento liberando esas sillas
        std::string queryUpdate = "UPDATE Eventos SET asientosLibres = asientosLibres + ?, asientosOcupados = asientosOcupados - ? WHERE idEvento=?;";
        sqlite3_prepare_v2(db, queryUpdate.c_str(), -1, &stmt, nullptr);
        sqlite3_bind_int(stmt, 1, totalSillas);
        sqlite3_bind_int(stmt, 2, totalSillas);
        sqlite3_bind_text(stmt, 3, idEvento.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        sqlite3_close(db);
        return "Reserva cancelada correctamente. Se liberaron " + std::to_string(totalSillas) + " asientos.";
    }

    sqlite3_close(db);
    return "No existe reserva para cliente=" + cliente + " en evento=" + idEvento;
}
