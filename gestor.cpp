// Gestor de transacciones (VM2)
// consulta y reserva van al RMC, las cancelaciones van a cancelaciones por PUSH

#include "common.hpp"
#include <iostream>
#include <sstream>
#include <string>
#include <ctime>

int main(int argc, char* argv[]) {
    auto cfg = leerConfig();
    bool esRespaldo = (argc > 1 && std::string(argv[1]) == "2");
    std::string puertoClientes = esRespaldo ? cfg["GESTOR2_PUERTO"] : cfg["GESTOR_PUERTO"];

    zmq::context_t context(1);

    // Socket REP para clientes
    zmq::socket_t socketClientes(context, ZMQ_REP);
    socketClientes.set(zmq::sockopt::rcvtimeo, PULSO_MS);   // despertar cada segundo para el watchdog
    socketClientes.bind("tcp://*:" + puertoClientes);
    std::cout << "Gestor escuchando clientes en puerto " << puertoClientes << "..." << std::endl;

    // Socket REQ para los RMC (sincrono). El gestor hace BIND y los RMC hacen connect:
    // asi se pueden levantar varios RMC sin que choquen por el puerto, y el REQ
    // les reparte las solicitudes por turnos.
    //
    // Lazy Pirate hacia los RMC: si un RMC no responde en ESPERA_RMC_MS, se reenvia la
    // misma solicitud y el REQ, que reparte por turnos, se la entrega a otro RMC.
    // Como este socket hace bind no se puede botar y reabrir (chocaria con su propio
    // puerto), asi que se usan dos opciones de ZeroMQ:
    //   req_relaxed:   deja volver a enviar aunque no haya llegado la respuesta anterior
    //   req_correlate: si la respuesta vieja llega tarde, se descarta en vez de confundirla
    std::string puertoRMC = esRespaldo ? cfg["RMC_BACKEND2_PUERTO"] : cfg["RMC_BACKEND_PUERTO"];
    int esperaRMC = std::stoi(cfg["ESPERA_RMC_MS"]);
    int intentosRMC = std::stoi(cfg["INTENTOS_RMC"]);
    zmq::socket_t socketRMC(context, ZMQ_REQ);
    socketRMC.set(zmq::sockopt::rcvtimeo, esperaRMC);   // no esperar la respuesta para siempre
    socketRMC.set(zmq::sockopt::sndtimeo, esperaRMC);   // ni quedarse trabado si no hay ningun RMC
    socketRMC.set(zmq::sockopt::req_relaxed, 1);
    socketRMC.set(zmq::sockopt::req_correlate, 1);
    socketRMC.bind("tcp://*:" + puertoRMC);
    std::cout << "Gestor esperando RMC en puerto " << puertoRMC << "..." << std::endl;

    // Socket PUSH para cancelaciones (asincrono, no espera respuesta)
    zmq::socket_t socketCancelacion(context, ZMQ_PUSH);
    socketCancelacion.connect(direccion(cfg["CANCELACION_IP"], cfg["CANCELACION_PUERTO"]));

    // contadores para las transacciones por minuto (TPM)
    int consultasMinuto = 0;
    int reservasMinuto = 0;
    time_t inicioMinuto = time(nullptr);

    avisarListo();   // a systemd: ya hice los bind, estoy listo

    while (true) {
        avisarVivo();   // watchdog: cada vuelta, al menos una por segundo, systemd sabe que sigue bien

        // Recibir solicitud del cliente (si no llega nada en PULSO_MS, se vuelve a esperar)
        zmq::message_t request;
        if (!socketClientes.recv(request, zmq::recv_flags::none)) continue;
        std::string msg(static_cast<char*>(request.data()), request.size());
        std::cout << "[Gestor] Solicitud recibida: " << msg << std::endl;

        std::istringstream iss(msg);
        std::string tipo;
        iss >> tipo;
        std::string respuesta;

        if (tipo == "CONSULTA" || tipo == "RESERVA" || tipo == "MODIFICACION") {
            // se manda a un RMC y se espera la respuesta; si no llega, se reenvia a otro
            for (int intento = 1; intento <= intentosRMC && respuesta.empty(); intento++) {
                zmq::message_t respRMC;
                if (socketRMC.send(zmq::buffer(msg), zmq::send_flags::none) &&
                    socketRMC.recv(respRMC, zmq::recv_flags::none)) {
                    respuesta = respRMC.to_string();
                } else {
                    std::cout << "[Gestor] Ningun RMC respondio en " << esperaRMC << " ms (intento "
                              << intento << " de " << intentosRMC << ")" << std::endl;
                }
            }
            if (respuesta.empty()) respuesta = "RECHAZO: ningun RMC respondio";

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
