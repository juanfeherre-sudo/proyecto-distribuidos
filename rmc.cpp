// RMC (VM2): reglas de negocio de consulta, reserva y modificacion. Nunca toca la BD:
// todo se lo pide a la persistencia.
//
// uso: ./rmc 1   ./rmc 2   ./rmc 3   (el numero solo distingue los logs)
//
// Recibe del gestor (el idSol lo pone el cliente y sirve para no aplicar dos veces una escritura):
//   CONSULTA     <idSol> <mes>
//   RESERVA      <idSol> <evento> <fecha> <cliente> <asientos>
//   MODIFICACION <idSol> <evento> <fecha> <cliente> <nuevaFecha> <asientos>
// Responde EXITO... o RECHAZO: <motivo>

#include "common.hpp"
#include <iostream>
#include <sstream>
#include <string>

std::string ETIQUETA = "[RMC]";   // en main pasa a ser [RMC-1], [RMC-2]...

// le manda una orden a la persistencia y espera la respuesta.
// Si la persistencia no contesta a tiempo se reintenta (Lazy Pirate); si se agotan
// los intentos devuelve vacio (nullopt) y el RMC responde RECHAZO en vez de quedarse colgado.
std::optional<std::string> pedirAPersistencia(ReqConReintento& persistencia, const std::string& orden,
                                              int intentos = 0) {
    std::cout << ETIQUETA << "   -> Persistencia: " << orden << std::endl;
    auto respuesta = persistencia.pedir(orden, intentos);
    if (respuesta && orden.substr(0, 8) != "CONSULTA") {
        std::cout << ETIQUETA << "   <- Persistencia: " << *respuesta << std::endl;
    }
    return respuesta;
}

// consulta: se le pide a persistencia y se devuelve la lista al gestor
std::string consultar(ReqConReintento& persistencia, const std::string& mes) {
    auto lista = pedirAPersistencia(persistencia, "CONSULTA " + mes);
    if (!lista) return "RECHAZO: la persistencia no responde";
    if (lista->empty()) return "RECHAZO: no hay eventos en " + mes;
    return "EXITO\n" + *lista;
}

// reserva: primero pregunta cuantos asientos hay libres, si alcanzan manda a reservar
std::string reservar(ReqConReintento& persistencia, const std::string& idSol, const std::string& idEvento,
                     const std::string& fecha, const std::string& cliente, int numAsientos) {
    if (numAsientos <= 0) return "RECHAZO: numero de asientos invalido";

    // DISPONIBLES es una lectura: si se pierde la respuesta, preguntar otra vez no daña nada
    auto disp = pedirAPersistencia(persistencia, "DISPONIBLES " + idEvento + " " + fecha);
    if (!disp) return "RECHAZO: la persistencia no responde";
    int libres = std::stoi(*disp);
    if (libres < 0)
        return "RECHAZO: el evento " + idEvento + " no tiene ocurrencia el " + fecha;
    if (libres < numAsientos)
        return "RECHAZO: solo hay " + std::to_string(libres) + " asientos libres";

    // RESERVAR es una escritura, pero lleva el idSol: si se reenvia porque la respuesta
    // se perdio, la persistencia la reconoce y no la aplica dos veces
    auto r = pedirAPersistencia(persistencia,
        "RESERVAR " + idSol + " " + idEvento + " " + fecha + " " + cliente + " " + std::to_string(numAsientos));
    if (!r) return "RECHAZO: la persistencia no responde";

    // si otro cliente reservo justo antes, persistencia responde SIN_CUPO
    if (r->rfind("OK", 0) == 0)
        return "EXITO: reserva " + r->substr(3) + " de " + std::to_string(numAsientos) +
               " asientos en " + idEvento + " el " + fecha;
    return "RECHAZO: " + *r;
}

// modificacion: cambia la reserva del cliente en ese evento y fecha a otra fecha del mismo
// evento (puede ser la misma) y/o a otro numero de asientos. Si no se puede, la reserva
// queda como estaba: la persistencia lo hace todo en una transaccion.
std::string modificar(ReqConReintento& persistencia, const std::string& idSol, const std::string& idEvento,
                      const std::string& fecha, const std::string& cliente,
                      const std::string& nuevaFecha, int numAsientos) {
    if (numAsientos <= 0) return "RECHAZO: numero de asientos invalido";

    auto r = pedirAPersistencia(persistencia,
        "MODIFICAR " + idSol + " " + idEvento + " " + fecha + " " + cliente + " " + nuevaFecha + " " +
        std::to_string(numAsientos));
    if (!r) return "RECHAZO: la persistencia no responde";

    auto p = partir(*r);   // OK <idReserva> <asientosAntes> | NO_EXISTE | NO_OCURRENCIA | SIN_CUPO <libres>
    if (p.size() == 3 && p[0] == "OK")
        return "EXITO: reserva " + p[1] + " de " + cliente + " ahora es de " + std::to_string(numAsientos) +
               " asientos en " + idEvento + " el " + nuevaFecha + " (antes " + p[2] + " el " + fecha + ")";
    if (!p.empty() && p[0] == "NO_EXISTE")
        return "RECHAZO: " + cliente + " no tiene reserva en " + idEvento + " el " + fecha;
    if (!p.empty() && p[0] == "NO_OCURRENCIA")
        return "RECHAZO: el evento " + idEvento + " no tiene ocurrencia el " + nuevaFecha;
    if (p.size() == 2 && p[0] == "SIN_CUPO")
        return "RECHAZO: solo hay " + p[1] + " asientos libres el " + nuevaFecha + "; la reserva queda como estaba";
    return "RECHAZO: " + *r;
}

int main(int argc, char* argv[]) {
    if (argc > 1) ETIQUETA = "[RMC-" + std::string(argv[1]) + "]";
    auto cfg = leerConfig();
    zmq::context_t context(1);

    // Socket REP hacia los gestores. El RMC hace CONNECT a los dos (principal y respaldo),
    // por eso se pueden levantar varios RMC a la vez: ./rmc 1, ./rmc 2, ./rmc 3
    zmq::socket_t socketGestor(context, ZMQ_REP);
    socketGestor.connect(direccion(cfg["GESTOR_IP"], cfg["RMC_BACKEND_PUERTO"]));
    socketGestor.connect(direccion(cfg["GESTOR_IP"], cfg["RMC_BACKEND2_PUERTO"]));

    // REQ hacia la persistencia (VM3), con tiempo de espera y reintento
    ReqConReintento persistencia(context,
        {direccion(cfg["PERSISTENCIA_IP"], cfg["PERSISTENCIA_PUERTO"])},
        std::stoi(cfg["ESPERA_PERSISTENCIA_MS"]), std::stoi(cfg["INTENTOS_PERSISTENCIA"]), "RMC");

    std::cout << "RMC conectado a los gestores (" << cfg["RMC_BACKEND_PUERTO"] << " y "
              << cfg["RMC_BACKEND2_PUERTO"] << ") y a persistencia en "
              << cfg["PERSISTENCIA_IP"] << ":" << cfg["PERSISTENCIA_PUERTO"] << std::endl;

    socketGestor.set(zmq::sockopt::rcvtimeo, PULSO_MS);   // despertar cada segundo para el watchdog
    avisarListo();   // a systemd: ya estoy conectado y listo

    while (true) {
        avisarVivo();   // watchdog: cada vuelta, al menos una por segundo, systemd sabe que sigue bien
        zmq::message_t request;
        if (!socketGestor.recv(request, zmq::recv_flags::none)) continue;   // no llego nada en 1 s
        std::string msg(static_cast<char*>(request.data()), request.size());
        std::cout << ETIQUETA << " Recibido: " << msg << std::endl;

        // Interpretar comando: siempre <COMANDO> <idSol> <datos...>
        auto p = partir(msg);
        std::string comando = p.empty() ? "" : p[0];
        std::string respuesta;

        try {
            if (comando == "CONSULTA" && p.size() == 3) {
                respuesta = consultar(persistencia, p[2]);
            } else if (comando == "RESERVA" && p.size() == 6) {
                respuesta = reservar(persistencia, p[1], p[2], p[3], p[4], std::stoi(p[5]));
            } else if (comando == "MODIFICACION" && p.size() == 7) {
                respuesta = modificar(persistencia, p[1], p[2], p[3], p[4], p[5], std::stoi(p[6]));
            } else if (comando == "CONSULTA" || comando == "RESERVA" || comando == "MODIFICACION") {
                respuesta = "RECHAZO: formato invalido";
            } else {
                respuesta = "RECHAZO: comando no reconocido";
            }
        } catch (const std::exception&) {
            respuesta = "RECHAZO: formato invalido";
        }

        std::cout << ETIQUETA << " Resultado: " << respuesta.substr(0, respuesta.find('\n')) << std::endl;
        socketGestor.send(zmq::buffer(respuesta), zmq::send_flags::none);
    }
}
