// common.hpp
// Código que comparten TODOS los programas: leer la configuración,
// enviar/recibir texto por ZeroMQ, partir mensajes y escribir logs.
#ifndef COMMON_HPP
#define COMMON_HPP

#include <zmq.hpp>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstddef>
#include <cstdlib>
#include <cstring>
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

// ---------------------------------------------------------------------------
// Aviso a systemd (patrón Supervisor / Watchdog).
// Cuando el programa corre como servicio de systemd (Type=notify), systemd le
// pasa en la variable NOTIFY_SOCKET un socket por donde espera dos mensajes:
//   READY=1     una vez, cuando el programa ya hizo sus bind y está listo
//   WATCHDOG=1  seguido; si deja de llegar durante WatchdogSec, systemd da el
//               proceso por trabado, lo mata y lo vuelve a arrancar
// Si el programa se corre a mano en una terminal, NOTIFY_SOCKET no existe y
// estas funciones no hacen nada.
// ---------------------------------------------------------------------------
inline void avisarSystemd(const char* mensaje) {
    const char* ruta = std::getenv("NOTIFY_SOCKET");
    if (!ruta || (ruta[0] != '/' && ruta[0] != '@')) return;

    sockaddr_un dir{};
    dir.sun_family = AF_UNIX;
    size_t largo = std::strlen(ruta);
    if (largo >= sizeof(dir.sun_path)) return;
    std::memcpy(dir.sun_path, ruta, largo);
    if (dir.sun_path[0] == '@') dir.sun_path[0] = '\0';   // socket "abstracto" de Linux

    int fd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return;
    sendto(fd, mensaje, std::strlen(mensaje), 0,
           reinterpret_cast<sockaddr*>(&dir), offsetof(sockaddr_un, sun_path) + largo);
    close(fd);
}
inline void avisarListo() { avisarSystemd("READY=1"); }
inline void avisarVivo()  { avisarSystemd("WATCHDOG=1"); }

// Los sockets que esperan solicitudes despiertan al menos cada PULSO_MS aunque no
// llegue nada, para poder mandar WATCHDOG=1. Sin esto, un proceso sano pero sin
// trabajo se quedaría callado en el recv y systemd lo mataría.
const int PULSO_MS = 1000;

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
// Es el patrón "Lazy Pirate" de la guía de ZeroMQ.
// OJO: reenviar una escritura (una reserva) puede duplicarla si la primera sí
// se aplicó y lo que se perdió fue la respuesta. Por eso pedir() deja indicar
// cuántos intentos se hacen: las lecturas se reintentan, las escrituras no,
// hasta que el protocolo tenga un id único por solicitud.
// ---------------------------------------------------------------------------
class ReqConReintento {
public:
    ReqConReintento(zmq::context_t& contexto, const std::vector<std::string>& direcciones,
                    int esperaMs, int intentos, const std::string& quien)
        : contexto_(contexto), direcciones_(direcciones), esperaMs_(esperaMs),
          intentos_(intentos), quien_(quien), socket_(contexto, zmq::socket_type::req) {
        abrir();
    }

    // Devuelve la respuesta. Si se agotaron los intentos devuelve "nada" (optional vacío),
    // que NO es lo mismo que una respuesta vacía: una consulta sin resultados responde "".
    // 'intentos' en 0 usa los del constructor; 1 = no reenviar.
    std::optional<std::string> pedir(const std::string& mensaje, int intentos = 0) {
        int maximo = intentos > 0 ? intentos : intentos_;
        for (int intento = 1; intento <= maximo; intento++) {
            enviar(socket_, mensaje);
            zmq::message_t respuesta;
            if (socket_.recv(respuesta, zmq::recv_flags::none)) return respuesta.to_string();

            // No contestó a tiempo: botar el socket y probar la siguiente dirección.
            log(quien_, direcciones_[actual_] + " no respondio en " + std::to_string(esperaMs_) +
                        " ms (intento " + std::to_string(intento) + " de " + std::to_string(maximo) + ")");
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
    std::string quien_;
    size_t actual_ = 0;
    zmq::socket_t socket_;
};

#endif
