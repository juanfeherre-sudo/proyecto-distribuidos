#include <zmq.hpp>
#include <iostream>
#include <sstream>
#include <string>
#include <sqlite3.h>

// --- Funciones de persistencia ---

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

std::string eliminarReserva(const std::string& idEvento, const std::string& cliente) {
    sqlite3* db;
    sqlite3_open("reserva.db", &db);

    // Verificar cuántos asientos tenía reservados el cliente
    std::string sqlCheck = "SELECT numAsientos FROM Reservas WHERE idEvento='" + idEvento + "' AND cliente='" + cliente + "';";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, sqlCheck.c_str(), -1, &stmt, nullptr);
    int numAsientos = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        numAsientos = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    if (numAsientos > 0) {
        // Eliminar la reserva
        std::string sqlDelete = "DELETE FROM Reservas WHERE idEvento='" + idEvento + "' AND cliente='" + cliente + "';";
        sqlite3_exec(db, sqlDelete.c_str(), nullptr, nullptr, nullptr);

        // Actualizar los asientos en Eventos
        std::string sqlUpdate = "UPDATE Eventos SET asientosLibres=asientosLibres+" + std::to_string(numAsientos) +
            ", asientosOcupados=asientosOcupados-" + std::to_string(numAsientos) +
            " WHERE idEvento='" + idEvento + "';";
        sqlite3_exec(db, sqlUpdate.c_str(), nullptr, nullptr, nullptr);

        sqlite3_close(db);
        return "Reserva eliminada correctamente";
    } else {
        sqlite3_close(db);
        return "No se encontró reserva para eliminar";
    }
}

// --- Servidor de persistencia ---
int main() {
    zmq::context_t context(1);
    zmq::socket_t socket(context, ZMQ_REP);
    socket.bind("tcp://*:6666");

    std::cout << "Persistencia escuchando en puerto 6666..." << std::endl;

    while (true) {
        zmq::message_t request;
        auto result = socket.recv(request, zmq::recv_flags::none);
        std::string msg(static_cast<char*>(request.data()), request.size());
        std::cout << "[Persistencia] Mensaje recibido: " << msg << std::endl;

        std::string respuesta;

        if (msg == "CONSULTA") {
            respuesta = consultarEventos();
        } else if (msg.rfind("RESERVA", 0) == 0) {
            std::istringstream iss(msg);
            std::string cmd, idEvento, cliente;
            int numAsientos;
            iss >> cmd >> idEvento >> cliente >> numAsientos;
            respuesta = reservarAsientos(idEvento, cliente, numAsientos);
        } else if (msg.rfind("ELIMINAR", 0) == 0) {
            std::istringstream iss(msg);
            std::string cmd, idEvento, cliente;
            iss >> cmd >> idEvento;
            std::getline(iss, cliente);
            if (!cliente.empty() && cliente[0] == ' ') cliente.erase(0,1);
            respuesta = eliminarReserva(idEvento, cliente);
        } else {
            respuesta = "Comando no reconocido";
        }

        zmq::message_t reply(respuesta.size());
        memcpy(reply.data(), respuesta.data(), respuesta.size());
        auto resultpersistencia = socket.send(reply, zmq::send_flags::none);
    }
}
