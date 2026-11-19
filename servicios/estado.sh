#!/bin/bash
# Muestra en vivo el estado de los servicios del SRB en esta VM (se refresca cada segundo).
#
# uso:  ./servicios/estado.sh        (Ctrl+C para salir; no necesita sudo)
#
# Por cada servicio: estado, PID actual, cuantas veces lo ha relanzado systemd y hace
# cuanto arranco. Si matan un proceso se ve cambiar el PID, subir REINICIOS y volver
# a 0 el tiempo arriba.
#
# En la VM de la persistencia muestra ademas la base de datos: cuantas reservas hay,
# si las cuentas cuadran, las ultimas reservas y los eventos con asientos ocupados.
# La lee en modo solo lectura (necesita el comando sqlite3: sudo apt install sqlite3).

VERDE=$'\e[32m'; ROJO=$'\e[31m'; AMARILLO=$'\e[33m'; NEGRITA=$'\e[1m'; NORMAL=$'\e[0m'
BD="$(cd "$(dirname "$0")/.." && pwd)/reserva.db"     # la misma que usa el servicio de persistencia

# consulta de solo lectura; si la persistencia esta escribiendo, espera hasta 0,5 s
consultar() { local sql="$1"; shift; sqlite3 -readonly -cmd ".timeout 500" "$@" "$BD" "$sql" 2>&1; }

seccionBD() {
    echo -e "\n${NEGRITA}BASE DE DATOS${NORMAL}  $BD"
    if ! command -v sqlite3 >/dev/null; then
        echo "Para verla hay que instalar sqlite3: sudo apt install sqlite3"
        return
    fi

    read -r reservas asientos < <(consultar "SELECT COUNT(*), IFNULL(SUM(numAsientos),0) FROM Reservas;" -separator ' ')
    # un evento esta descuadrado si sus asientos no suman 100 o si los ocupados no son
    # exactamente la suma de sus reservas (eso seria una reserva perdida o duplicada)
    descuadrados=$(consultar "SELECT COUNT(*) FROM Eventos e
        WHERE asientosLibres < 0 OR asientosLibres + asientosOcupados <> 100
           OR asientosOcupados <> (SELECT IFNULL(SUM(numAsientos),0) FROM Reservas r
                                   WHERE r.idEvento = e.idEvento AND r.fecha = e.fecha);")

    echo "Reservas: $reservas ($asientos asientos)"
    if [ "$descuadrados" = 0 ]; then
        echo "Cuentas:  ${VERDE}cuadran (libres + ocupados = 100 y ocupados = suma de reservas)${NORMAL}"
    else
        echo "Cuentas:  ${ROJO}$descuadrados evento(s) descuadrado(s)${NORMAL}"
    fi

    echo -e "\nUltimas reservas:"
    consultar "SELECT idReservas AS id, idEvento AS evento, fecha, cliente, numAsientos AS asientos
               FROM Reservas ORDER BY idReservas DESC LIMIT 5;" -header -column
    echo -e "\nEventos con asientos ocupados:"
    consultar "SELECT idEvento AS evento, fecha, asientosLibres AS libres, asientosOcupados AS ocupados
               FROM Eventos WHERE asientosOcupados > 0 ORDER BY asientosOcupados DESC LIMIT 5;" -header -column
}

while true; do
    ahora=$(cut -d' ' -f1 /proc/uptime | cut -d. -f1)     # segundos desde que prendio la VM
    pantalla="${NEGRITA}SRB en $(hostname)  -  $(date +%H:%M:%S)${NORMAL}\n\n"
    pantalla+="$(printf '%-24s %-26s %-9s %-10s %s' SERVICIO ESTADO PID REINICIOS 'ARRIBA HACE')\n"

    unidades=$(systemctl list-units --all --plain --no-legend 'srb-*' | awk '{print $1}')
    if [ -z "$unidades" ]; then
        pantalla+="\nNo hay servicios del SRB en esta VM (sudo ./servicios/instalar.sh vm2 | vm3)\n"
    fi

    for unidad in $unidades; do
        declare -A p=()
        while IFS='=' read -r clave valor; do p[$clave]=$valor; done < <(
            systemctl show "$unidad" -p ActiveState -p SubState -p MainPID -p NRestarts \
                                     -p ActiveEnterTimestampMonotonic)

        estado="${p[ActiveState]} (${p[SubState]})"
        [ "${p[ActiveState]}" = active ] && color=$VERDE || color=$ROJO

        reinicios=${p[NRestarts]:-0}
        [ "$reinicios" -gt 0 ] && colorReinicios=$AMARILLO || colorReinicios=$NORMAL

        arriba="-"
        inicio=${p[ActiveEnterTimestampMonotonic]:-0}      # microsegundos desde que prendio la VM
        if [ "${p[ActiveState]}" = active ] && [ "$inicio" -gt 0 ]; then
            segundos=$(( ahora - inicio / 1000000 ))
            arriba="$(( segundos / 60 )) min $(( segundos % 60 )) s"
        fi

        pid=${p[MainPID]}; [ "$pid" = 0 ] && pid="-"
        pantalla+="$(printf '%-24s %s%-26s%s %-9s %s%-10s%s %s' "${unidad%.service}" \
                     "$color" "$estado" "$NORMAL" "$pid" "$colorReinicios" "$reinicios" "$NORMAL" "$arriba")\n"
        unset p
    done

    # la BD solo se muestra en la VM donde corre la persistencia
    if [ -f "$BD" ] && { [[ "$unidades" == *srb-persistencia* ]] || pgrep -x persistencia >/dev/null; }; then
        bd="$(seccionBD)"
    else
        bd=""
    fi

    clear
    printf '%b' "$pantalla"
    printf '%s\n' "$bd"
    printf "\nCtrl+C para salir.  Mensajes en vivo: journalctl -f -u 'srb-*'\n"
    sleep 1
done
