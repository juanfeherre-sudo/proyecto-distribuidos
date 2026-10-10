// Gestor de transacciones (VM2)
// consulta y reserva van al RMC, las cancelaciones van a cancelaciones por PUSH

#include <zmq.hpp>
#include <iostream>
#include <sstream>
#include <string>
#include <ctime>

int main() {
    zmq::context_t context(1);

    // Socket REP para clientes
    zmq::socket_t socketClientes(context, ZMQ_REP);
    socketClientes.bind("tcp://*:5555");
    std::cout << "Gestor escuchando clientes en puerto 5555..." << std::endl;

    // Socket REQ para el RMC (sincrono)
    zmq::socket_t socketRMC(context, ZMQ_REQ);
    socketRMC.connect("tcp://localhost:5556"); // el RMC esta en la misma VM
    std::cout << "Gestor conectado al RMC en puerto 5556..." << std::endl;

    // Socket PUSH para cancelaciones (asincrono, no espera respuesta)
    zmq::socket_t socketCancelacion(context, ZMQ_PUSH);
    socketCancelacion.connect("tcp://localhost:5557");

    // contadores para las transacciones por minuto (TPM)
    int consultasMinuto = 0;
    int reservasMinuto = 0;
    time_t inicioMinuto = time(nullptr);

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
            // se manda al RMC y se espera la respuesta
            socketRMC.send(zmq::buffer(msg), zmq::send_flags::none);
            zmq::message_t respRMC;
            auto resultRMC = socketRMC.recv(respRMC, zmq::recv_flags::none);
            respuesta = std::string(static_cast<char*>(respRMC.data()), respRMC.size());

            if (respuesta.substr(0, 5) == "EXITO") {
                if (tipo == "CONSULTA") consultasMinuto++;
                if (tipo == "RESERVA") reservasMinuto++;
            }
        } else if (tipo == "ELIMINAR") {
            // se manda a cancelaciones y se responde de una vez, sin esperar
            socketCancelacion.send(zmq::buffer(msg), zmq::send_flags::none);
            respuesta = "RECIBIDO: cancelacion en proceso";
        } else {
            respuesta = "RECHAZO: comando no reconocido";
        }

        // Enviar respuesta al cliente
        std::cout << "[Gestor] Respuesta enviada al cliente" << std::endl;
        socketClientes.send(zmq::buffer(respuesta), zmq::send_flags::none);

        // cada minuto se muestra el TPM y se reinician los contadores
        if (time(nullptr) - inicioMinuto >= 60) {
            std::cout << "[Gestor] TPM -> consultas: " << consultasMinuto
                      << " reservas: " << reservasMinuto << std::endl;
            consultasMinuto = 0;
            reservasMinuto = 0;
            inicioMinuto = time(nullptr);
        }
    }
}
