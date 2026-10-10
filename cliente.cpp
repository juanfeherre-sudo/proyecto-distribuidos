// cliente.cpp  —  corre en la VM1 (capa de consumidores)
//
// Envía solicitudes al Gestor de Transacciones con REQ/REP (síncrono): manda una
// solicitud y se queda esperando la respuesta antes de mandar la siguiente.
//
// Dos modos de uso:
//   ./cliente <idCliente>                         menú interactivo
//   ./cliente <idCliente> <archivo> [porMinuto]   lee las operaciones de un archivo y las envía
//                                                 a 'porMinuto' solicitudes por minuto (por defecto 20)
//
// Formato del archivo (una operación por línea; el id del cliente se agrega solo):
//   CONSULTA 2026-11
//   RESERVA E5 2026-11-10 3
//
// En modo archivo guarda el tiempo de respuesta de cada solicitud en metricas_<idCliente>.csv.
// Para simular varios clientes simultáneos, se lanzan varios procesos con ids distintos.

#include "common.hpp"
#include <thread>

// Envía una solicitud, espera la respuesta y mide cuánto tardó (en milisegundos).
std::string solicitar(zmq::socket_t& gestor, const std::string& solicitud, double& ms) {
    auto inicio = std::chrono::steady_clock::now();
    enviar(gestor, solicitud);
    std::string respuesta = recibir(gestor);
    auto fin = std::chrono::steady_clock::now();
    ms = std::chrono::duration<double, std::milli>(fin - inicio).count();
    return respuesta;
}

// Convierte una línea del archivo o del menú en el mensaje del protocolo.
// "RESERVA E5 2026-11-10 3" -> "RESERVA E5 2026-11-10 <idCliente> 3"
std::string armarSolicitud(const std::string& linea, const std::string& idCliente) {
    auto p = partir(linea);
    if (p.size() == 2 && p[0] == "CONSULTA") return "CONSULTA " + p[1];
    if (p.size() == 4 && p[0] == "RESERVA")  return "RESERVA " + p[1] + " " + p[2] + " " + idCliente + " " + p[3];
    return "";
}

void modoArchivo(zmq::socket_t& gestor, const std::string& idCliente,
                 const std::string& ruta, int porMinuto) {
    std::ifstream archivo(ruta);
    if (!archivo) { std::cerr << "No se pudo abrir " << ruta << std::endl; return; }

    std::ofstream csv("metricas_" + idCliente + ".csv");
    csv << "cliente,operacion,hora,ms,resultado\n";
    auto pausa = std::chrono::milliseconds(60000 / porMinuto);   // 20/min -> una cada 3 s

    std::string linea;
    while (std::getline(archivo, linea)) {
        std::string solicitud = armarSolicitud(linea, idCliente);
        if (solicitud.empty()) continue;                         // línea vacía o comentario

        auto siguiente = std::chrono::steady_clock::now() + pausa;
        double ms;
        std::string respuesta = solicitar(gestor, solicitud, ms);
        std::string resultado = respuesta.substr(0, respuesta.find_first_of(" \n"));  // EXITO / RECHAZO

        log("Cliente " + idCliente, solicitud + "  =>  " + respuesta.substr(0, respuesta.find('\n')) +
                                    "  (" + std::to_string((int)ms) + " ms)");
        csv << idCliente << "," << partir(solicitud)[0] << "," << hora() << ","
            << ms << "," << resultado << "\n";
        std::this_thread::sleep_until(siguiente);                // respetar la tasa configurada
    }
    log("Cliente " + idCliente, "Terminado. Tiempos en metricas_" + idCliente + ".csv");
}

void modoInteractivo(zmq::socket_t& gestor, const std::string& idCliente) {
    while (true) {
        std::cout << "\n1. Consultar eventos de un mes\n2. Reservar\n0. Salir\n> ";
        std::string opcion, linea;
        std::cin >> opcion;
        if (opcion == "0") return;
        if (opcion == "1") {
            std::string mes;
            std::cout << "Mes (AAAA-MM): ";
            std::cin >> mes;
            linea = "CONSULTA " + mes;
        } else if (opcion == "2") {
            std::string evento, fecha, n;
            std::cout << "Evento (ej. E5): ";            std::cin >> evento;
            std::cout << "Fecha (AAAA-MM-DD): ";        std::cin >> fecha;
            std::cout << "Numero de asientos: ";        std::cin >> n;
            linea = "RESERVA " + evento + " " + fecha + " " + n;
        } else {
            continue;
        }
        double ms;
        std::string respuesta = solicitar(gestor, armarSolicitud(linea, idCliente), ms);
        std::cout << respuesta << "\n(" << (int)ms << " ms)" << std::endl;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Uso: ./cliente <idCliente> [archivo] [solicitudesPorMinuto]" << std::endl;
        return 1;
    }
    std::string idCliente = argv[1];
    auto cfg = leerConfig();

    zmq::context_t contexto(1);
    zmq::socket_t gestor(contexto, zmq::socket_type::req);       // REQ: envío -> espero respuesta
    gestor.connect(direccion(cfg["GESTOR_IP"], cfg["GESTOR_PUERTO"]));
    log("Cliente " + idCliente, "Conectado al Gestor en " + direccion(cfg["GESTOR_IP"], cfg["GESTOR_PUERTO"]));

    if (argc >= 3) modoArchivo(gestor, idCliente, argv[2], argc >= 4 ? std::stoi(argv[3]) : 20);
    else           modoInteractivo(gestor, idCliente);
    return 0;
}
