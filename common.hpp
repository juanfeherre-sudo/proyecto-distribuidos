// common.hpp
// Código que comparten TODOS los programas: leer la configuración,
// enviar/recibir texto por ZeroMQ, partir mensajes y escribir logs.
#ifndef COMMON_HPP
#define COMMON_HPP

#include <zmq.hpp>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Configuración: lee config.txt, que tiene líneas CLAVE=VALOR.
// Así las IPs y puertos no quedan "quemados" en el código: para pasar de
// probar en un solo computador (127.0.0.1) a las 3 VMs solo se edita el archivo.
// ---------------------------------------------------------------------------
inline std::map<std::string, std::string> leerConfig(const std::string& ruta = "config.txt") {
    std::map<std::string, std::string> cfg;
    std::ifstream archivo(ruta);
    if (!archivo) {
        std::cerr << "No se pudo abrir " << ruta << std::endl;
        std::exit(1);
    }
    std::string linea;
    while (std::getline(archivo, linea)) {
        if (linea.empty() || linea[0] == '#') continue;      // saltar vacías y comentarios
        auto pos = linea.find('=');
        if (pos == std::string::npos) continue;
        cfg[linea.substr(0, pos)] = linea.substr(pos + 1);   // "GESTOR_PUERTO=5555" -> cfg["GESTOR_PUERTO"]="5555"
    }
    return cfg;
}

// Arma una dirección de ZeroMQ: direccion("10.0.0.5", "6666") -> "tcp://10.0.0.5:6666"
inline std::string direccion(const std::string& ip, const std::string& puerto) {
    return "tcp://" + ip + ":" + puerto;
}

// ---------------------------------------------------------------------------
// Enviar y recibir texto. ZeroMQ trabaja con bytes; estas funciones convierten
// entre std::string y zmq::message_t para no repetir ese código en cada programa.
// ---------------------------------------------------------------------------
inline void enviar(zmq::socket_t& socket, const std::string& texto) {
    socket.send(zmq::buffer(texto), zmq::send_flags::none);
}

inline std::string recibir(zmq::socket_t& socket) {
    zmq::message_t mensaje;
    auto ok = socket.recv(mensaje, zmq::recv_flags::none);   // se bloquea hasta que llegue algo
    if (!ok) return "";
    return mensaje.to_string();
}

// ---------------------------------------------------------------------------
// Mensajes de varias partes. Los necesitan ROUTER y DEALER, que le pegan al
// mensaje un "sobre" con la identidad del cliente para saber a quién responderle:
//   [identidad, "", texto]
// ---------------------------------------------------------------------------
inline std::vector<std::string> recibirPartes(zmq::socket_t& socket) {
    std::vector<std::string> partes;
    while (true) {
        zmq::message_t parte;
        auto ok = socket.recv(parte, zmq::recv_flags::none);
        if (!ok) break;
        partes.push_back(parte.to_string());
        if (!parte.more()) break;                            // more() = todavía faltan partes
    }
    return partes;
}

inline void enviarPartes(zmq::socket_t& socket, const std::vector<std::string>& partes) {
    if (partes.empty()) return;
    for (size_t i = 0; i + 1 < partes.size(); i++)
        socket.send(zmq::buffer(partes[i]), zmq::send_flags::sndmore);
    socket.send(zmq::buffer(partes.back()), zmq::send_flags::none);
}

// Parte un mensaje por espacios: "RESERVA E5 2026-11-10 ana 3" -> {"RESERVA","E5","2026-11-10","ana","3"}
inline std::vector<std::string> partir(const std::string& texto) {
    std::istringstream iss(texto);
    std::vector<std::string> partes;
    std::string p;
    while (iss >> p) partes.push_back(p);
    return partes;
}

// Hora actual "HH:MM:SS" para que cada línea del log diga cuándo pasó.
inline std::string hora() {
    std::time_t t = std::time(nullptr);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&t), "%H:%M:%S");
    return oss.str();
}

// Escribe "[12:01:33][RMC] mensaje" en la consola.
inline void log(const std::string& quien, const std::string& texto) {
    std::cout << "[" << hora() << "][" << quien << "] " << texto << std::endl;
}

// ---------------------------------------------------------------------------
// Socket REQ que NO se queda colgado si el otro extremo se muere.
//
// Un REQ normal espera la respuesta para siempre: si matan al servicio, el
// programa se congela y con él todos los que dependen de él. Aquí se le pone
// un tiempo de espera; si se vence, el socket REQ queda inservible (su máquina
// de estados quedó a medias), así que se bota, se abre otro contra la siguiente
// dirección de la lista y se reenvía el MISMO mensaje.
//
// Es el patrón "Lazy Pirate" de la guía de ZeroMQ. Reenviar es seguro porque
// cada solicitud lleva su idSol y la persistencia no la aplica dos veces.
// ---------------------------------------------------------------------------
class ReqConReintento {
public:
    ReqConReintento(zmq::context_t& contexto, const std::vector<std::string>& direcciones,
                    int esperaMs, int intentos)
        : contexto_(contexto), direcciones_(direcciones), esperaMs_(esperaMs),
          intentos_(intentos), socket_(contexto, zmq::socket_type::req) {
        abrir();
    }

    // Devuelve la respuesta. Si se agotaron los intentos devuelve "nada" (optional vacío),
    // que NO es lo mismo que una respuesta vacía: una consulta sin resultados responde "".
    std::optional<std::string> pedir(const std::string& mensaje) {
        for (int intento = 0; intento < intentos_; intento++) {
            enviar(socket_, mensaje);
            zmq::message_t respuesta;
            if (socket_.recv(respuesta, zmq::recv_flags::none)) return respuesta.to_string();

            // No contestó a tiempo: botar el socket y probar la siguiente réplica.
            actual_ = (actual_ + 1) % direcciones_.size();
            abrir();
        }
        return std::nullopt;
    }

    std::string donde() const { return direcciones_[actual_]; }

private:
    void abrir() {
        socket_ = zmq::socket_t(contexto_, zmq::socket_type::req);
        socket_.set(zmq::sockopt::rcvtimeo, esperaMs_);   // no esperar más de esto
        socket_.set(zmq::sockopt::linger, 0);             // al cerrar, no retener lo pendiente
        socket_.connect(direcciones_[actual_]);
    }

    zmq::context_t& contexto_;
    std::vector<std::string> direcciones_;
    int esperaMs_;
    int intentos_;
    size_t actual_ = 0;
    zmq::socket_t socket_;
};

#endif
