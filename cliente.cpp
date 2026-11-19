// Cliente (VM1)
// ./cliente                    -> se escribe cada solicitud en una linea, ej: RESERVA E5 2026-11-02 juan 3
// ./cliente archivo.txt 20     -> manda las operaciones del archivo, 20 por minuto
// formato (igual en los dos modos): CONSULTA 2026-11 / RESERVA E5 2026-11-02 juan 3 / ELIMINAR E5 2026-11-02 juan

#include "common.hpp"
#include <cctype>
#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <thread>

// manda una solicitud al gestor, espera la respuesta y mide cuanto se demoro (ms).
// Si el gestor no responde a tiempo, ReqConReintento reenvia la misma solicitud al
// otro gestor (Lazy Pirate). El tiempo medido incluye esos reintentos.
std::string enviarSolicitud(ReqConReintento& gestor, const std::string& solicitud, double& ms) {
    auto inicio = std::chrono::steady_clock::now();
    auto respuesta = gestor.pedir(solicitud);
    auto fin = std::chrono::steady_clock::now();
    ms = std::chrono::duration<double, std::milli>(fin - inicio).count();

    if (!respuesta) return "SIN_RESPUESTA: ningun gestor respondio";
    return *respuesta;
}

// revisa que la linea sea una solicitud conocida con el numero de datos correcto,
// para avisar del error de una vez sin mandarla al gestor
bool formatoValido(const std::string& linea) {
    std::istringstream iss(linea);
    std::string tipo, dato;
    iss >> tipo;
    int datos = 0;
    while (iss >> dato) datos++;
    return (tipo == "CONSULTA" && datos == 1) ||
           (tipo == "RESERVA"  && datos == 4) ||
           (tipo == "ELIMINAR" && datos == 3);
}

// lee las operaciones de un archivo y las manda una por una (porMinuto = solicitudes por minuto)
void modoArchivo(ReqConReintento& gestor, const std::string& ruta, int porMinuto) {
    std::ifstream archivo(ruta);
    if (!archivo) {
        std::cout << "No se encontro " << ruta << std::endl;
        return;
    }
    std::ofstream csv("metricas_" + ruta + ".csv");
    csv << "operacion,ms,resultado" << std::endl;
    int espera = 60000 / porMinuto;   // 20 por minuto = una cada 3000 ms

    std::string linea;
    while (std::getline(archivo, linea)) {
        if (linea.empty() || linea[0] == '#') continue;

        double ms;
        std::string respuesta = enviarSolicitud(gestor, linea, ms);
        std::string operacion = linea.substr(0, linea.find(' '));
        std::string resultado = "RECHAZO";
        if (respuesta.substr(0, 5) == "EXITO") resultado = "EXITO";
        if (respuesta.substr(0, 8) == "RECIBIDO") resultado = "RECIBIDO";
        if (respuesta.substr(0, 13) == "SIN_RESPUESTA") resultado = "SIN_RESPUESTA";

        std::cout << linea << "  =>  " << respuesta.substr(0, respuesta.find('\n'))
                  << "  (" << (int)ms << " ms)" << std::endl;
        csv << operacion << "," << ms << "," << resultado << std::endl;

        std::this_thread::sleep_for(std::chrono::milliseconds(espera));
    }
    std::cout << "Terminado. Tiempos guardados en metricas_" << ruta << ".csv" << std::endl;
}

int main(int argc, char* argv[]) {
    auto cfg = leerConfig();
    zmq::context_t context(1);

    // Los dos gestores: se empieza por el principal y, si no responde, se pasa al respaldo
    ReqConReintento gestor(context,
        {direccion(cfg["GESTOR_IP"], cfg["GESTOR_PUERTO"]),
         direccion(cfg["GESTOR_IP"], cfg["GESTOR2_PUERTO"])},
        std::stoi(cfg["ESPERA_GESTOR_MS"]), std::stoi(cfg["INTENTOS_GESTOR"]), "Cliente");

    if (argc > 1) {
        int porMinuto = 20;
        if (argc > 2) porMinuto = std::stoi(argv[2]);
        modoArchivo(gestor, argv[1], porMinuto);
        return 0;
    }

    // modo interactivo: cada solicitud se escribe completa en una sola linea,
    // con el mismo formato del archivo de operaciones (sin menu)
    std::cout << "Escriba una solicitud por linea (SALIR para terminar):\n"
              << "  CONSULTA <AAAA-MM>\n"
              << "  RESERVA <evento> <AAAA-MM-DD> <cliente> <asientos>\n"
              << "  ELIMINAR <evento> <AAAA-MM-DD> <cliente>\n";

    std::string linea;
    while (std::cout << "> " && std::getline(std::cin, linea)) {
        // la primera palabra se pasa a mayusculas: "consulta 2026-11" tambien sirve
        for (size_t i = 0; i < linea.size() && linea[i] != ' '; i++)
            linea[i] = std::toupper(static_cast<unsigned char>(linea[i]));

        if (linea.empty()) continue;
        if (linea == "SALIR") break;
        if (!formatoValido(linea)) {
            std::cout << "Formato no valido. Ejemplos:\n"
                      << "  CONSULTA 2026-11\n"
                      << "  RESERVA E1 2026-11-18 laura 2\n"
                      << "  ELIMINAR E1 2026-11-18 laura" << std::endl;
            continue;
        }

        double ms;
        std::string respuesta = enviarSolicitud(gestor, linea, ms);
        std::cout << respuesta << "\n(" << (int)ms << " ms)" << std::endl;
    }
    return 0;
}
