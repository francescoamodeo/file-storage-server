#!/bin/bash
echo ""
echo -e "< TEST 2 STARTING..."
BASEDIR="$(cd "$(dirname "$(dirname "${BASH_SOURCE[0]}")")" && pwd)"
SOCKET="$BASEDIR"/storage_sock.sk
EJECTDIR="$BASEDIR"/tests/test2/ejected
SENDDIR="$BASEDIR"/tests/test2/send

echo -e "< Starting server..."
"$BASEDIR"/bin/server -f "$BASEDIR"/tests/test2/config2.txt &
# server pid
SERVER_PID=$!
export SERVER_PID

sleep 2

echo -e "< Starting clients..."
echo ""

"$BASEDIR"/bin/client -a client1 -p -f "$SOCKET" -w "$SENDDIR"

sleep 1

#dovrebbe espellere il file2
"$BASEDIR"/bin/client -a client2 -p -f "$SOCKET" -W "$BASEDIR"/tests/test2/trigger -D "$EJECTDIR"


echo ""
echo -e "< Terminating server with SIGHUP"
echo ""
kill -s SIGHUP $SERVER_PID
wait $SERVER_PID
echo ""
echo -e "< TEST 2 COMPLETED"
echo ""