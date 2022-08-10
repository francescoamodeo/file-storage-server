#!/bin/bash
echo ""
echo -e "< TEST 3 STARTING..."

BASEDIR="$(cd "$(dirname "$(dirname "${BASH_SOURCE[0]}")")" && pwd)"

echo -e "< Starting server..."
"$BASEDIR"/bin/server -f "$BASEDIR"/tests/test3/config3.txt &
# server pid
SERVER_PID=$!
export SERVER_PID

sleep 2

# faccio partire lo script per generare i client
echo -e "< Starting clients..."
array_id=()
for i in {0..9}; do
	"$BASEDIR"/scripts/start_clients.sh "${i}" &
	array_id+=($!)
	sleep 0.1
done
# Aspetto 30 secondi
echo "< Test started. Wait 30 seconds for the result"
sleep 30
echo ""
echo -e "< Terminating server with SIGINT"
echo ""
kill -s SIGINT ${SERVER_PID}

sleep 1

# stop processi
echo ""
echo -e "< Terminating clients with SIGKILL"
echo ""
for i in "${array_id[@]}"; do
	kill -9 "${i}"
  wait "${i}"
done

echo ""

wait ${SERVER_PID}

echo -e "< TEST 3 COMPLETED"
echo ""