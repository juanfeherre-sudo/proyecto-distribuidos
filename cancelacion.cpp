// cancelacion.cpp  —  corre en la VM2 (capa de lógica de negocio)
//
// Servicio de Cancelaciones y Notificaciones. En la 1ª entrega solo se pide consulta y reserva,
// así que este proceso existe (es parte de la arquitectura base, Figura 1) pero solo
// recibe las cancelaciones y las registra en el log.
//
// En la 2ª entrega:
//   1. Recibir la cancelación del Gestor (PULL).
//   2. Pedir a la Persistencia que libere los asientos (REQ: "LIBERAR <idReserva>").
//   3. Notificar al cliente que la cancelación se hizo efectiva (PUB, tema = id del cliente).

#include "common.hpp"

int main() {
    auto cfg = leerConfig();
    zmq::context_t contexto(1);

    // Gestor -> Cancelación: PULL. Recibe lo que el Gestor empuja con PUSH.
    zmq::socket_t entrada(contexto, zmq::socket_type::pull);
    entrada.bind("tcp://*:" + cfg["CANCELACION_PUERTO"]);

    // Cancelación -> Clientes: PUB. Cada cliente se suscribe a su propio id (2ª entrega).
    zmq::socket_t notificaciones(contexto, zmq::socket_type::pub);
    notificaciones.bind("tcp://*:" + cfg["NOTIFICACION_PUERTO"]);

    log("Cancelacion", "Esperando cancelaciones en el puerto " + cfg["CANCELACION_PUERTO"]);

    while (true) {
        std::string solicitud = recibir(entrada);
        log("Cancelacion", "<- Gestor: " + solicitud + "  (se procesara en la 2a entrega)");
    }
}
