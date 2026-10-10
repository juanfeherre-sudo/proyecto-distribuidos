// gestor.cpp  -  corre en la VM2 (capa de logica de negocio)
//
// Gestor de Transacciones (GT): puerta de entrada del sistema. Segun el tipo de solicitud
// la enruta por el canal sincrono o el asincrono:
//   - CONSULTA / RESERVA / MODIFICACION -> RMC (REQ/REP), espera la respuesta y la devuelve.
//   - ELIMINAR (cancelacion)            -> servicio de cancelacion (PUSH), responde de inmediato.
// Tambien cuenta las transacciones por minuto (TPM) para las pruebas de rendimiento.

#include <zmq.hpp>
#include <iostream>
#include <sstream>
#include <string>
#include <map>
#include <chrono>
#include <fstream>
#include "config.h"

int main() {
    auto cfg = leerConfig();
    zmq::context_t context(1);

    // Socket REP para clientes
    zmq::socket_t socketClientes(context, ZMQ_REP);
    socketClientes.bind("tcp://*:" + cfg["GESTOR_PUERTO"]);
    std::cout << "[Gestor] Escuchando clientes en puerto " << cfg["GESTOR_PUERTO"] << "..." << std::endl;

    // Canal sincrono: socket REQ hacia el RMC
    zmq::socket_t socketRMC(context, ZMQ_REQ);
    socketRMC.connect(direccion(cfg, "RMC"));
    std::cout << "[Gestor] Conectado al RMC en " << direccion(cfg, "RMC") << std::endl;

    // Canal asincrono: socket PUSH hacia el servicio de cancelacion (send no espera respuesta)
    zmq::socket_t socketCancelacion(context, ZMQ_PUSH);
    socketCancelacion.connect(direccion(cfg, "CANCELACION"));

    // TPM: transacciones exitosas por tipo en el minuto actual
    std::map<std::string, int> tpm;
    auto inicioMinuto = std::chrono::steady_clock::now();
    std::ofstream archivoTPM("tpm.csv", std::ios::app);

    while (true) {
        // Recibir solicitud del cliente
        zmq::message_t request;
        auto result = socketClientes.recv(request, zmq::recv_flags::none);
        std::string msg(static_cast<char*>(request.data()), request.size());
        std::cout << "[Gestor] Solicitud recibida: " << msg << std::endl;

        std::istringstream iss(msg);
        std::string tipo;
        iss >> tipo;
        std::string respuesta;

        if (tipo == "CONSULTA" || tipo == "RESERVA" || tipo == "MODIFICACION") {
            // Canal sincrono: reenviar al RMC y esperar su respuesta
            socketRMC.send(zmq::buffer(msg), zmq::send_flags::none);
            zmq::message_t respRMC;
            auto resultRMC = socketRMC.recv(respRMC, zmq::recv_flags::none);
            respuesta = std::string(static_cast<char*>(respRMC.data()), respRMC.size());
            if (respuesta.rfind("EXITO", 0) == 0) tpm[tipo]++;
        } else if (tipo == "ELIMINAR") {
            // Canal asincrono: se encola la cancelacion y se responde sin esperar
            socketCancelacion.send(zmq::buffer(msg), zmq::send_flags::none);
            respuesta = "RECIBIDO: cancelacion en proceso";
        } else {
            respuesta = "RECHAZO: comando no reconocido";
        }

        // Enviar respuesta al cliente
        std::cout << "[Gestor] Respuesta: " << respuesta.substr(0, respuesta.find('\n')) << std::endl;
        socketClientes.send(zmq::buffer(respuesta), zmq::send_flags::none);

        // Cada minuto se registra el TPM y se reinician los contadores
        auto ahora = std::chrono::steady_clock::now();
        if (ahora - inicioMinuto >= std::chrono::minutes(1)) {
            std::cout << "[Gestor] TPM consultas=" << tpm["CONSULTA"] << " reservas=" << tpm["RESERVA"] << std::endl;
            archivoTPM << tpm["CONSULTA"] << "," << tpm["RESERVA"] << std::endl;
            tpm.clear();
            inicioMinuto = ahora;
        }
    }
}
