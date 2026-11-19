// Cliente (VM1)
//
// ./cliente                       -> se escribe cada solicitud en una linea
// ./cliente archivo.txt 20        -> manda las operaciones del archivo, 20 por minuto
// ./cliente carga C1 50 50 0 60 20
//                                 -> genera 60 operaciones del cliente C1 al azar, con
//                                    50 % consultas, 50 % reservas y 0 % modificaciones, 20 por minuto
//
// Formato de una solicitud (igual en la terminal y en el archivo):
//   CONSULTA <AAAA-MM>
//   RESERVA <evento> <AAAA-MM-DD> <cliente> <asientos>
//   MODIFICACION <evento> <AAAA-MM-DD> <cliente> <nuevaFecha> <asientos>
//   ELIMINAR <evento> <AAAA-MM-DD> <cliente>
//
// Antes de enviarla, el cliente le agrega un id unico (idSol) despues del tipo:
//   RESERVA a3f9c1-7 E1 2026-11-18 laura 2
// Si hay que reenviarla (Lazy Pirate), va con el mismo idSol y la persistencia no la aplica dos veces.
//
// Las cancelaciones son asincronas: el gestor responde RECIBIDO de una vez y el resultado
// llega despues como notificacion (SUB), que se imprime como [Notificacion] ...
//
// En los modos archivo y carga guarda el tiempo de cada solicitud en un CSV para las graficas.

#include "common.hpp"
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Id unico por solicitud: un prefijo al azar por cada cliente que se abre + un contador.
// ---------------------------------------------------------------------------
std::mt19937 azar{std::random_device{}()};
std::string prefijoIdSol;
int contadorSolicitudes = 0;

std::string conIdSol(const std::string& linea) {
    std::string idSol = prefijoIdSol + "-" + std::to_string(++contadorSolicitudes);
    size_t espacio = linea.find(' ');
    return linea.substr(0, espacio) + " " + idSol + linea.substr(espacio);
}

// manda una solicitud al gestor, espera la respuesta y mide cuanto se demoro (ms).
// Si el gestor no responde a tiempo, ReqConReintento reenvia la misma solicitud (mismo
// idSol) al otro gestor (Lazy Pirate). El tiempo medido incluye esos reintentos.
std::string enviarSolicitud(ReqConReintento& gestor, const std::string& linea, double& ms) {
    std::string solicitud = conIdSol(linea);
    auto inicio = std::chrono::steady_clock::now();
    auto respuesta = gestor.pedir(solicitud);
    auto fin = std::chrono::steady_clock::now();
    ms = std::chrono::duration<double, std::milli>(fin - inicio).count();

    if (!respuesta) return "SIN_RESPUESTA: ningun gestor respondio";
    return *respuesta;
}

std::string resultadoDe(const std::string& respuesta) {   // para la columna del CSV
    if (respuesta.substr(0, 5) == "EXITO") return "EXITO";
    if (respuesta.substr(0, 8) == "RECIBIDO") return "RECIBIDO";
    if (respuesta.substr(0, 13) == "SIN_RESPUESTA") return "SIN_RESPUESTA";
    return "RECHAZO";
}

// revisa que la linea sea una solicitud conocida con el numero de datos correcto,
// para avisar del error de una vez sin mandarla al gestor
bool formatoValido(const std::string& linea) {
    auto p = partir(linea);
    if (p.empty()) return false;
    return (p[0] == "CONSULTA"     && p.size() == 2) ||
           (p[0] == "RESERVA"      && p.size() == 5) ||
           (p[0] == "MODIFICACION" && p.size() == 6) ||
           (p[0] == "ELIMINAR"     && p.size() == 4);
}

// ---------------------------------------------------------------------------
// Notificaciones de cancelacion (PUB/SUB).
// Un hilo aparte tiene el socket SUB y espera las notificaciones mientras el hilo
// principal sigue mandando solicitudes. El tema es el nombre del cliente seguido de
// un espacio ("laura "): asi "laura" no recibe tambien las de "laura2".
// Un socket de ZeroMQ no se comparte entre hilos, por eso las suscripciones nuevas se
// le piden al hilo por una lista protegida con un mutex, y el hilo las aplica.
// ---------------------------------------------------------------------------
std::mutex mutexNotif;
std::condition_variable avisoNotif;
std::vector<std::string> porSuscribir;   // nombres que el hilo todavia no ha suscrito
std::set<std::string> suscritos;
std::atomic<bool> terminar{false};

void escucharNotificaciones(zmq::context_t* contexto, std::string direccionPub) {
    zmq::socket_t sub(*contexto, ZMQ_SUB);
    sub.set(zmq::sockopt::rcvtimeo, 100);   // revisar cada 100 ms si hay suscripciones nuevas
    sub.connect(direccionPub);

    while (!terminar) {
        {
            std::lock_guard<std::mutex> candado(mutexNotif);
            for (const auto& nombre : porSuscribir) {
                sub.set(zmq::sockopt::subscribe, nombre + " ");
                suscritos.insert(nombre);
            }
            if (!porSuscribir.empty()) {
                porSuscribir.clear();
                avisoNotif.notify_all();
            }
        }
        zmq::message_t mensaje;
        if (sub.recv(mensaje, zmq::recv_flags::none))
            std::cout << "[Notificacion] " << mensaje.to_string() << std::endl;
    }
}

// pide las notificaciones de un cliente y espera a que el hilo ya este suscrito,
// para no perder la notificacion de la cancelacion que se va a mandar ahora
void suscribir(const std::string& nombre) {
    std::unique_lock<std::mutex> candado(mutexNotif);
    if (suscritos.count(nombre)) return;
    porSuscribir.push_back(nombre);
    avisoNotif.wait(candado, [&] { return suscritos.count(nombre) > 0; });
}

// si la linea es una cancelacion, suscribirse al cliente antes de mandarla
void prepararNotificacion(const std::string& linea) {
    auto p = partir(linea);
    if (p.size() == 4 && p[0] == "ELIMINAR") suscribir(p[3]);
}

// ---------------------------------------------------------------------------
// Modo archivo: lee las operaciones de un archivo y las manda una por una
// ---------------------------------------------------------------------------
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
        prepararNotificacion(linea);

        double ms;
        std::string respuesta = enviarSolicitud(gestor, linea, ms);
        std::cout << linea << "  =>  " << respuesta.substr(0, respuesta.find('\n'))
                  << "  (" << (int)ms << " ms)" << std::endl;
        csv << linea.substr(0, linea.find(' ')) << "," << ms << "," << resultadoDe(respuesta) << std::endl;

        std::this_thread::sleep_for(std::chrono::milliseconds(espera));
    }
    std::cout << "Terminado. Tiempos guardados en metricas_" << ruta << ".csv" << std::endl;
}

// ---------------------------------------------------------------------------
// Modo carga: genera operaciones al azar con el porcentaje pedido de cada tipo.
// Es lo que se usa en los experimentos (Tabla 1 del enunciado).
//   - Las consultas son del mes MES_CARGA.
//   - Las reservas son de 1 a 3 asientos en una ocurrencia al azar.
//   - Las modificaciones cambian una reserva que el cliente ya tiene: a otra fecha del
//     mismo evento o a otro numero de asientos. Si todavia no tiene ninguna, esa
//     operacion se manda como reserva, para tener algo que modificar.
// ---------------------------------------------------------------------------
const std::string MES_CARGA = "2026-11";

struct Ocurrencia {
    std::string evento, fecha;
};

// pide la lista de eventos del mes y la convierte en ocurrencias (evento, fecha)
std::vector<Ocurrencia> leerEventos(ReqConReintento& gestor) {
    double ms;
    std::string respuesta = enviarSolicitud(gestor, "CONSULTA " + MES_CARGA, ms);
    std::vector<Ocurrencia> lista;
    std::istringstream lineas(respuesta);
    std::string linea;
    while (std::getline(lineas, linea)) {   // "Evento: E1 | Fecha: 2026-11-18 | Libres: ..."
        auto p = partir(linea);
        if (p.size() >= 5 && p[0] == "Evento:") lista.push_back({p[1], p[4]});
    }
    return lista;
}

void modoCarga(ReqConReintento& gestor, const std::string& nombre, int pctConsulta, int pctReserva,
               int pctModificacion, int operaciones, int porMinuto) {
    auto eventos = leerEventos(gestor);
    if (eventos.empty()) {
        std::cout << "No se pudo leer la lista de eventos de " << MES_CARGA << std::endl;
        return;
    }
    std::map<std::string, std::vector<std::string>> fechasDe;   // evento -> sus fechas
    for (const auto& o : eventos) fechasDe[o.evento].push_back(o.fecha);

    std::vector<Ocurrencia> misReservas;   // donde tiene reserva este cliente
    auto alAzar = [](int desde, int hasta) {
        return std::uniform_int_distribution<int>(desde, hasta)(azar);
    };

    std::ofstream csv("metricas_" + nombre + ".csv");
    csv << "operacion,ms,resultado" << std::endl;
    int espera = 60000 / porMinuto;
    std::cout << "Carga de " << nombre << ": " << operaciones << " operaciones, " << pctConsulta
              << " % consulta, " << pctReserva << " % reserva, " << pctModificacion
              << " % modificacion, " << porMinuto << " por minuto" << std::endl;

    for (int i = 0; i < operaciones; i++) {
        int dado = alAzar(1, 100);
        std::string linea;
        if (dado <= pctConsulta) {
            linea = "CONSULTA " + MES_CARGA;
        } else if (dado <= pctConsulta + pctReserva || misReservas.empty()) {
            const auto& o = eventos[alAzar(0, eventos.size() - 1)];
            linea = "RESERVA " + o.evento + " " + o.fecha + " " + nombre + " " + std::to_string(alAzar(1, 3));
        } else {
            int cual = alAzar(0, misReservas.size() - 1);
            const auto& r = misReservas[cual];
            const auto& fechas = fechasDe[r.evento];
            std::string nuevaFecha = fechas[alAzar(0, fechas.size() - 1)];
            linea = "MODIFICACION " + r.evento + " " + r.fecha + " " + nombre + " " + nuevaFecha + " " +
                    std::to_string(alAzar(1, 3));
        }

        double ms;
        std::string respuesta = enviarSolicitud(gestor, linea, ms);
        std::string operacion = linea.substr(0, linea.find(' '));
        std::string resultado = resultadoDe(respuesta);
        std::cout << linea << "  =>  " << respuesta.substr(0, respuesta.find('\n'))
                  << "  (" << (int)ms << " ms)" << std::endl;
        csv << operacion << "," << ms << "," << resultado << std::endl;

        // llevar la cuenta de donde tiene reservas para poder modificarlas despues
        if (resultado == "EXITO") {
            auto p = partir(linea);
            if (operacion == "RESERVA") {
                misReservas.push_back({p[1], p[2]});
            } else if (operacion == "MODIFICACION") {
                for (auto it = misReservas.begin(); it != misReservas.end();)   // todas quedan en una
                    it = (it->evento == p[1] && it->fecha == p[2]) ? misReservas.erase(it) : it + 1;
                misReservas.push_back({p[1], p[4]});
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(espera));
    }
    std::cout << "Terminado. Tiempos guardados en metricas_" << nombre << ".csv" << std::endl;
}

// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    auto cfg = leerConfig();
    zmq::context_t context(1);

    char prefijo[8];
    std::snprintf(prefijo, sizeof(prefijo), "%06x", (unsigned)(azar() & 0xFFFFFF));
    prefijoIdSol = prefijo;

    // Los dos gestores: se empieza por el principal y, si no responde, se pasa al respaldo
    ReqConReintento gestor(context,
        {direccion(cfg["GESTOR_IP"], cfg["GESTOR_PUERTO"]),
         direccion(cfg["GESTOR_IP"], cfg["GESTOR2_PUERTO"])},
        std::stoi(cfg["ESPERA_GESTOR_MS"]), std::stoi(cfg["INTENTOS_GESTOR"]), "Cliente");

    // hilo que recibe las notificaciones de cancelacion (el PUB esta en la VM de cancelaciones)
    std::thread hiloNotificaciones(escucharNotificaciones, &context,
                                   direccion(cfg["CANCELACION_IP"], cfg["NOTIFICACION_PUERTO"]));

    if (argc >= 7 && std::string(argv[1]) == "carga") {
        int c = std::stoi(argv[3]), r = std::stoi(argv[4]), m = std::stoi(argv[5]);
        if (c + r + m != 100) {
            std::cout << "Los porcentajes deben sumar 100 (van " << c + r + m << ")" << std::endl;
        } else {
            modoCarga(gestor, argv[2], c, r, m, std::stoi(argv[6]), argc > 7 ? std::stoi(argv[7]) : 20);
        }
    } else if (argc > 1 && std::string(argv[1]) == "carga") {
        std::cout << "uso: ./cliente carga <nombre> <%consulta> <%reserva> <%modificacion> <operaciones> [porMinuto]\n"
                  << "ej:  ./cliente carga C1 50 50 0 60 20" << std::endl;
    } else if (argc > 1) {
        modoArchivo(gestor, argv[1], argc > 2 ? std::stoi(argv[2]) : 20);
    } else {
        // modo interactivo: cada solicitud se escribe completa en una sola linea, sin menu
        std::cout << "Escriba una solicitud por linea (SALIR para terminar):\n"
                  << "  CONSULTA <AAAA-MM>\n"
                  << "  RESERVA <evento> <AAAA-MM-DD> <cliente> <asientos>\n"
                  << "  MODIFICACION <evento> <AAAA-MM-DD> <cliente> <nuevaFecha> <asientos>\n"
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
                          << "  MODIFICACION E1 2026-11-18 laura 2026-11-06 3\n"
                          << "  ELIMINAR E1 2026-11-18 laura" << std::endl;
                continue;
            }
            prepararNotificacion(linea);

            double ms;
            std::string respuesta = enviarSolicitud(gestor, linea, ms);
            std::cout << respuesta << "\n(" << (int)ms << " ms)" << std::endl;
        }
    }

    // antes de salir, dar un momento por si falta llegar la notificacion de la ultima cancelacion
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    terminar = true;
    hiloNotificaciones.join();
    return 0;
}
