#!/bin/bash

BASEDIR="$(cd "$(dirname "$(dirname "${BASH_SOURCE[0]}")")" && pwd)"
SOCKET="$BASEDIR"/storage_sock.sk
STOREDIR="$BASEDIR"/tests/test3/store
SENDDIR="$BASEDIR"/tests/test3/send

#argomento passato da test3.sh
folder_index=$1
FOLDER="$SENDDIR/send${folder_index}"

while true; do
  "$BASEDIR"/bin/client -a "client${folder_index}" -f "$SOCKET" -w "${FOLDER}"
  "$BASEDIR"/bin/client -a "client${folder_index}" -f "$SOCKET" -r "$FOLDER"/file0,"$FOLDER"/file1,"$FOLDER"/file2,"$FOLDER"/file3,"$FOLDER"/file4 -d "$STOREDIR/store${folder_index}"
  "$BASEDIR"/bin/client -a "client${folder_index}" -f "$SOCKET" -l "$FOLDER"/file3,"$FOLDER"/file4 -u "$FOLDER"/file4
  "$BASEDIR"/bin/client -a "client${folder_index}" -f "$SOCKET" -r "$FOLDER"/file5,"$FOLDER"/file6,"$FOLDER"/file7,"$FOLDER"/file8,"$FOLDER"/file9 -d "$STOREDIR/store${folder_index}"
done

sleep 0.5

exit 0