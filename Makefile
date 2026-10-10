# Compila los 5 programas. Uso: make        (todo)
#                               make clean  (borra binarios)
CXX      = g++
CXXFLAGS = -std=c++17 -Wall -O2 -I.
ZMQ      = -lzmq
SQLITE   = -lsqlite3

all: cliente gestor rmc cancelacion persistencia

cliente: cliente.cpp common.hpp
	$(CXX) $(CXXFLAGS) cliente.cpp -o cliente $(ZMQ) -pthread

gestor: gestor.cpp common.hpp
	$(CXX) $(CXXFLAGS) gestor.cpp -o gestor $(ZMQ)

rmc: rmc.cpp common.hpp
	$(CXX) $(CXXFLAGS) rmc.cpp -o rmc $(ZMQ)

cancelacion: cancelacion.cpp common.hpp
	$(CXX) $(CXXFLAGS) cancelacion.cpp -o cancelacion $(ZMQ)

persistencia: persistencia.cpp common.hpp
	$(CXX) $(CXXFLAGS) persistencia.cpp -o persistencia $(ZMQ) $(SQLITE)

clean:
	rm -f cliente gestor rmc cancelacion persistencia

.PHONY: all clean
