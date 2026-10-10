// gestor.cpp  —  corre en la VM2 (capa de lógica de negocio)
//
// Gestor de Transacciones (GT): la puerta de entrada del sistema. No aplica reglas;
// solo mira el tipo de solicitud y la enruta por el canal que corresponde:
//   - Canal síncrono  (CONSULTA, RESERVA, MODIFICACION) -> RMC, espera su respuesta y la devuelve.
//   - Canal asíncrono (CANCELACION) -> la empuja al servicio de cancelación y responde
//     "RECIBIDO" de inmediato, sin esperar a que se procese.
// También cuenta las transacciones por minuto (TPM), que se usan en las pruebas de rendimiento.

#include "common.hpp"

int main() {
    auto cfg = leerConfig();
    zmq::context_t contexto(1);

    // Clientes -> Gestor: REP. Recibe una solicitud, responde, y pasa a la siguiente.
    zmq::socket_t clientes(contexto, zmq::socket_type::rep);
    clientes.bind("tcp://*:" + cfg["GESTOR_PUERTO"]);

    // Gestor -> RMC: REQ. Canal síncrono. Como están en la misma VM, la IP suele ser 127.0.0.1.
    zmq::socket_t rmc(contexto, zmq::socket_type::req);
    rmc.connect(direccion(cfg["RMC_IP"], cfg["RMC_PUERTO"]));

    // Gestor -> Cancelación: PUSH. Canal asíncrono: send() no espera respuesta.
    zmq::socket_t cancelacion(contexto, zmq::socket_type::push);
    cancelacion.connect(direccion(cfg["CANCELACION_IP"], cfg["CANCELACION_PUERTO"]));

    log("Gestor", "Escuchando clientes en el puerto " + cfg["GESTOR_PUERTO"] +
                  ", RMC en " + direccion(cfg["RMC_IP"], cfg["RMC_PUERTO"]));

    // Contadores para TPM: cuántas transacciones de cada tipo se respondieron en el minuto actual.
    std::map<std::string, int> tpm;
    auto inicioMinuto = std::chrono::steady_clock::now();
    std::ofstream archivoTPM("tpm.csv", std::ios::app);   // minuto,consultas,reservas

    while (true) {
        std::string solicitud = recibir(clientes);
        auto p = partir(solicitud);
        std::string tipo = p.empty() ? "" : p[0];
        log("Gestor", "<- Cliente: " + solicitud);
        std::string respuesta;

        if (tipo == "CONSULTA" || tipo == "RESERVA" || tipo == "MODIFICACION") {
            enviar(rmc, solicitud);          // reenviar tal cual al RMC...
            respuesta = recibir(rmc);        // ...y esperar (bloqueado) su respuesta
            if (respuesta.rfind("EXITO", 0) == 0) tpm[tipo]++;
        } else if (tipo == "CANCELACION") {
            enviar(cancelacion, solicitud);  // se encola y seguimos sin esperar
            respuesta = "RECIBIDO cancelacion en proceso; se notificara al terminar";
        } else {
            respuesta = "RECHAZO tipo de solicitud desconocido";
        }

        log("Gestor", "-> Cliente: " + respuesta.substr(0, respuesta.find('\n')));
        enviar(clientes, respuesta);

        // Cada 60 s, escribir el TPM del minuto que terminó y reiniciar los contadores.
        auto ahora = std::chrono::steady_clock::now();
        if (ahora - inicioMinuto >= std::chrono::minutes(1)) {
            log("Gestor", "TPM: consultas=" + std::to_string(tpm["CONSULTA"]) +
                          " reservas=" + std::to_string(tpm["RESERVA"]));
            archivoTPM << hora() << "," << tpm["CONSULTA"] << "," << tpm["RESERVA"] << std::endl;
            tpm.clear();
            inicioMinuto = ahora;
        }
    }
}
