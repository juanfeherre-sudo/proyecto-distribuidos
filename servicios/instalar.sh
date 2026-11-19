#!/bin/bash
# Instala los servicios de systemd del SRB en esta VM, para que systemd relance
# cada proceso cuando muere o se traba (patron Supervisor / Watchdog).
#
# uso:  sudo ./servicios/instalar.sh vm2      gestores 1 y 2, RMC 1 a 3 y cancelaciones
#       sudo ./servicios/instalar.sh vm3      persistencia
#       sudo ./servicios/instalar.sh quitar   detiene y borra todos los servicios del SRB
#
# Antes: make. Mientras los servicios esten activos no se corren los programas a mano
# en esa VM, porque chocarian por los puertos.
set -e

DIR="$(cd "$(dirname "$0")/.." && pwd)"    # carpeta del proyecto (donde estan los binarios y config.txt)
USUARIO="${SUDO_USER:-$USER}"              # el usuario que llamo a sudo, para no correr como root

case "$1" in
    vm2) SERVICIOS="srb-gestor@1 srb-gestor@2 srb-rmc@1 srb-rmc@2 srb-rmc@3 srb-cancelaciones" ;;
    vm3) SERVICIOS="srb-persistencia" ;;
    quitar)
        systemctl disable --now 'srb-*' 2>/dev/null || true
        rm -f /etc/systemd/system/srb-*.service
        systemctl daemon-reload
        echo "Servicios del SRB detenidos y borrados."
        exit 0 ;;
    *) echo "uso: sudo $0 vm2 | vm3 | quitar"; exit 1 ;;
esac

if [ "$(id -u)" -ne 0 ]; then echo "Hay que correrlo con sudo."; exit 1; fi
for programa in gestor rmc cancelaciones persistencia; do
    [ -x "$DIR/$programa" ] || { echo "Falta $DIR/$programa: corra make primero."; exit 1; }
done
[ -f "$DIR/config.txt" ] || { echo "Falta $DIR/config.txt"; exit 1; }

# copiar las plantillas a systemd con la carpeta y el usuario reales
for plantilla in "$DIR"/servicios/*.service; do
    sed -e "s|@DIR@|$DIR|g" -e "s|@USUARIO@|$USUARIO|g" "$plantilla" \
        > "/etc/systemd/system/$(basename "$plantilla")"
done

systemctl daemon-reload
systemctl enable --now $SERVICIOS     # arrancar ya y en cada encendido de la VM
sleep 2
systemctl --no-pager --lines=0 status $SERVICIOS | grep -E "^● |Active:|Main PID:"
