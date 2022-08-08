#!/bin/bash
echo ""
echo -e "< TEST 1 STARTING..."
BASEDIR="$(cd "$(dirname "$(dirname "${BASH_SOURCE[0]}")")" && pwd)"
SOCKET="$BASEDIR"/storage_sock.sk
STOREDIR="$BASEDIR"/tests/test1/store
EJECTDIR="$BASEDIR"/tests/test1/ejected
SENDDIR="$BASEDIR"/tests/test1/send
echo -e "< Starting server..."
valgrind --leak-check=full "$BASEDIR"/bin/server -f "$BASEDIR"/tests/test1/config1.txt &
# server pid
SERVER_PID=$!
export SERVER_PID

echo -e "< Starting clients..."
echo ""
"$BASEDIR"/bin/client -a client1 -f "$SOCKET" -h
"$BASEDIR"/bin/client -a client1 -f "$SOCKET" -p -t200
# Scrittura di file
"$BASEDIR"/bin/client -a client1 -f "$SOCKET" -p -t200 -W "$SENDDIR"/file7,"$SENDDIR"/file8 -D "$EJECTDIR"
# Scritture e letture dallo stesso client
"$BASEDIR"/bin/client -a client2 -f "$SOCKET" -p -t200 -w "$SENDDIR",7 -D "$EJECTDIR" -r "$SENDDIR"/file4 -d "$STOREDIR"
# Lock e rimozione di file
"$BASEDIR"/bin/client -a client3 -f "$SOCKET" -p -t200 -l "$SENDDIR"/file2 -c "$SENDDIR"/file2 &
sleep 1
# Unlock per poter permettere di completare la lock
"$BASEDIR"/bin/client -a client2 -f "$SOCKET" -p -t200 -u "$SENDDIR"/file2,"$SENDDIR"/file4,"$SENDDIR"/file5
# Lettura di N files
"$BASEDIR"/bin/client -a client4 -f "$SOCKET" -p -t200 -R -d "$STOREDIR"
# Lettura di file salvandoli nella cartella specificata
"$BASEDIR"/bin/client -a client1 -f "$SOCKET" -p -t200 -r "$SENDDIR"/file7,"$SENDDIR"/file8 -d "$STOREDIR"
# Lettura di 2 file dal server salvandoli nella cartella specificata
"$BASEDIR"/bin/client -a client2 -f "$SOCKET" -p -t200 -R5 -d "$STOREDIR"

echo ""
echo -e "< Terminating server with SIGHUP"
echo ""
kill -s SIGHUP $SERVER_PID
wait $SERVER_PID
echo ""
echo -e "< TEST 1 COMPLETED"
echo ""
